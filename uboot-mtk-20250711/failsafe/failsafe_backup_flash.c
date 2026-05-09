/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 *
 * Failsafe backup and flash editor
 */

#include <errno.h>
#include <malloc.h>
#include <limits.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <vsprintf.h>
#include <net/mtk_httpd.h>

#ifdef CONFIG_MTD
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/spi-nor.h>
#include <linux/mtd/spinand.h>
#include "../board/mediatek/common/mtd_helper.h"
#endif

#ifdef CONFIG_MTK_BOOTMENU_MMC
#include "../board/mediatek/common/mmc_helper.h"
#endif

#ifdef CONFIG_PARTITIONS
#include <part.h>
#endif

#include "failsafe_internal.h"

enum backup_phase {
	BACKUP_PHASE_HDR = 0,
	BACKUP_PHASE_DATA = 1,
};

enum backup_src {
	BACKUP_SRC_MTD = 0,
	BACKUP_SRC_MMC = 1,
};

struct backup_session {
	enum backup_src src;
	enum backup_phase phase;

	u64 start;
	u64 end;
	u64 total;
	u64 cur;
	u64 target_size;

	char filename[128];
	char hdr[512];
	int hdr_len;

	void *buf;
	size_t buf_size;

#ifdef CONFIG_MTD
	struct mtd_info *mtd;
#endif
#ifdef CONFIG_MTK_BOOTMENU_MMC
	struct mmc *mmc;
	struct disk_partition dpart;
	u64 mmc_base;
#endif
};

static void str_sanitize_component(char *s)
{
	char *p;

	if (!s)
		return;

	for (p = s; *p; p++) {
		unsigned char c = *p;

		if (isalnum(c) || c == '-' || c == '_' || c == '.')
			continue;

		*p = '_';
	}
}

static int parse_u64_len(const char *s, u64 *out)
{
	char *end;
	unsigned long long v;

	if (!s || !*s || !out)
		return -EINVAL;

	v = simple_strtoull(s, &end, 0);
	if (end == s)
		return -EINVAL;

	while (*end == ' ' || *end == '\t')
		end++;

	if (!*end) {
		*out = (u64)v;
		return 0;
	}

	if (!strcasecmp(end, "k") || !strcasecmp(end, "kb") ||
	    !strcasecmp(end, "kib")) {
		*out = (u64)v * 1024ULL;
		return 0;
	}

	return -EINVAL;
}

static bool mtd_part_exists(const char *name)
{
#ifdef CONFIG_MTD
	struct mtd_info *mtd;

	if (!name || !*name)
		return false;

	gen_mtd_probe_devices();
	mtd = get_mtd_device_nm(name);
	if (IS_ERR(mtd))
		return false;

	put_mtd_device(mtd);
	return true;
#else
	(void)name;
	return false;
#endif
}

#ifdef CONFIG_MTD
static const struct spinand_info *failsafe_spinand_match_info(struct spinand_device *spinand)
{
	size_t i;
	const struct spinand_manufacturer *manufacturer;
	const u8 *id;

	if (!spinand)
		return NULL;

	manufacturer = spinand->manufacturer;
	if (!manufacturer || !manufacturer->chips || !manufacturer->nchips)
		return NULL;

	id = spinand->id.data;

	for (i = 0; i < manufacturer->nchips; i++) {
		const struct spinand_info *info = &manufacturer->chips[i];

		if (!info->devid.id || !info->devid.len)
			continue;

		/* spinand->id.data[0] is manufacturer ID, device ID starts from [1]. */
		if (spinand->id.len < (int)(1 + info->devid.len))
			continue;

		if (!memcmp(id + 1, info->devid.id, info->devid.len))
			return info;
	}

	return NULL;
}

static const char *failsafe_get_mtd_chip_model(struct mtd_info *mtd, char *out, size_t out_sz)
{
	if (!out || !out_sz)
		return "";

	out[0] = '\0';

	if (!mtd)
		return "";

	/* SPI NOR: mtd->priv points to struct spi_nor (see spi-nor-core.c). */
	if (mtd->type == MTD_NORFLASH) {
		struct spi_nor *nor = mtd->priv;

		if (nor && nor->name && nor->name[0]) {
			snprintf(out, out_sz, "%s", nor->name);
			return out;
		}
	}

#if IS_ENABLED(CONFIG_MTD_SPI_NAND)
	/* SPI NAND: mtd->priv points to struct nand_device embedded in spinand_device. */
	if (mtd->type == MTD_NANDFLASH || mtd->type == MTD_MLCNANDFLASH) {
		struct spinand_device *spinand = mtd_to_spinand(mtd);
		const struct spinand_manufacturer *manufacturer;
		const struct spinand_info *info;
		const char *mname = NULL;
		const char *model = NULL;

		if (spinand) {
			manufacturer = spinand->manufacturer;
			info = failsafe_spinand_match_info(spinand);

			if (manufacturer && manufacturer->name && manufacturer->name[0])
				mname = manufacturer->name;
			if (info && info->model && info->model[0])
				model = info->model;

			if (mname && model) {
				snprintf(out, out_sz, "%s %s", mname, model);
				return out;
			}
			if (model) {
				snprintf(out, out_sz, "%s", model);
				return out;
			}
			if (mname) {
				snprintf(out, out_sz, "%s", mname);
				return out;
			}
		}
	}
#endif

	/* Fallback: keep old behavior. */
	if (mtd->name && mtd->name[0]) {
		snprintf(out, out_sz, "%s", mtd->name);
		return out;
	}

	return "";
}
#endif

void backupinfo_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *buf;
	int len = 0;
	int left = 16384;

	if (status == HTTP_CB_CLOSED) {
		free(response->session_data);
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	buf = malloc(left);
	if (!buf) {
		response->status = HTTP_RESP_STD;
		response->data = "{}";
		response->size = strlen(response->data);
		response->info.code = 500;
		response->info.connection_close = 1;
		response->info.content_type = "application/json";
		return;
	}

	len += snprintf(buf + len, left - len, "{");

	/* MMC info + partitions */
	len += snprintf(buf + len, left - len, "\"mmc\":{");
#ifdef CONFIG_MTK_BOOTMENU_MMC
	{
		struct mmc *mmc;
		struct blk_desc *bd;
		bool present;

		mmc = _mmc_get_dev(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0, false);
		bd = mmc ? mmc_get_blk_desc(mmc) : NULL;
		present = mmc && bd && bd->type != DEV_TYPE_UNKNOWN;

		if (present) {
			len += snprintf(buf + len, left - len,
				"\"present\":true,\"vendor\":\"%s\",\"product\":\"%s\",\"blksz\":%lu,\"size\":%llu,",
				bd->vendor, bd->product, (unsigned long)bd->blksz,
				(unsigned long long)mmc->capacity_user);
		} else {
			len += snprintf(buf + len, left - len, "\"present\":false,");
		}

		len += snprintf(buf + len, left - len, "\"parts\":[");
#ifdef CONFIG_PARTITIONS
		if (present) {
			struct disk_partition dpart;
			u32 i = 1;
			bool first = true;

			part_init(bd);
			while (len < left - 128) {
				if (part_get_info(bd, i, &dpart))
					break;

				if (!dpart.name[0]) {
					i++;
					continue;
				}

				len += snprintf(buf + len, left - len,
					"%s{\"name\":\"%s\",\"size\":%llu}",
					first ? "" : ",",
					dpart.name,
					(unsigned long long)dpart.size * dpart.blksz);

				first = false;
				i++;
			}
		}
#endif
		len += snprintf(buf + len, left - len, "]");
	}
#else
	len += snprintf(buf + len, left - len, "\"present\":false,\"parts\":[]");
#endif
	len += snprintf(buf + len, left - len, "},");

	/* MTD info + partitions */
	len += snprintf(buf + len, left - len, "\"mtd\":{");
#ifdef CONFIG_MTD
	{
		struct mtd_info *mtd, *sel = NULL;
		u32 i;
		bool first = true;
		const char *model = NULL;
		char model_buf[128];
		int type = -1;
		bool present = false;

		gen_mtd_probe_devices();

		/* Prefer a master MTD device (mtd->parent == NULL) for chip model info. */
		for (i = 0; i < 64; i++) {
			mtd = get_mtd_device(NULL, i);
			if (IS_ERR(mtd))
				continue;

			if (!sel) {
				sel = mtd;
			} else {
				if (mtd->parent) {
					put_mtd_device(mtd);
					continue;
				}

				/* Found master: replace current selection. */
				put_mtd_device(sel);
				sel = mtd;
				break;
			}

			if (!mtd->parent)
				break;
		}

		if (sel && !IS_ERR(sel)) {
			present = true;
			type = sel->type;
			model = failsafe_get_mtd_chip_model(sel, model_buf, sizeof(model_buf));
			put_mtd_device(sel);
		}

		len += snprintf(buf + len, left - len,
			"\"present\":%s,\"model\":\"%s\",\"type\":%d,",
			present ? "true" : "false",
			model ? model : "", type);

		len += snprintf(buf + len, left - len, "\"parts\":[");
		for (i = 0; i < 64 && len < left - 128; i++) {
			mtd = get_mtd_device(NULL, i);
			if (IS_ERR(mtd))
				continue;

			if (!mtd->name || !mtd->name[0]) {
				put_mtd_device(mtd);
				continue;
			}

			len += snprintf(buf + len, left - len,
				"%s{\"name\":\"%s\",\"size\":%llu,\"master\":%s}",
				first ? "" : ",",
				mtd->name,
				(unsigned long long)mtd->size,
				mtd->parent ? "false" : "true");

			first = false;
			put_mtd_device(mtd);
		}
		len += snprintf(buf + len, left - len, "]");
	}
#else
	len += snprintf(buf + len, left - len, "\"present\":false,\"parts\":[]");
#endif
	len += snprintf(buf + len, left - len, "}");
	len += snprintf(buf + len, left - len, "}");

	response->status = HTTP_RESP_STD;
	response->data = buf;
	response->size = strlen(buf);
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";

	/* response data must stay valid until sent */
	response->session_data = buf;
}

void backup_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	struct backup_session *st;
	struct httpd_form_value *mode, *storage, *target, *start, *end;
	char target_name[64] = "";
	char storage_sel[16] = "auto";
	u64 off_start = 0, off_end = 0;
	int ret;

	if (status == HTTP_CB_NEW) {
		mode = httpd_request_find_value(request, "mode");
		storage = httpd_request_find_value(request, "storage");
		target = httpd_request_find_value(request, "target");
		start = httpd_request_find_value(request, "start");
		end = httpd_request_find_value(request, "end");

		if (storage && storage->data)
			strlcpy(storage_sel, storage->data, sizeof(storage_sel));

		if (!mode || !mode->data || !target || !target->data)
			goto bad;

		strlcpy(target_name, target->data, sizeof(target_name));

		/* allow overriding storage by target prefix: mtd:<name> / mmc:<name> */
		if (!strncmp(target_name, "mtd:", 4)) {
			memmove(target_name, target_name + 4, strlen(target_name + 4) + 1);
			strlcpy(storage_sel, "mtd", sizeof(storage_sel));
		} else if (!strncmp(target_name, "mmc:", 4)) {
			memmove(target_name, target_name + 4, strlen(target_name + 4) + 1);
			strlcpy(storage_sel, "mmc", sizeof(storage_sel));
		}

		if (!strcmp(mode->data, "part")) {
			off_start = 0;
			off_end = ULLONG_MAX;
		} else if (!strcmp(mode->data, "range")) {
			if (!start || !end || !start->data || !end->data)
				goto bad;

			if (parse_u64_len(start->data, &off_start))
				goto bad;
			if (parse_u64_len(end->data, &off_end))
				goto bad;
		} else {
			goto bad;
		}

		st = calloc(1, sizeof(*st));
		if (!st)
			goto oom;

		st->buf_size = 64 * 1024;
		st->buf = malloc(st->buf_size);
		if (!st->buf) {
			free(st);
			goto oom;
		}

		/* open target and get size */
		if (!strcasecmp(storage_sel, "mtd") ||
		    (!strcasecmp(storage_sel, "auto") && mtd_part_exists(target_name))) {
#ifdef CONFIG_MTD
			gen_mtd_probe_devices();
			st->mtd = get_mtd_device_nm(target_name);
			if (IS_ERR(st->mtd)) {
				st->mtd = NULL;
				goto bad_target;
			}

			st->src = BACKUP_SRC_MTD;
			st->target_size = st->mtd->size;
#else
			goto bad_target;
#endif
		} else {
			/* MMC path */
#ifdef CONFIG_MTK_BOOTMENU_MMC
			st->mmc = _mmc_get_dev(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0, false);
			if (!st->mmc)
				goto bad_target;

			st->src = BACKUP_SRC_MMC;
			if (!strcmp(target_name, "raw")) {
				st->mmc_base = 0;
				st->target_size = st->mmc->capacity_user;
			} else {
				ret = _mmc_find_part(st->mmc, target_name, &st->dpart, true);
				if (ret)
					goto bad_target;

				st->mmc_base = (u64)st->dpart.start * st->dpart.blksz;
				st->target_size = (u64)st->dpart.size * st->dpart.blksz;
			}
#else
			goto bad_target;
#endif
		}

		/* range normalization */
		if (!strcmp(mode->data, "part")) {
			off_start = 0;
			off_end = st->target_size;
		}

		if (off_end == ULLONG_MAX)
			off_end = st->target_size;

		if (off_start >= off_end)
			goto bad_range;
		if (off_end > st->target_size)
			goto bad_range;

		st->start = off_start;
		st->end = off_end;
		st->total = st->end - st->start;
		st->cur = 0;
		st->phase = BACKUP_PHASE_HDR;

		/* filename */
		{
			char model[64] = "";
			const char *stype = st->src == BACKUP_SRC_MTD ? "mtd" : "mmc";

			if (st->src == BACKUP_SRC_MMC) {
#ifdef CONFIG_MTK_BOOTMENU_MMC
				struct blk_desc *bd = mmc_get_blk_desc(st->mmc);
				if (bd)
					strlcpy(model, bd->product, sizeof(model));
#endif
			} else {
#ifdef CONFIG_MTD
				if (st->mtd && st->mtd->name)
					strlcpy(model, st->mtd->name, sizeof(model));
#endif
			}

			str_sanitize_component(model);
			str_sanitize_component(target_name);

			snprintf(st->filename, sizeof(st->filename),
				"backup_%s_%s_%s_0x%llx-0x%llx.bin",
				stype,
				model[0] ? model : "device",
				target_name,
				(unsigned long long)st->start,
				(unsigned long long)st->end);
		}

		/* build HTTP header (CUSTOM response must include header) */
		st->hdr_len = snprintf(st->hdr, sizeof(st->hdr),
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: application/octet-stream\r\n"
			"Content-Length: %llu\r\n"
			"Content-Disposition: attachment; filename=\"%s\"\r\n"
			"Cache-Control: no-store\r\n"
			"Connection: close\r\n"
			"\r\n",
			(unsigned long long)st->total,
			st->filename);

		response->session_data = st;
		response->status = HTTP_RESP_CUSTOM;
		response->data = st->hdr;
		response->size = st->hdr_len;
		return;
	}

	if (status == HTTP_CB_RESPONDING) {
		u64 remain;
		size_t to_read, got = 0;

		st = response->session_data;
		if (!st) {
			response->status = HTTP_RESP_NONE;
			return;
		}

		if (st->phase == BACKUP_PHASE_HDR)
			st->phase = BACKUP_PHASE_DATA;

		remain = st->total - st->cur;
		if (!remain) {
			response->status = HTTP_RESP_NONE;
			return;
		}

		to_read = (size_t)min_t(u64, remain, st->buf_size);

		if (st->src == BACKUP_SRC_MTD) {
#ifdef CONFIG_MTD
			size_t readsz = 0;

			ret = mtd_read_skip_bad(st->mtd, st->start + st->cur,
					to_read,
					st->mtd->size - (st->start + st->cur),
					&readsz, st->buf);
			if (ret)
				goto io_err;

			got = readsz;
#else
			goto io_err;
#endif
		} else {
#ifdef CONFIG_MTK_BOOTMENU_MMC
			ret = mmc_read_generic(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0,
					st->mmc_base + st->start + st->cur,
					st->buf, to_read);
			if (ret)
				goto io_err;

			got = to_read;
#else
			goto io_err;
#endif
		}

		if (!got)
			goto io_err;

		st->cur += got;

		response->status = HTTP_RESP_CUSTOM;
		response->data = (const char *)st->buf;
		response->size = got;
		return;

	io_err:
		response->status = HTTP_RESP_NONE;
		return;
	}

	if (status == HTTP_CB_CLOSED) {
		st = response->session_data;
		if (st) {
#ifdef CONFIG_MTD
			if (st->mtd)
				put_mtd_device(st->mtd);
#endif
			free(st->buf);
			free(st);
		}
	}

	return;

bad:
	response->status = HTTP_RESP_STD;
	response->data = "bad request";
	response->size = strlen(response->data);
	response->info.code = 400;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
	return;

bad_target:
	response->status = HTTP_RESP_STD;
	response->data = "target not found";
	response->size = strlen(response->data);
	response->info.code = 404;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
#ifdef CONFIG_MTD
	if (st->mtd)
		put_mtd_device(st->mtd);
#endif
	free(st->buf);
	free(st);
	return;

bad_range:
	response->status = HTTP_RESP_STD;
	response->data = "invalid range";
	response->size = strlen(response->data);
	response->info.code = 400;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
#ifdef CONFIG_MTD
	if (st->mtd)
		put_mtd_device(st->mtd);
#endif
	free(st->buf);
	free(st);
	return;

oom:
	response->status = HTTP_RESP_STD;
	response->data = "no mem";
	response->size = strlen(response->data);
	response->info.code = 500;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
	return;
}

#define FLASH_EDIT_MAX_READ	4096
#define FLASH_EDIT_MAX_WRITE	(64 * 1024)

struct flash_target {
	enum backup_src src;
	u64 base;
	u64 size;
#ifdef CONFIG_MTD
	struct mtd_info *mtd;
#endif
#ifdef CONFIG_MTK_BOOTMENU_MMC
	struct mmc *mmc;
	struct disk_partition dpart;
#endif
};

static void flash_close_target(struct flash_target *t)
{
	if (!t)
		return;
#ifdef CONFIG_MTD
	if (t->mtd)
		put_mtd_device(t->mtd);
#endif
}

static int flash_open_target(const char *storage_sel, const char *target_name,
	struct flash_target *t)
{
	if (!storage_sel || !target_name || !t)
		return -EINVAL;

	memset(t, 0, sizeof(*t));

	if (!strcasecmp(storage_sel, "mtd") ||
	    (!strcasecmp(storage_sel, "auto") && mtd_part_exists(target_name))) {
#ifdef CONFIG_MTD
		gen_mtd_probe_devices();
		t->mtd = get_mtd_device_nm(target_name);
		if (IS_ERR(t->mtd)) {
			t->mtd = NULL;
			return -ENODEV;
		}

		t->src = BACKUP_SRC_MTD;
		t->base = 0;
		t->size = t->mtd->size;
		return 0;
#else
		return -ENODEV;
#endif
	}

#ifdef CONFIG_MTK_BOOTMENU_MMC
	t->mmc = _mmc_get_dev(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0, false);
	if (!t->mmc)
		return -ENODEV;

	t->src = BACKUP_SRC_MMC;
	if (!strcmp(target_name, "raw")) {
		t->base = 0;
		t->size = t->mmc->capacity_user;
		return 0;
	}

	if (_mmc_find_part(t->mmc, target_name, &t->dpart, true))
		return -ENODEV;

	t->base = (u64)t->dpart.start * t->dpart.blksz;
	t->size = (u64)t->dpart.size * t->dpart.blksz;
	return 0;
#else
	return -ENODEV;
#endif
}

static int flash_parse_start_end(const char *start_s, const char *end_s,
	u64 *start, u64 *end)
{
	if (!start_s || !end_s || !start || !end)
		return -EINVAL;

	if (parse_u64_len(start_s, start))
		return -EINVAL;
	if (parse_u64_len(end_s, end))
		return -EINVAL;
	if (*end <= *start)
		return -ERANGE;

	return 0;
}

static int flash_parse_hex(const char *in, u8 **out, size_t *out_len)
{
	size_t digits = 0, i = 0, o = 0, bytes;
	u8 *buf;
	int high = -1;

	if (!in || !out || !out_len)
		return -EINVAL;

	*out = NULL;
	*out_len = 0;

	while (in[i]) {
		if (in[i] == '0' && (in[i + 1] == 'x' || in[i + 1] == 'X')) {
			i += 2;
			continue;
		}
		if (isxdigit((unsigned char)in[i]))
			digits++;
		i++;
	}

	if (!digits || (digits & 1))
		return -EINVAL;

	bytes = digits / 2;
	if (bytes > FLASH_EDIT_MAX_WRITE)
		return -E2BIG;

	buf = malloc(bytes);
	if (!buf)
		return -ENOMEM;

	for (i = 0; in[i]; i++) {
		int v;

		if (in[i] == '0' && (in[i + 1] == 'x' || in[i + 1] == 'X')) {
			i++;
			high = -1;
			continue;
		}

		if (!isxdigit((unsigned char)in[i]))
			continue;

		if (in[i] >= '0' && in[i] <= '9')
			v = in[i] - '0';
		else if (in[i] >= 'a' && in[i] <= 'f')
			v = in[i] - 'a' + 10;
		else
			v = in[i] - 'A' + 10;

		if (high < 0) {
			high = v;
		} else {
			buf[o++] = (u8)((high << 4) | v);
			high = -1;
		}
	}

	if (o != bytes) {
		free(buf);
		return -EINVAL;
	}

	*out = buf;
	*out_len = bytes;
	return 0;
}

static char *flash_hex_dump(const u8 *data, size_t len, size_t *out_len)
{
	size_t i, cap;
	char *out;
	static const char hex[] = "0123456789abcdef";

	if (!data || !out_len)
		return NULL;

	cap = len * 3 + 8;
	out = malloc(cap);
	if (!out)
		return NULL;

	for (i = 0; i < len; i++) {
		out[i * 3] = hex[(data[i] >> 4) & 0xf];
		out[i * 3 + 1] = hex[data[i] & 0xf];
		out[i * 3 + 2] = (i + 1 == len) ? '\0' : ' ';
	}

	*out_len = strlen(out);
	return out;
}

#ifdef CONFIG_MTD
static int flash_mtd_update_range(struct mtd_info *mtd, u64 start,
	const u8 *data, size_t len)
{
	u64 block_start, block_end, blk;
	size_t erase_sz;
	u8 *blkbuf = NULL;
	int ret = 0;

	if (!mtd || !data || !len)
		return -EINVAL;

	erase_sz = mtd->erasesize;
	if (!erase_sz)
		return -EINVAL;

	block_start = start & ~((u64)erase_sz - 1);
	block_end = (start + len + erase_sz - 1) & ~((u64)erase_sz - 1);

	blkbuf = malloc(erase_sz);
	if (!blkbuf)
		return -ENOMEM;

	for (blk = block_start; blk < block_end; blk += erase_sz) {
		size_t readsz = 0;
		u64 data_start, data_end;
		size_t copy_len;

		ret = mtd_read_skip_bad(mtd, blk, erase_sz, erase_sz, &readsz, blkbuf);
		if (ret || readsz != erase_sz) {
			ret = ret ? ret : -EIO;
			goto out;
		}

		data_start = max(start, blk);
		data_end = min(start + (u64)len, blk + (u64)erase_sz);
		copy_len = (size_t)(data_end - data_start);
		if (copy_len) {
			memcpy(blkbuf + (data_start - blk),
				data + (size_t)(data_start - start),
				copy_len);
		}

		ret = mtd_erase_skip_bad(mtd, blk, erase_sz, erase_sz,
			NULL, NULL, mtd->name, true);
		if (ret)
			goto out;

		ret = mtd_write_skip_bad(mtd, blk, erase_sz, erase_sz,
			NULL, blkbuf, true);
		if (ret)
			goto out;
	}

out:
	free(blkbuf);
	return ret;
}

static int flash_mtd_restore_range(struct mtd_info *mtd, u64 start,
	const u8 *data, size_t len)
{
	u64 erased = 0;
	size_t written = 0;
	int ret;

	if (!mtd || !data || !len)
		return -EINVAL;

	ret = mtd_erase_skip_bad(mtd, start, len, mtd->size - start,
		&erased, NULL, mtd->name, true);
	if (ret)
		return ret;

	ret = mtd_write_skip_bad(mtd, start, len, mtd->size - start,
		&written, data, true);
	if (ret)
		return ret;

	if (written != len)
		return -EIO;

	return 0;
}

static int flash_mtd_erase_range(struct mtd_info *mtd, u64 start, u64 len)
{
	u64 block_start, block_end, blk;
	size_t erase_sz;
	u8 *blkbuf = NULL;
	int ret = 0;

	if (!mtd || !len)
		return -EINVAL;

	erase_sz = mtd->erasesize;
	if (!erase_sz)
		return -EINVAL;

	block_start = start & ~((u64)erase_sz - 1);
	block_end = (start + len + erase_sz - 1) & ~((u64)erase_sz - 1);

	for (blk = block_start; blk < block_end; blk += erase_sz) {
		u64 data_start = max(start, blk);
		u64 data_end = min(start + (u64)len, blk + (u64)erase_sz);
		bool full_block = (data_start == blk) && (data_end == blk + (u64)erase_sz);

		if (full_block) {
			ret = mtd_erase_skip_bad(mtd, blk, erase_sz, erase_sz,
				NULL, NULL, mtd->name, true);
			if (ret)
				goto out;
			continue;
		}

		if (!blkbuf) {
			blkbuf = malloc(erase_sz);
			if (!blkbuf) {
				ret = -ENOMEM;
				goto out;
			}
		}

		{
			size_t readsz = 0;
			size_t fill_len = (size_t)(data_end - data_start);

			ret = mtd_read_skip_bad(mtd, blk, erase_sz, erase_sz, &readsz, blkbuf);
			if (ret || readsz != erase_sz) {
				ret = ret ? ret : -EIO;
				goto out;
			}

			memset(blkbuf + (size_t)(data_start - blk), 0xff, fill_len);

			ret = mtd_erase_skip_bad(mtd, blk, erase_sz, erase_sz,
				NULL, NULL, mtd->name, true);
			if (ret)
				goto out;

			ret = mtd_write_skip_bad(mtd, blk, erase_sz, erase_sz,
				NULL, blkbuf, true);
			if (ret)
				goto out;
		}
	}

out:
	free(blkbuf);
	return ret;
}
#endif

static const char *flash_find_last_before(const char *s, const char *needle,
	const char *limit)
{
	const char *p = s;
	const char *last = NULL;

	if (!s || !needle || !limit || limit <= s)
		return NULL;

	while ((p = strstr(p, needle)) != NULL) {
		if (p >= limit)
			break;
		last = p;
		p++;
	}

	return last;
}

static int flash_parse_backup_filename(const char *filename,
	char *storage, size_t storage_sz,
	char *target, size_t target_sz,
	u64 *start, u64 *end)
{
	const char *range, *dash, *stype_mtd, *stype_mmc, *stype;
	char *range_end = NULL;
	char tmp[128];
	size_t seg_len;

	if (!filename || !storage || !target || !start || !end)
		return -EINVAL;

	range = strstr(filename, "_0x");
	if (!range)
		return -EINVAL;
	
	dash = strstr(range, "-0x");
	if (!dash)
		return -EINVAL;

	*start = simple_strtoull(range + 1, &range_end, 0);
	if (!range_end || range_end <= range + 1)
		return -EINVAL;

	*end = simple_strtoull(dash + 1, &range_end, 0);
	if (!range_end || range_end <= dash + 1)
		return -EINVAL;

	if (*end <= *start)
		return -ERANGE;

	stype_mtd = flash_find_last_before(filename, "_mtd_", range);
	stype_mmc = flash_find_last_before(filename, "_mmc_", range);
	if (stype_mtd && stype_mmc)
		stype = (stype_mtd > stype_mmc) ? stype_mtd : stype_mmc;
	else
		stype = stype_mtd ? stype_mtd : stype_mmc;

	if (!stype)
		return -EINVAL;

	if (stype == stype_mtd)
		strlcpy(storage, "mtd", storage_sz);
	else
		strlcpy(storage, "mmc", storage_sz);

	stype += (stype == stype_mtd) ? 5 : 5;
	seg_len = (size_t)(range - stype);
	if (!seg_len || seg_len >= sizeof(tmp))
		return -EINVAL;

	memcpy(tmp, stype, seg_len);
	tmp[seg_len] = '\0';

	{
		char *last = strrchr(tmp, '_');
		const char *name = last ? last + 1 : tmp;
		if (!name || !name[0])
			return -EINVAL;
		if (strlen(name) >= target_sz)
			return -E2BIG;
		strlcpy(target, name, target_sz);
	}

	return 0;
}

static void flash_reply_json(struct httpd_response *response, int code,
	const char *json, char *alloc)
{
	response->status = HTTP_RESP_STD;
	response->data = json;
	response->size = strlen(json);
	response->info.code = code;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";
	response->session_data = alloc;
}

static const char *flash_detect_op(struct httpd_request *request)
{
	const char *uri;

	if (!request || !request->urih || !request->urih->uri)
		return NULL;

	uri = request->urih->uri;
	if (!strcmp(uri, "/flash/read"))
		return "read";
	if (!strcmp(uri, "/flash/write"))
		return "write";
	if (!strcmp(uri, "/flash/restore"))
		return "restore";
	if (!strcmp(uri, "/flash/erase"))
		return "erase";

	return NULL;
}

void flash_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	struct httpd_form_value *opv;
	const char *op = NULL;
	char *json = NULL;
	char storage_sel[16] = "auto";
	char target_name[64] = "";
	u64 start = 0, end = 0;
	int ret;

	if (status == HTTP_CB_CLOSED) {
		free(response->session_data);
		response->session_data = NULL;
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_POST) {
		flash_reply_json(response, 405,
			"{\"ok\":false,\"error\":\"method\"}\n", NULL);
		return;
	}

	opv = httpd_request_find_value(request, "op");
	if (opv && opv->data)
		op = opv->data;
	if (!op)
		op = flash_detect_op(request);
	if (!op) {
		flash_reply_json(response, 400,
			"{\"ok\":false,\"error\":\"no_op\"}\n", NULL);
		return;
	}

	if (!strcmp(op, "read")) {
		struct httpd_form_value *storage, *target, *startv, *endv;
		struct flash_target tgt;
		u8 *buf = NULL;
		char *hex = NULL;
		size_t len, hex_len = 0;

		storage = httpd_request_find_value(request, "storage");
		target = httpd_request_find_value(request, "target");
		startv = httpd_request_find_value(request, "start");
		endv = httpd_request_find_value(request, "end");

		if (storage && storage->data)
			strlcpy(storage_sel, storage->data, sizeof(storage_sel));
		if (target && target->data)
			strlcpy(target_name, target->data, sizeof(target_name));

		if (!target_name[0] || !startv || !endv || !startv->data || !endv->data)
			goto bad_req;

		if (!strncmp(target_name, "mtd:", 4)) {
			memmove(target_name, target_name + 4, strlen(target_name + 4) + 1);
			strlcpy(storage_sel, "mtd", sizeof(storage_sel));
		} else if (!strncmp(target_name, "mmc:", 4)) {
			memmove(target_name, target_name + 4, strlen(target_name + 4) + 1);
			strlcpy(storage_sel, "mmc", sizeof(storage_sel));
		}

		ret = flash_parse_start_end(startv->data, endv->data, &start, &end);
		if (ret)
			goto bad_range;

		len = (size_t)(end - start);
		if (!len || len > FLASH_EDIT_MAX_READ)
			goto too_large;

		ret = flash_open_target(storage_sel, target_name, &tgt);
		if (ret)
			goto bad_target;

		if (end > tgt.size) {
			flash_close_target(&tgt);
			goto bad_range;
		}

		buf = malloc(len);
		if (!buf) {
			flash_close_target(&tgt);
			goto oom;
		}

		if (tgt.src == BACKUP_SRC_MTD) {
#ifdef CONFIG_MTD
			size_t readsz = 0;
			ret = mtd_read_skip_bad(tgt.mtd, start, len,
				tgt.mtd->size - start, &readsz, buf);
			if (ret || readsz != len) {
				free(buf);
				flash_close_target(&tgt);
				goto io_err;
			}
#else
			free(buf);
			flash_close_target(&tgt);
			goto bad_target;
#endif
		} else {
#ifdef CONFIG_MTK_BOOTMENU_MMC
			ret = mmc_read_generic(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0,
				tgt.base + start, buf, len);
			if (ret) {
				free(buf);
				flash_close_target(&tgt);
				goto io_err;
			}
#else
			free(buf);
			flash_close_target(&tgt);
			goto bad_target;
#endif
		}

		hex = flash_hex_dump(buf, len, &hex_len);
		free(buf);
		flash_close_target(&tgt);
		if (!hex)
			goto oom;

		json = malloc(hex_len + 160);
		if (!json) {
			free(hex);
			goto oom;
		}

		snprintf(json, hex_len + 160,
			"{\"ok\":true,\"start\":\"0x%llx\",\"end\":\"0x%llx\",\"size\":%zu,\"data\":\"%s\"}\n",
			(unsigned long long)start,
			(unsigned long long)end,
			len, hex);
		free(hex);

		flash_reply_json(response, 200, json, json);
		return;
	}

	if (!strcmp(op, "write")) {
		struct httpd_form_value *storage, *target, *startv, *datav;
		struct flash_target tgt;
		u8 *buf = NULL;
		size_t len = 0;

		storage = httpd_request_find_value(request, "storage");
		target = httpd_request_find_value(request, "target");
		startv = httpd_request_find_value(request, "start");
		datav = httpd_request_find_value(request, "data");

		if (storage && storage->data)
			strlcpy(storage_sel, storage->data, sizeof(storage_sel));
		if (target && target->data)
			strlcpy(target_name, target->data, sizeof(target_name));

		if (!target_name[0] || !startv || !startv->data || !datav || !datav->data)
			goto bad_req;

		if (!strncmp(target_name, "mtd:", 4)) {
			memmove(target_name, target_name + 4, strlen(target_name + 4) + 1);
			strlcpy(storage_sel, "mtd", sizeof(storage_sel));
		} else if (!strncmp(target_name, "mmc:", 4)) {
			memmove(target_name, target_name + 4, strlen(target_name + 4) + 1);
			strlcpy(storage_sel, "mmc", sizeof(storage_sel));
		}

		if (parse_u64_len(startv->data, &start))
			goto bad_range;

		ret = flash_parse_hex(datav->data, &buf, &len);
		if (ret)
			goto bad_hex;

		ret = flash_open_target(storage_sel, target_name, &tgt);
		if (ret) {
			free(buf);
			goto bad_target;
		}

		if (start + len > tgt.size) {
			flash_close_target(&tgt);
			free(buf);
			goto bad_range;
		}

		if (tgt.src == BACKUP_SRC_MTD) {
#ifdef CONFIG_MTD
			ret = flash_mtd_update_range(tgt.mtd, start, buf, len);
#else
			ret = -ENODEV;
#endif
		} else {
#ifdef CONFIG_MTK_BOOTMENU_MMC
			ret = mmc_write_generic(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0,
				tgt.base + start, tgt.size - start, buf, len, true);
#else
			ret = -ENODEV;
#endif
		}

		flash_close_target(&tgt);
		free(buf);

		if (ret)
			goto io_err;

		json = malloc(96);
		if (!json)
			goto oom;
		snprintf(json, 96, "{\"ok\":true,\"written\":%zu}\n", len);
		flash_reply_json(response, 200, json, json);
		return;
	}

	if (!strcmp(op, "restore")) {
		struct httpd_form_value *fw, *storage, *target, *startv, *endv;
		struct flash_target tgt;
		char storage_from_name[16] = "";
		char target_from_name[64] = "";
		u64 name_start = 0, name_end = 0;
		size_t len = 0;

		fw = httpd_request_find_value(request, "backup");
		if (!fw)
			fw = httpd_request_find_value(request, "file");

		if (!fw || !fw->data || !fw->size)
			goto bad_req;

		ret = fw->filename ? flash_parse_backup_filename(fw->filename,
			storage_from_name, sizeof(storage_from_name),
			target_from_name, sizeof(target_from_name),
			&name_start, &name_end) : -EINVAL;

		if (!ret) {
			strlcpy(storage_sel, storage_from_name, sizeof(storage_sel));
			strlcpy(target_name, target_from_name, sizeof(target_name));
			start = name_start;
			end = name_end;
		} else {
			storage = httpd_request_find_value(request, "storage");
			target = httpd_request_find_value(request, "target");
			startv = httpd_request_find_value(request, "start");
			endv = httpd_request_find_value(request, "end");

			if (storage && storage->data)
				strlcpy(storage_sel, storage->data, sizeof(storage_sel));
			if (target && target->data)
				strlcpy(target_name, target->data, sizeof(target_name));

			if (!target_name[0] || !startv || !endv || !startv->data || !endv->data)
				goto bad_req;

			if (flash_parse_start_end(startv->data, endv->data, &start, &end))
				goto bad_range;
		}

		len = fw->size;
		if (end <= start || (u64)len != (end - start))
			goto bad_range;

		if (!strncmp(target_name, "mtd:", 4)) {
			memmove(target_name, target_name + 4, strlen(target_name + 4) + 1);
			strlcpy(storage_sel, "mtd", sizeof(storage_sel));
		} else if (!strncmp(target_name, "mmc:", 4)) {
			memmove(target_name, target_name + 4, strlen(target_name + 4) + 1);
			strlcpy(storage_sel, "mmc", sizeof(storage_sel));
		}

		ret = flash_open_target(storage_sel, target_name, &tgt);
		if (ret)
			goto bad_target;

		if (end > tgt.size) {
			flash_close_target(&tgt);
			goto bad_range;
		}

		if (tgt.src == BACKUP_SRC_MTD) {
#ifdef CONFIG_MTD
			ret = flash_mtd_restore_range(tgt.mtd, start, fw->data, len);
#else
			ret = -ENODEV;
#endif
		} else {
#ifdef CONFIG_MTK_BOOTMENU_MMC
			ret = mmc_write_generic(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0,
				tgt.base + start, tgt.size - start, fw->data, len, true);
#else
			ret = -ENODEV;
#endif
		}

		flash_close_target(&tgt);

		if (ret)
			goto io_err;

		json = malloc(96);
		if (!json)
			goto oom;
		snprintf(json, 96, "{\"ok\":true,\"restored\":%zu}\n", len);
		flash_reply_json(response, 200, json, json);
		return;
	}

	if (!strcmp(op, "erase")) {
		struct httpd_form_value *storage, *target, *startv, *endv;
		struct flash_target tgt;
		u64 len;

		storage = httpd_request_find_value(request, "storage");
		target = httpd_request_find_value(request, "target");
		startv = httpd_request_find_value(request, "start");
		endv = httpd_request_find_value(request, "end");

		if (storage && storage->data)
			strlcpy(storage_sel, storage->data, sizeof(storage_sel));
		if (target && target->data)
			strlcpy(target_name, target->data, sizeof(target_name));

		if (!target_name[0])
			goto bad_req;

		if (!strncmp(target_name, "mtd:", 4)) {
			memmove(target_name, target_name + 4, strlen(target_name + 4) + 1);
			strlcpy(storage_sel, "mtd", sizeof(storage_sel));
		} else if (!strncmp(target_name, "mmc:", 4)) {
			memmove(target_name, target_name + 4, strlen(target_name + 4) + 1);
			strlcpy(storage_sel, "mmc", sizeof(storage_sel));
		}

		ret = flash_open_target(storage_sel, target_name, &tgt);
		if (ret)
			goto bad_target;

		if (startv && endv && startv->data && startv->data[0] &&
		    endv && endv->data && endv->data[0]) {
			if (flash_parse_start_end(startv->data, endv->data, &start, &end)) {
				flash_close_target(&tgt);
				goto bad_range;
			}
		} else if ((!startv || !startv->data || !startv->data[0]) &&
			   (!endv || !endv->data || !endv->data[0])) {
			start = 0;
			end = tgt.size;
		} else {
			flash_close_target(&tgt);
			goto bad_range;
		}

		if (start >= end || end > tgt.size) {
			flash_close_target(&tgt);
			goto bad_range;
		}

		len = end - start;

		if (tgt.src == BACKUP_SRC_MTD) {
#ifdef CONFIG_MTD
			ret = flash_mtd_erase_range(tgt.mtd, start, len);
#else
			ret = -ENODEV;
#endif
		} else {
#ifdef CONFIG_MTK_BOOTMENU_MMC
			ret = mmc_erase_generic(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0,
				tgt.base + start, len);
#else
			ret = -ENODEV;
#endif
		}

		flash_close_target(&tgt);

		if (ret)
			goto io_err;

		json = malloc(160);
		if (!json)
			goto oom;
		snprintf(json, 160,
			"{\"ok\":true,\"erased\":%llu,\"start\":\"0x%llx\",\"end\":\"0x%llx\"}\n",
			(unsigned long long)len,
			(unsigned long long)start,
			(unsigned long long)end);
		flash_reply_json(response, 200, json, json);
		return;
	}

	flash_reply_json(response, 400,
		"{\"ok\":false,\"error\":\"unknown_op\"}\n", NULL);
	return;

bad_req:
	flash_reply_json(response, 400,
		"{\"ok\":false,\"error\":\"bad_request\"}\n", NULL);
	return;
bad_target:
	flash_reply_json(response, 404,
		"{\"ok\":false,\"error\":\"target_not_found\"}\n", NULL);
	return;
bad_range:
	flash_reply_json(response, 400,
		"{\"ok\":false,\"error\":\"bad_range\"}\n", NULL);
	return;
bad_hex:
	flash_reply_json(response, 400,
		"{\"ok\":false,\"error\":\"bad_hex\"}\n", NULL);
	return;
too_large:
	flash_reply_json(response, 413,
		"{\"ok\":false,\"error\":\"too_large\"}\n", NULL);
	return;
io_err:
	flash_reply_json(response, 500,
		"{\"ok\":false,\"error\":\"io\"}\n", NULL);
	return;
oom:
	flash_reply_json(response, 500,
		"{\"ok\":false,\"error\":\"oom\"}\n", NULL);
	return;
}
