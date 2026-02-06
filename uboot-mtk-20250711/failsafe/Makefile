# SPDX-License-Identifier: GPL-2.0

ifeq ($(CONFIG_XPL_BUILD),)

ifeq ($(CONFIG_WEBUI_FAILSAFE_UI_OLD),y)
obj-y += fsdata_old/
else
obj-y += fsdata/
endif

obj-y += fs.o failsafe.o

endif
