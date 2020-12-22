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

#ifndef _PGO_H
#define _PGO_H

/* Note: These internal LLVM definitions must match the compiler version */

#ifdef CONFIG_64BIT
	#define LLVM_PRF_MAGIC		\
		((u64)255 << 56 |	\
		 (u64)'l' << 48 |	\
		 (u64)'p' << 40 |	\
		 (u64)'r' << 32 |	\
		 (u64)'o' << 24 |	\
		 (u64)'f' << 16 |	\
		 (u64)'r' << 8  |	\
		 (u64)129)
#else
	#define LLVM_PRF_MAGIC		\
		((u64)255 << 56 |	\
		 (u64)'l' << 48 |	\
		 (u64)'p' << 40 |	\
		 (u64)'r' << 32 |	\
		 (u64)'o' << 24 |	\
		 (u64)'f' << 16 |	\
		 (u64)'R' << 8  |	\
		 (u64)129)
#endif

#define LLVM_PRF_VERSION		5
#define LLVM_PRF_DATA_ALIGN		8
#define LLVM_PRF_IPVK_LAST		1
#define LLVM_PRF_MAX_NUM_VALS_PER_SITE	16

#define LLVM_PRF_VARIANT_MASK_IR	(0x1ull << 56)
#define LLVM_PRF_VARIANT_MASK_CSIR	(0x1ull << 57)

struct llvm_prf_header {
	u64 magic;
	u64 version;
	u64 data_size;
	u64 padding_bytes_before_counters;
	u64 counters_size;
	u64 padding_bytes_after_counters;
	u64 names_size;
	u64 counters_delta;
	u64 names_delta;
	u64 value_kind_last;
};

struct llvm_prf_data {
	const u64 name_ref;
	const u64 func_hash;
	const void *counter_ptr;
	const void *function_ptr;
	void *values;
	const u32 num_counters;
	const u16 num_value_sites[LLVM_PRF_IPVK_LAST + 1];
} __aligned(LLVM_PRF_DATA_ALIGN);

struct llvm_prf_value_node_data {
	u64 value;
	u64 count;
};

struct llvm_prf_value_node {
	u64 value;
	u64 count;
	struct llvm_prf_value_node *next;
};

struct llvm_prf_value_data {
	u32 total_size;
	u32 num_value_kinds;
};

struct llvm_prf_value_record {
	u32 kind;
	u32 num_value_sites;
	u8 site_count_array[1];
};

#define prf_get_value_record_header_size() \
	offsetof(struct llvm_prf_value_record, site_count_array)
#define prf_get_value_record_site_count_size(sites) \
	roundup((sites), 8)
#define prf_get_value_record_size(sites) \
	(prf_get_value_record_header_size() + \
	 prf_get_value_record_site_count_size((sites)))

/* Data sections */
extern struct llvm_prf_data __llvm_prf_data_start[];
extern struct llvm_prf_data __llvm_prf_data_end[];

extern u64 __llvm_prf_cnts_start[];
extern u64 __llvm_prf_cnts_end[];

extern char __llvm_prf_names_start[];
extern char __llvm_prf_names_end[];

extern struct llvm_prf_value_node __llvm_prf_vnds_start[];
extern struct llvm_prf_value_node __llvm_prf_vnds_end[];

/* Locking for vnodes */
extern unsigned long prf_serialize_lock(void);
extern void prf_serialize_unlock(unsigned long flags);

#define __DEFINE_PRF_SIZE(s) \
	static inline unsigned long prf_ ## s ## _size(void)		\
	{								\
		unsigned long start =					\
			(unsigned long)__llvm_prf_ ## s ## _start;	\
		unsigned long end =					\
			(unsigned long)__llvm_prf_ ## s ## _end;	\
		return roundup(end - start,				\
				sizeof(__llvm_prf_ ## s ## _start[0]));	\
	}								\
	static inline unsigned long prf_ ## s ## _count(void)		\
	{								\
		return prf_ ## s ## _size() /				\
			sizeof(__llvm_prf_ ## s ## _start[0]);		\
	}

__DEFINE_PRF_SIZE(data);
__DEFINE_PRF_SIZE(cnts);
__DEFINE_PRF_SIZE(names);
__DEFINE_PRF_SIZE(vnds);

static inline unsigned long prf_get_padding(unsigned long size)
{
	return 7 & (8 - size % 8);
}

#endif /* _PGO_H */
