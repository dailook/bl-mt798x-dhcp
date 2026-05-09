// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 MediaTek Inc.
 * Author: Sam Shih <sam.shih@mediatek.com>
 */

#include <asm/io.h>
#include <asm/global_data.h>
#include <linux/sizes.h>
#include <linux/types.h>

DECLARE_GLOBAL_DATA_PTR;

int board_init(void)
{
	return 0;
}

#define	MT7986_BOOT_NOR		0
#define	MT7986_BOOT_SPIM_NAND	1
#define	MT7986_BOOT_EMMC	2
#define	MT7986_BOOT_SNFI_NAND	3

const char *mtk_board_rootdisk(void)
{
	switch ((readl(0x1001f6f0) & 0x300) >> 8) {
	case MT7986_BOOT_NOR:
		return "nor";

	case MT7986_BOOT_SPIM_NAND:
		return "spim-nand";

	case MT7986_BOOT_EMMC:
		return "emmc";

	case MT7986_BOOT_SNFI_NAND:
		return "sd";

	default:
		return "";
	}
}

ulong board_get_load_addr(void)
{
	if (gd->ram_size <= SZ_128M)
		return gd->ram_base;

	if (gd->ram_size <= SZ_256M)
		return gd->ram_top - SZ_64M;

	return gd->ram_base + SZ_256M;
}


const static struct {
	const char *name;
	const char *desc;
	int group;
} board_fit_conf_info[] = {
	{ "mt7986-rfb-emmc", "Image on eMMC", 0 },
	{ "mt7986-rfb-sd", "Image on SD", 0 },
	{ "mt7986-rfb-snfi-nand", "Image on SNFI-NAND", 0 },
	{ "mt7986-rfb-spim-nand", "Image on SPIM-NAND (UBI)", 0 },
	{ "mt7986-rfb-spim-nand-factory", "UBI \"factory\" volume config", -1 },
	{ "mt7986-rfb-spim-nand-nmbm", "Image on SPIM-NAND (NMBM)", 0 },
	{ "mt7986-rfb-spim-nor", "Image on SPI-NOR", 0 },
};

int mtk_board_get_fit_conf_info(const char *name, const char **retdesc)
{
	uint32_t i;

	for (i = 0; i < ARRAY_SIZE(board_fit_conf_info); i++) {
		if (!strcmp(name, board_fit_conf_info[i].name)) {
			(*retdesc) = board_fit_conf_info[i].desc;
			return board_fit_conf_info[i].group;
		}
	}

	(*retdesc) = NULL;
	return -1;
}
