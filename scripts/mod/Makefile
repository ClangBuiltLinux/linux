# SPDX-License-Identifier: GPL-2.0
OBJECT_FILES_NON_STANDARD := y
CFLAGS_REMOVE_empty.o += $(CC_FLAGS_LTO)

hostprogs-always-y	+= modpost mk_elfconfig
libelf_flags	:= $(shell $(HOSTPKG_CONFIG) --cflags libelf)
HOSTCFLAGS_mk_elfconfig.o	+= $(libelf_flags)
HOSTCFLAGS_modpost.o	+= $(libelf_flags) -include $(obj)/elf_helper.h
HOSTCFLAGS_sumversion.o	+= $(libelf_flags)
#HOSTCFLAGS_file2alias.o	+= $(libelf_flags) -U_FORTIFY_SOURCE
HOSTCFLAGS_file2alias.o	+= $(libelf_flags) -D_FORTIFY_SOURCE=0
always-y		+= empty.o

modpost-objs	:= modpost.o file2alias.o sumversion.o

devicetable-offsets-file := devicetable-offsets.h

$(obj)/$(devicetable-offsets-file): $(obj)/devicetable-offsets.s FORCE
	$(call filechk,offsets,__DEVICETABLE_OFFSETS_H__)

targets += $(devicetable-offsets-file) devicetable-offsets.s

# dependencies on generated files need to be listed explicitly

$(obj)/modpost.o $(obj)/file2alias.o $(obj)/sumversion.o: $(obj)/elfconfig.h
$(obj)/file2alias.o: $(obj)/$(devicetable-offsets-file)

quiet_cmd_elfconfig = MKELF   $@
      cmd_elfconfig = $(obj)/mk_elfconfig < $< > $@

$(obj)/elfconfig.h: $(obj)/empty.o $(obj)/mk_elfconfig FORCE
	$(call if_changed,elfconfig)

targets += elfconfig.h
