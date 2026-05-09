/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 *
 * Failsafe Web UI
 */

#include <command.h>
#include <errno.h>
#include <env.h>
#include <malloc.h>
#include <net.h>
#include <net/mtk_tcp.h>
#include <net/mtk_httpd.h>
#include <net/mtk_dhcpd.h>
#include <u-boot/md5.h>
#include <linux/stringify.h>
#include <linux/string.h>
#include <dm/ofnode.h>
#include <vsprintf.h>
#include <version_string.h>
#include <failsafe/fw_type.h>
#include "../board/mediatek/common/boot_helper.h"
#include "fs.h"
#include "failsafe_internal.h"

#ifdef CONFIG_MTD
#include "../board/mediatek/common/mtd_helper.h"
#endif
#ifdef CONFIG_MTK_BOOTMENU_MMC
#include "../board/mediatek/common/mmc_helper.h"
#endif
#ifdef CONFIG_PARTITIONS
#include <part.h>
#endif

static u32 upload_data_id;
static const void *upload_data;
static size_t upload_size;
static bool upgrade_success;
static bool auto_action_pending;
static failsafe_fw_t fw_type;
static bool failsafe_httpd_running;

#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
static const char *mtd_layout_label;
const char *get_mtd_layout_label(void);
#define MTD_LAYOUTS_MAXLEN	128
#endif

int __weak failsafe_validate_image(const void *data, size_t size, failsafe_fw_t fw)
{
	return 0;
}

int __weak failsafe_write_image(const void *data, size_t size, failsafe_fw_t fw)
{
	return -ENOSYS;
}

void schedule_hook(void)
{
	if (!failsafe_httpd_running)
		return;

#if defined(CONFIG_MTK_TCP)
	eth_rx();
	mtk_tcp_periodic_check();
#endif
}

struct reboot_session {
	bool do_reboot;
};

#ifdef CONFIG_MTK_BOOTMENU_MMC
static bool failsafe_mmc_present(void)
{
	struct mmc *mmc;
	struct blk_desc *bd;

	mmc = _mmc_get_dev(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0, false);
	bd = mmc ? mmc_get_blk_desc(mmc) : NULL;

	return mmc && bd && bd->type != DEV_TYPE_UNKNOWN;
}
#endif

size_t json_escape(char *dst, size_t dst_sz, const char *src)
{
	size_t di = 0;
	const unsigned char *s = (const unsigned char *)src;

	if (!dst || !dst_sz)
		return 0;

	if (!src)
	{
		dst[0] = '\0';
		return 0;
	}

	while (*s && di + 2 < dst_sz)
	{
		unsigned char c = *s++;

		if (c == '"' || c == '\\')
		{
			if (di + 2 >= dst_sz)
				break;
			dst[di++] = '\\';
			dst[di++] = (char)c;
			continue;
		}

		if (c == '\n' || c == '\r' || c == '\t')
		{
			if (di + 2 >= dst_sz)
				break;
			dst[di++] = '\\';
			dst[di++] = (c == '\n') ? 'n' : (c == '\r') ? 'r' : 't';
			continue;
		}

		if (c < 0x20)
		{
			/* skip other control chars */
			dst[di++] = ' ';
			continue;
		}

		dst[di++] = (char)c;
	}

	dst[di] = '\0';
	return di;
}

static bool failsafe_auto_reboot_enabled(void)
{
	const char *val = env_get("failsafe_auto_reboot");

	if (!val || !val[0])
		return IS_ENABLED(CONFIG_WEBUI_FAILSAFE_UI_GL) ||
		       IS_ENABLED(CONFIG_WEBUI_FAILSAFE_UI_MTK);

	if (!strcmp(val, "1") || !strcasecmp(val, "true") ||
	    !strcasecmp(val, "yes") || !strcasecmp(val, "on"))
		return true;

	return false;
}

static int output_plain_file(struct httpd_response *response,
			     const char *filename)
{
	const struct fs_desc *file;
	int ret = 0;

	file = fs_find_file(filename);

	response->status = HTTP_RESP_STD;

	if (file) {
		response->data = file->data;
		response->size = file->size;
	} else {
		response->data = "Error: file not found";
		response->size = strlen(response->data);
		ret = 1;
	}

	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/html";

	return ret;
}

#ifndef WEBUI_FAILSAFE_GIT_HASH
#define WEBUI_FAILSAFE_GIT_HASH "unknown"
#endif

#ifndef WEBUI_FAILSAFE_GIT_DIRTY
#define WEBUI_FAILSAFE_GIT_DIRTY 0
#endif

static void version_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	const char *build_variant;
	const char *git_hash = WEBUI_FAILSAFE_GIT_HASH;
	static char version_buf[512];
	bool dirty = !!WEBUI_FAILSAFE_GIT_DIRTY;

	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;

	build_variant = CONFIG_WEBUI_FAILSAFE_BUILD_VARIANT;
	if (!git_hash || !git_hash[0])
		git_hash = "unknown";

	if (build_variant && build_variant[0]) {
		snprintf(version_buf, sizeof(version_buf),
			 "%s %s%s %s",
			 version_string, git_hash, dirty ? "-dirty" : "",
			 build_variant);
		response->data = version_buf;
	} else {
		snprintf(version_buf, sizeof(version_buf),
			 "%s %s%s",
			 version_string, git_hash, dirty ? "-dirty" : "");
		response->data = version_buf;
	}
	response->size = strlen(response->data);

	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
}

static void sysinfo_handler(enum httpd_uri_handler_status status,
							struct httpd_request *request,
							struct httpd_response *response)
{
	char *buf;
	int len = 0;
	int left = 8192;
	off_t ram_size = 0;
	ofnode root;
	const char *board_model = NULL;
	const char *board_compat = NULL;
	const char *build_variant = NULL;
	char esc_board_model[256], esc_board_compat[256], esc_build_variant[256];

	(void)request;

	if (status == HTTP_CB_CLOSED)
	{
		free(response->session_data);
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	buf = malloc(left);
	if (!buf)
	{
		response->status = HTTP_RESP_STD;
		response->data = "{}";
		response->size = strlen(response->data);
		response->info.code = 500;
		response->info.connection_close = 1;
		response->info.content_type = "application/json";
		return;
	}

	root = ofnode_path("/");
	if (ofnode_valid(root))
	{
		board_model = ofnode_read_string(root, "model");
		board_compat = ofnode_read_string(root, "compatible");
	}

	if (!board_model || !board_model[0])
	{
		board_model = env_get("model");
		if (!board_model || !board_model[0])
			board_model = env_get("board_name");
		if (!board_model || !board_model[0])
			board_model = env_get("board");
	}

	/* RAM size from global data */
	if (gd)
		ram_size = (off_t)gd->ram_size;

	build_variant = CONFIG_WEBUI_FAILSAFE_BUILD_VARIANT;
	if (build_variant && !build_variant[0])
		build_variant = NULL;

	json_escape(esc_board_model, sizeof(esc_board_model), board_model ? board_model : "");
	json_escape(esc_board_compat, sizeof(esc_board_compat), board_compat ? board_compat : "");
	json_escape(esc_build_variant, sizeof(esc_build_variant), build_variant ? build_variant : "");

	len += snprintf(buf + len, left - len, "{");
	len += snprintf(buf + len, left - len,
					"\"board\":{\"model\":\"%s\",\"compatible\":\"%s\"},",
					esc_board_model, esc_board_compat);
	len += snprintf(buf + len, left - len,
					"\"ram\":{\"size\":%llu},",
					(unsigned long long)ram_size);
	len += snprintf(buf + len, left - len,
					"\"build_variant\":\"%s\",",
					esc_build_variant);

	len += snprintf(buf + len, left - len, "\"storage\":{");

#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
	{
		const char *cur = get_mtd_layout_label();
		char esc_cur[128];
		const char *cur_parts = NULL;
		char esc_cur_parts[512];
		ofnode node, layout;
		bool first = true;

		json_escape(esc_cur, sizeof(esc_cur), cur ? cur : "");
		len += snprintf(buf + len, left - len,
				"\"mtd_layout\":{\"current\":\"%s\",",
				esc_cur);

		node = ofnode_path("/mtd-layout");
		if (ofnode_valid(node) && ofnode_get_child_count(node)) {
			len += snprintf(buf + len, left - len, "\"layouts\":[");
			ofnode_for_each_subnode(layout, node) {
				const char *label = ofnode_read_string(layout, "label");
				const char *parts = ofnode_read_string(layout, "mtdparts");
				char esc_label[128];
				char esc_parts[512];

				if (!label)
					continue;
				json_escape(esc_label, sizeof(esc_label), label);
				json_escape(esc_parts, sizeof(esc_parts), parts ? parts : "");
				if (cur && !strcmp(label, cur))
					cur_parts = parts;
				len += snprintf(buf + len, left - len,
					"%s{\"label\":\"%s\",\"parts\":\"%s\"}",
					first ? "" : ",", esc_label, esc_parts);
				first = false;
			}
			len += snprintf(buf + len, left - len, "],");
		} else {
			len += snprintf(buf + len, left - len, "\"layouts\":[],");
		}

		json_escape(esc_cur_parts, sizeof(esc_cur_parts), cur_parts ? cur_parts : "");
		len += snprintf(buf + len, left - len,
				"\"current_parts\":\"%s\"},",
				esc_cur_parts);
	}
#else
	len += snprintf(buf + len, left - len, "\"mtd_layout\":null,");
#endif

	len += snprintf(buf + len, left - len, "\"mmc\":{");
#ifdef CONFIG_MTK_BOOTMENU_MMC
	{
		struct mmc *mmc;
		struct blk_desc *bd;
		bool present;
		char esc_vendor[128], esc_product[128];

		mmc = _mmc_get_dev(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0, false);
		bd = mmc ? mmc_get_blk_desc(mmc) : NULL;
		present = mmc && bd && bd->type != DEV_TYPE_UNKNOWN;

		if (present) {
			json_escape(esc_vendor, sizeof(esc_vendor), bd->vendor ? bd->vendor : "");
			json_escape(esc_product, sizeof(esc_product), bd->product ? bd->product : "");
			len += snprintf(buf + len, left - len,
				"\"present\":true,\"vendor\":\"%s\",\"product\":\"%s\",\"blksz\":%lu,\"size\":%llu,",
				esc_vendor, esc_product, (unsigned long)bd->blksz,
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
					"%s{\"name\":\"%s\",\"start\":%llu,\"size\":%llu}",
					first ? "" : ",",
					dpart.name,
					(unsigned long long)dpart.start * dpart.blksz,
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
	len += snprintf(buf + len, left - len, "}");

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

static void reboot_handler(enum httpd_uri_handler_status status,
			   struct httpd_request *request,
			   struct httpd_response *response)
{
	struct reboot_session *st;

	if (status == HTTP_CB_NEW) {
		st = calloc(1, sizeof(*st));
		if (!st) {
			response->info.code = 500;
			return;
		}

		st->do_reboot = true;

		response->session_data = st;
		response->status = HTTP_RESP_STD;
		response->data = "rebooting";
		response->size = strlen(response->data);
		response->info.code = 200;
		response->info.connection_close = 1;
		response->info.content_type = "text/plain";
		return;
	}

	if (status == HTTP_CB_CLOSED) {
		bool do_reboot = false;

		st = response->session_data;
		if (st)
			do_reboot = st->do_reboot;
		free(st);

		if (do_reboot) {
			/* Make sure the current HTTP session has fully closed before reset */
			mtk_tcp_close_all_conn();
			do_reset(NULL, 0, 0, NULL);
		}
	}
}

static void reboot_failsafe_handler(enum httpd_uri_handler_status status,
				   struct httpd_request *request,
				   struct httpd_response *response)
{
	struct reboot_session *st;
	int ret;

	if (status == HTTP_CB_NEW) {
		ret = env_set("failsafe", "1");
		if (!ret)
			ret = env_save();

		if (ret) {
			response->status = HTTP_RESP_STD;
			response->data = "failsafe env set failed";
			response->size = strlen(response->data);
			response->info.code = 500;
			response->info.connection_close = 1;
			response->info.content_type = "text/plain";
			return;
		}

		st = calloc(1, sizeof(*st));
		if (!st) {
			response->info.code = 500;
			return;
		}

		st->do_reboot = true;
		response->session_data = st;
		response->status = HTTP_RESP_STD;
		response->data = "rebooting to failsafe";
		response->size = strlen(response->data);
		response->info.code = 200;
		response->info.connection_close = 1;
		response->info.content_type = "text/plain";
		return;
	}

	if (status == HTTP_CB_CLOSED) {
		bool do_reboot = false;

		st = response->session_data;
		if (st)
			do_reboot = st->do_reboot;
		free(st);

		if (do_reboot) {
			mtk_tcp_close_all_conn();
			do_reset(NULL, 0, 0, NULL);
		}
	}
}

static void index_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	if (status == HTTP_CB_NEW)
		output_plain_file(response, "index.html");
}

static void upload_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	static char md5_str[33] = "";
	static char resp[128];
	struct httpd_form_value *fw;
#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
	struct httpd_form_value *mtd = NULL;
#endif
	u8 md5_sum[16];
	int i;

	static char hexchars[] = "0123456789abcdef";

	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";

#ifdef CONFIG_MTK_BOOTMENU_MMC
	fw = httpd_request_find_value(request, "gpt");
	if (fw) {
		fw_type = FW_TYPE_GPT;
		goto done;
	}
#endif

	fw = httpd_request_find_value(request, "fip");
	if (fw) {
		fw_type = FW_TYPE_FIP;
		if (failsafe_validate_image(fw->data, fw->size, fw_type))
			goto fail;
		goto done;
	}

	fw = httpd_request_find_value(request, "bl2");
	if (fw) {
		fw_type = FW_TYPE_BL2;
		if (failsafe_validate_image(fw->data, fw->size, fw_type))
			goto fail;
		goto done;
	}

	fw = httpd_request_find_value(request, "firmware");
	if (fw) {
		fw_type = FW_TYPE_FW;
		if (failsafe_validate_image(fw->data, fw->size, fw_type))
			goto fail;
#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
		mtd = httpd_request_find_value(request, "mtd_layout");
#endif
		goto done;
	}

#ifdef CONFIG_WEBUI_FAILSAFE_SIMG
	fw = httpd_request_find_value(request, "simg");
	if (fw) {
		fw_type = FW_TYPE_SIMG;
		if (failsafe_validate_image(fw->data, fw->size, fw_type))
			goto fail;
		goto done;
	}
#endif

#ifdef CONFIG_WEBUI_FAILSAFE_FACTORY
	fw = httpd_request_find_value(request, "factory");
	if (fw) {
		fw_type = FW_TYPE_FACTORY;
		if (failsafe_validate_image(fw->data, fw->size, fw_type))
			goto fail;
		goto done;
	}
#endif

	fw = httpd_request_find_value(request, "initramfs");
	if (fw) {
		fw_type = FW_TYPE_INITRD;
		if (fdt_check_header(fw->data))
			goto fail;
		goto done;
	}

fail:
	response->data = "fail";
	response->size = strlen(response->data);
	return;

done:
	upload_data_id = upload_id;
	upload_data = fw->data;
	upload_size = fw->size;

	md5_wd((u8 *)fw->data, fw->size, md5_sum, 0);
	for (i = 0; i < 16; i++) {
		u8 hex = (md5_sum[i] >> 4) & 0xf;
		md5_str[i * 2] = hexchars[hex];
		hex = md5_sum[i] & 0xf;
		md5_str[i * 2 + 1] = hexchars[hex];
	}

#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
	if (mtd) {
		mtd_layout_label = mtd->data;
		sprintf(resp, "%ld %s %s", fw->size, md5_str, mtd->data);
	} else {
		sprintf(resp, "%ld %s", fw->size, md5_str);
	}
#else
	sprintf(resp, "%ld %s", fw->size, md5_str);
#endif

	response->data = resp;
	response->size = strlen(response->data);

	return;

}

struct flashing_status {
	char buf[4096];
	int ret;
	int body_sent;
};

static void result_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	struct flashing_status *st;
	u32 size;

	if (status == HTTP_CB_NEW) {
		st = calloc(1, sizeof(*st));
		if (!st) {
			response->info.code = 500;
			return;
		}

		st->ret = -1;

		response->session_data = st;

		response->status = HTTP_RESP_CUSTOM;

		response->info.http_1_0 = 1;
		response->info.content_length = -1;
		response->info.connection_close = 1;
		response->info.content_type = "text/html";
		response->info.code = 200;

		size = http_make_response_header(&response->info,
			st->buf, sizeof(st->buf));

		response->data = st->buf;
		response->size = size;

		return;
	}

	if (status == HTTP_CB_RESPONDING) {
		st = response->session_data;

		if (st->body_sent) {
			response->status = HTTP_RESP_NONE;
			return;
		}

		if (upload_data_id == upload_id) {
#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
			if (mtd_layout_label) {
				const char *cur_layout = get_mtd_layout_label();
				const char *env_layout = env_get("mtd_layout");

				if (!cur_layout || strcmp(cur_layout, mtd_layout_label) ||
				    !env_layout || strcmp(env_layout, mtd_layout_label)) {
					printf("httpd: saving mtd layout: %s\n", mtd_layout_label);
					env_set("mtd_layout", mtd_layout_label);
					env_set("mtd_layout_label", mtd_layout_label);
					env_set("mtdids", NULL);
					env_set("mtdparts", NULL);
					env_save();
				}
			}
#endif
			if (fw_type == FW_TYPE_INITRD)
				st->ret = 0;
			else
				st->ret = failsafe_write_image(upload_data,
							       upload_size, fw_type);
		}

		/* invalidate upload identifier */
		upload_data_id = rand();

		if (!st->ret)
			response->data = "success";
		else
			response->data = "failed";

		response->size = strlen(response->data);

		st->body_sent = 1;

		return;
	}

	if (status == HTTP_CB_CLOSED) {
		st = response->session_data;

		upgrade_success = !st->ret;
		auto_action_pending = upgrade_success &&
			(fw_type == FW_TYPE_INITRD || failsafe_auto_reboot_enabled());

		free(response->session_data);

		if (auto_action_pending)
			mtk_tcp_close_all_conn();
	}
}

static void style_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	if (status == HTTP_CB_NEW) {
		output_plain_file(response, "style.css");
		response->info.content_type = "text/css";
	}
}

static void js_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	if (status == HTTP_CB_NEW) {
		const char *uri = request && request->urih ? request->urih->uri : NULL;
		const char *file = "main.js";

		if (uri && strstr(uri, "i18n.js"))
			file = "i18n.js";
		else if (uri && strstr(uri, "themeloader.js"))
			file = "themeloader.js";

		output_plain_file(response, file);
		response->info.content_type = "text/javascript";
	}
}

static void not_found_handler(enum httpd_uri_handler_status status,
			      struct httpd_request *request,
			      struct httpd_response *response)
{
	if (status == HTTP_CB_NEW) {
		output_plain_file(response, "404.html");
		response->info.code = 404;
	}
}

static void html_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	if (status != HTTP_CB_NEW)
		return;

	if (output_plain_file(response, request->urih->uri + 1))
		not_found_handler(status, request, response);
}

#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
static const char *get_mtdlayout_str(void)
{
	static char mtd_layout_str[MTD_LAYOUTS_MAXLEN];
	ofnode node, layout;

	sprintf(mtd_layout_str, "%s;", get_mtd_layout_label());

	node = ofnode_path("/mtd-layout");
	if (ofnode_valid(node) && ofnode_get_child_count(node)) {
		ofnode_for_each_subnode(layout, node) {
			strcat(mtd_layout_str, ofnode_read_string(layout, "label"));
			strcat(mtd_layout_str, ";");
		}
	}

	return mtd_layout_str;
}
#endif

static void mtd_layout_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;

#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
	response->data = get_mtdlayout_str();
#else
	response->data = "error";
#endif

	response->size = strlen(response->data);

	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
}

int start_web_failsafe(void)
{
	struct httpd_instance *inst;

	inst = httpd_find_instance(80);
	if (inst)
		httpd_free_instance(inst);

	inst = httpd_create_instance(80);
	if (!inst) {
		printf("Error: failed to create HTTP instance on port 80\n");
		return -1;
	}

	httpd_register_uri_handler(inst, "/", &index_handler, NULL);
	httpd_register_uri_handler(inst, "/bl2.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/booting.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/cgi-bin/luci", &index_handler, NULL);
	httpd_register_uri_handler(inst, "/cgi-bin/luci/", &index_handler, NULL);
	httpd_register_uri_handler(inst, "/fail.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/flashing.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/getmtdlayout", &mtd_layout_handler, NULL);
#ifdef CONFIG_MTK_BOOTMENU_MMC
	if (failsafe_mmc_present())
		httpd_register_uri_handler(inst, "/gpt.html", &html_handler, NULL);
#endif
	httpd_register_uri_handler(inst, "/initramfs.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/main.js", &js_handler, NULL);
	httpd_register_uri_handler(inst, "/result", &result_handler, NULL);
	httpd_register_uri_handler(inst, "/style.css", &style_handler, NULL);
	httpd_register_uri_handler(inst, "/uboot.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/upload", &upload_handler, NULL);
	httpd_register_uri_handler(inst, "/version", &version_handler, NULL);
	httpd_register_uri_handler(inst, "", &not_found_handler, NULL);
	httpd_register_uri_handler(inst, "/reboot", &reboot_handler, NULL);
	httpd_register_uri_handler(inst, "/reboot-failsafe", &reboot_failsafe_handler, NULL);
	httpd_register_uri_handler(inst, "/reboot.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/sysinfo", &sysinfo_handler, NULL);
#ifdef CONFIG_WEBUI_FAILSAFE_UI_NEW
	httpd_register_uri_handler(inst, "/favicon.svg", &picture_handler, NULL);
	httpd_register_uri_handler(inst, "/themeloader.js", &js_handler, NULL);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_I18N
	httpd_register_uri_handler(inst, "/i18n.js", &js_handler, NULL);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_BACKUP
	httpd_register_uri_handler(inst, "/backup.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/backup/info", &backupinfo_handler, NULL);
	httpd_register_uri_handler(inst, "/backup/main", &backup_handler, NULL);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_FLASH
	httpd_register_uri_handler(inst, "/flash.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/flash/read", &flash_handler, NULL);
	httpd_register_uri_handler(inst, "/flash/write", &flash_handler, NULL);
	httpd_register_uri_handler(inst, "/flash/erase", &flash_handler, NULL);
	httpd_register_uri_handler(inst, "/flash/restore", &flash_handler, NULL);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_ENV
	httpd_register_uri_handler(inst, "/env.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/env/list", &env_list_handler, NULL);
	httpd_register_uri_handler(inst, "/env/set", &env_set_handler, NULL);
	httpd_register_uri_handler(inst, "/env/unset", &env_unset_handler, NULL);
	httpd_register_uri_handler(inst, "/env/reset", &env_reset_handler, NULL);
	httpd_register_uri_handler(inst, "/env/restore", &env_restore_handler, NULL);
#ifdef CONFIG_WEBUI_FAILSAFE_UI_NEW
	httpd_register_uri_handler(inst, "/theme/get", &theme_get_handler, NULL);
	httpd_register_uri_handler(inst, "/theme/set", &theme_set_handler, NULL);
#endif
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_SIMG
	httpd_register_uri_handler(inst, "/simg.html", &html_handler, NULL);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_FACTORY
	httpd_register_uri_handler(inst, "/factory.html", &html_handler, NULL);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_CONSOLE
	/* Enable recording early so we can stream output to the browser */
	failsafe_webconsole_ensure_recording();
	httpd_register_uri_handler(inst, "/console.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/console/poll", &webconsole_poll_handler, NULL);
	httpd_register_uri_handler(inst, "/console/exec", &webconsole_exec_handler, NULL);
	httpd_register_uri_handler(inst, "/console/clear", &webconsole_clear_handler, NULL);
#endif

	if (IS_ENABLED(CONFIG_MTK_DHCPD))
		mtk_dhcpd_start();

	failsafe_httpd_running = true;
	net_loop(MTK_TCP);
	failsafe_httpd_running = false;

	if (IS_ENABLED(CONFIG_MTK_DHCPD))
		mtk_dhcpd_stop();

	return 0;
}

static int do_httpd(struct cmd_tbl *cmdtp, int flag, int argc,
		    char *const argv[])
{
	u32 local_ip;
	int ret;

#ifdef CONFIG_NET_FORCE_IPADDR
	net_ip = string_to_ip(CONFIG_IPADDR);
	net_netmask = string_to_ip(CONFIG_NETMASK);
#endif
	local_ip = ntohl(net_ip.s_addr);

	printf("\nWeb failsafe UI started\n");
	printf("URL: http://%u.%u.%u.%u/\n",
	       (local_ip >> 24) & 0xff, (local_ip >> 16) & 0xff,
	       (local_ip >> 8) & 0xff, local_ip & 0xff);
	printf("\nPress Ctrl+C to exit\n");

	ret = start_web_failsafe();

	if (auto_action_pending) {
		if (fw_type == FW_TYPE_INITRD)
			boot_from_mem((ulong)upload_data);
		else
			do_reset(NULL, 0, 0, NULL);
	}

	return ret;
}

U_BOOT_CMD(httpd, 1, 0, do_httpd,
	"Start failsafe HTTP server", ""
);