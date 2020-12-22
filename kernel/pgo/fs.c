// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Google, Inc.
 *
 * Author:
 *	Sami Tolvanen <samitolvanen@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "pgo.h"

static struct dentry *directory;

struct prf_private_data {
	void *buffer;
	unsigned long size;
};

static void prf_fill_header(void **buffer)
{
	struct llvm_prf_header *header = *(struct llvm_prf_header **)buffer;

	header->magic = LLVM_PRF_MAGIC;
	header->version = LLVM_PRF_VARIANT_MASK_IR | LLVM_PRF_VERSION;
	header->data_size = prf_data_count();
	header->padding_bytes_before_counters = 0;
	header->counters_size = prf_cnts_count();
	header->padding_bytes_after_counters = 0;
	header->names_size = prf_names_count();
	header->counters_delta = (u64)__llvm_prf_cnts_start;
	header->names_delta = (u64)__llvm_prf_names_start;
	header->value_kind_last = LLVM_PRF_IPVK_LAST;

	*buffer += sizeof(*header);
}

static void prf_copy_buffer(void **buffer, void *src, unsigned long size)
{
	memcpy(*buffer, src, size);
	*buffer += size;
}

/*
 * Profile data format:
 *	- llvm_prf_header
 *	- __llvm_prf_data
 *	- __llvm_prf_cnts
 *	- __llvm_prf_names
 *	- zero padding to 8 bytes
 *	- for each llvm_prf_data in __llvm_prf_data:
 *		- llvm_prf_value_data
 *			- llvm_prf_value_record + site count array
 *				- llvm_prf_value_node_data
 *				...
 *			...
 *		...
 */

static u32 __prf_get_value_size(struct llvm_prf_data *p, u32 *value_kinds)
{
	struct llvm_prf_value_node **nodes =
		(struct llvm_prf_value_node **)p->values;
	u32 kinds = 0;
	u32 size = 0;
	unsigned int kind;
	unsigned int n;
	unsigned int s = 0;

	for (kind = 0; kind < ARRAY_SIZE(p->num_value_sites); kind++) {
		unsigned int sites = p->num_value_sites[kind];

		if (!sites)
			continue;

		/* record + site count array */
		size += prf_get_value_record_size(sites);
		kinds++;

		if (!nodes)
			continue;

		for (n = 0; n < sites; n++) {
			u32 count = 0;
			struct llvm_prf_value_node *site = nodes[s + n];

			while (site && ++count <= U8_MAX)
				site = site->next;

			/* value data */
			size += count *
				sizeof(struct llvm_prf_value_node_data);
		}

		s += sites;
	}

	/* header */
	if (size)
		size += sizeof(struct llvm_prf_value_data);

	if (value_kinds)
		*value_kinds = kinds;

	return size;
}

static u32 prf_get_value_size(void)
{
	u32 size = 0;
	struct llvm_prf_data *p;

	for (p = __llvm_prf_data_start; p < __llvm_prf_data_end; p++)
		size += __prf_get_value_size(p, NULL);

	return size;
}

static void prf_serialize_value(struct llvm_prf_data *p, void **buffer)
{
	struct llvm_prf_value_data header;
	struct llvm_prf_value_node **nodes =
		(struct llvm_prf_value_node **)p->values;
	unsigned int kind;
	unsigned int n;
	unsigned int s = 0;

	header.total_size = __prf_get_value_size(p, &header.num_value_kinds);

	if (!header.num_value_kinds)
		return; /* nothing to write */

	prf_copy_buffer(buffer, &header, sizeof(header));

	for (kind = 0; kind < ARRAY_SIZE(p->num_value_sites); kind++) {
		struct llvm_prf_value_record *record;
		u8 *counts;
		unsigned int sites = p->num_value_sites[kind];

		if (!sites)
			continue;

		/* record */
		record = *(struct llvm_prf_value_record **)buffer;
		*buffer += prf_get_value_record_header_size();

		record->kind = kind;
		record->num_value_sites = sites;

		/* site count array */
		counts = *(u8 **)buffer;
		*buffer += prf_get_value_record_site_count_size(sites);

		/*
		 * If we don't have nodes, we can skip updating the site count
		 * array, because the buffer is zero filled.
		 */
		if (!nodes)
			continue;

		for (n = 0; n < sites; n++) {
			u32 count = 0;
			struct llvm_prf_value_node *site = nodes[s + n];

			while (site && ++count <= U8_MAX) {
				prf_copy_buffer(buffer, site,
				  sizeof(struct llvm_prf_value_node_data));
				site = site->next;
			}

			counts[n] = (u8)count;
		}

		s += sites;
	}
}

static void prf_serialize_values(void **buffer)
{
	struct llvm_prf_data *p;

	for (p = __llvm_prf_data_start; p < __llvm_prf_data_end; p++)
		prf_serialize_value(p, buffer);
}

static unsigned long prf_buffer_size(void)
{
	return sizeof(struct llvm_prf_header) +
			prf_data_size()	+
			prf_cnts_size() +
			prf_names_size() +
			prf_get_padding(prf_names_size()) +
			prf_get_value_size();
}

static int prf_serialize(struct prf_private_data *p)
{
	int err = 0;
	unsigned long flags;
	void *buffer;

	flags = prf_serialize_lock();

	p->size = prf_buffer_size();
	p->buffer = vzalloc(p->size);

	if (!p->buffer) {
		err = -ENOMEM;
		goto out;
	}

	buffer = p->buffer;

	prf_fill_header(&buffer);
	prf_copy_buffer(&buffer, __llvm_prf_data_start,  prf_data_size());
	prf_copy_buffer(&buffer, __llvm_prf_cnts_start,  prf_cnts_size());
	prf_copy_buffer(&buffer, __llvm_prf_names_start, prf_names_size());
	buffer += prf_get_padding(prf_names_size());

	prf_serialize_values(&buffer);

out:
	prf_serialize_unlock(flags);
	return err;
}

static int prf_open(struct inode *inode, struct file *file)
{
	struct prf_private_data *data;
	int err;

	WARN_ON(file->private_data);

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	err = prf_serialize(data);
	if (err) {
		kfree(data);
		return err;
	}

	file->private_data = data;
	return 0;
}

static ssize_t prf_read(struct file *file, char __user *buf, size_t count,
			loff_t *ppos)
{
	struct prf_private_data *data = file->private_data;

	WARN_ON(!data);
	return simple_read_from_buffer(buf, count, ppos, data->buffer,
				       data->size);
}

static int prf_release(struct inode *inode, struct file *file)
{
	struct prf_private_data *data = file->private_data;

	if (data) {
		vfree(data->buffer);
		kfree(data);
	}

	return 0;
}

static const struct file_operations prf_fops = {
	.owner		= THIS_MODULE,
	.open		= prf_open,
	.read		= prf_read,
	.llseek		= default_llseek,
	.release	= prf_release
};

static ssize_t prf_reset_write(struct file *file, const char __user *addr,
			       size_t len, loff_t *pos)
{
	memset(__llvm_prf_cnts_start, 0, prf_cnts_size());
	return len;
}

static ssize_t prf_reset_read(struct file *file, char __user *addr, size_t len,
			      loff_t *pos)
{
	/* Allow read operation so that a recursive copy won't fail. */
	return 0;
}

static const struct file_operations prf_reset_fops = {
	.owner		= THIS_MODULE,
	.write		= prf_reset_write,
	.read		= prf_reset_read,
	.llseek		= noop_llseek,
};

static int __init pgo_init(void)
{
	struct dentry *entry;

	directory = debugfs_create_dir("pgo", NULL);
	if (!directory)
		goto err_remove;

	entry = debugfs_create_file("profraw", 0644, directory, NULL,
				    &prf_fops);
	if (!entry)
		goto err_remove;

	entry = debugfs_create_file("reset", 0666, directory, NULL,
				    &prf_reset_fops);
	if (!entry)
		goto err_remove;

	return 0;

err_remove:
	pr_err("init failed\n");
	return -EIO;
}

static void __exit pgo_exit(void)
{
	debugfs_remove_recursive(directory);
}

module_init(pgo_init);
module_exit(pgo_exit);
