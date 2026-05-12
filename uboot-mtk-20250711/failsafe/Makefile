# SPDX-License-Identifier: GPL-2.0

WEBUI_FAILSAFE_GIT_HASH := $(shell \
	if git -C $(srctree) rev-parse --short=7 HEAD >/dev/null 2>&1; then \
		git -C $(srctree) rev-parse --short=7 HEAD; \
	elif git -C $(srctree)/.. rev-parse --short=7 HEAD >/dev/null 2>&1; then \
		git -C $(srctree)/.. rev-parse --short=7 HEAD; \
	else \
		echo unknown; \
	fi)

WEBUI_FAILSAFE_GIT_DIRTY := $(shell \
	if git -C $(srctree) rev-parse --is-inside-work-tree >/dev/null 2>&1; then \
		test -n "$$(git -C $(srctree) status --porcelain --untracked-files=no 2>/dev/null)" && echo 1 || echo 0; \
	elif git -C $(srctree)/.. rev-parse --is-inside-work-tree >/dev/null 2>&1; then \
		test -n "$$(git -C $(srctree)/.. status --porcelain --untracked-files=no 2>/dev/null)" && echo 1 || echo 0; \
	else \
		echo 0; \
	fi)

ccflags-y += -DWEBUI_FAILSAFE_GIT_HASH=\"$(WEBUI_FAILSAFE_GIT_HASH)\"
ccflags-y += -DWEBUI_FAILSAFE_GIT_DIRTY=$(WEBUI_FAILSAFE_GIT_DIRTY)

ifeq ($(CONFIG_XPL_BUILD),)

obj-y += fsdata/

endif

obj-y += fs.o failsafe.o
obj-$(CONFIG_WEBUI_FAILSAFE_CONSOLE) += failsafe_console.o
obj-$(CONFIG_WEBUI_FAILSAFE_ENV) += failsafe_env.o
obj-$(CONFIG_WEBUI_FAILSAFE_BACKUP) += failsafe_backup_flash.o