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
#ifdef CONFIG_WEBUI_FAILSAFE_ENV
#include <env_internal.h>
#endif
#include <malloc.h>
#include <console.h>
#include <membuf.h>
#include <net.h>
#include <net/mtk_tcp.h>
#include <net/mtk_httpd.h>
#include <net/mtk_dhcpd.h>
#include <u-boot/md5.h>
#include <asm/global_data.h>
#include <linux/kernel.h>
#include <linux/stringify.h>
#include <linux/ctype.h>
#ifdef CONFIG_MTD
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/spi-nor.h>
#include <linux/mtd/spinand.h>
#endif
#include <limits.h>
#include <dm/ofnode.h>
#include <vsprintf.h>
#include <version_string.h>
#include <failsafe/fw_type.h>
#include "../board/mediatek/common/boot_helper.h"
#include "fs.h"
#ifdef CONFIG_MTD
#include "../board/mediatek/common/mtd_helper.h"
#endif
#ifdef CONFIG_MTK_BOOTMENU_MMC
#include "../board/mediatek/common/mmc_helper.h"
#endif

DECLARE_GLOBAL_DATA_PTR;

static u32 upload_data_id;
static const void *upload_data;
static size_t upload_size;
static bool upgrade_success;
static failsafe_fw_t fw_type;

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

static void version_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;

	response->data = version_string;
	response->size = strlen(response->data);

	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
}

struct reboot_session {
	int dummy;
};

static size_t json_escape(char *dst, size_t dst_sz, const char *src)
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

#ifdef CONFIG_WEBUI_FAILSAFE_CONSOLE
#define WEB_CONSOLE_CMD_MAX	256
#define WEB_CONSOLE_POLL_MAX	2048

static const char *failsafe_get_prompt(void)
{
	const char *p = env_get("prompt");

	if (p && p[0])
		return p;

#ifdef CONFIG_SYS_PROMPT
	return CONFIG_SYS_PROMPT;
#else
	return "MTK> ";
#endif
}

static void failsafe_webconsole_free_session(enum httpd_uri_handler_status status,
	struct httpd_response *response)
{
	if (status != HTTP_CB_CLOSED)
		return;

	if (response->session_data) {
		free(response->session_data);
		response->session_data = NULL;
	}
}

static int failsafe_webconsole_require_token(struct httpd_request *request,
	struct httpd_response *response)
{
	const char *tok;
	struct httpd_form_value *v;
	size_t toklen;

	tok = env_get("failsafe_console_token");
	if (!tok || !tok[0])
		return 0;

	if (!request || request->method != HTTP_POST)
		goto deny;

	v = httpd_request_find_value(request, "token");
	if (!v || !v->data)
		goto deny;

	toklen = strlen(tok);
	if (v->size != toklen)
		goto deny;

	if (memcmp(v->data, tok, toklen))
		goto deny;

	return 0;

deny:
	response->status = HTTP_RESP_STD;
	response->info.code = 403;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
	response->data = "forbidden";
	response->size = strlen(response->data);
	return -EACCES;
}

static int failsafe_webconsole_ensure_recording(void)
{
	int ret;

	if (!gd)
		return -ENODEV;

	if (!gd->console_out.start) {
		ret = console_record_init();
		if (ret)
			return ret;
	}

	gd->flags |= GD_FLG_RECORD;
	return 0;
}

static void webconsole_poll_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *chunk = NULL, *esc = NULL, *json = NULL;
	int ret, avail, want, got;
	size_t esc_sz, json_sz;

	if (status == HTTP_CB_CLOSED) {
		failsafe_webconsole_free_session(status, response);
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";

	if (!request || request->method != HTTP_POST) {
		response->info.code = 405;
		response->data = "{\"error\":\"method\"}\n";
		response->size = strlen(response->data);
		return;
	}

	if (failsafe_webconsole_require_token(request, response))
		return;

	ret = failsafe_webconsole_ensure_recording();
	if (ret) {
		response->info.code = 503;
		response->data = "{\"error\":\"no_console\"}\n";
		response->size = strlen(response->data);
		return;
	}

	avail = membuf_avail(&gd->console_out);
	want = min(avail, (int)WEB_CONSOLE_POLL_MAX);

	chunk = malloc(want + 1);
	if (!chunk) {
		response->info.code = 500;
		response->data = "{\"error\":\"oom\"}\n";
		response->size = strlen(response->data);
		return;
	}

	got = want ? membuf_get(&gd->console_out, chunk, want) : 0;
	chunk[got] = '\0';

	/* Worst case: every char becomes ' ' or escaped with one extra backslash */
	esc_sz = (size_t)got * 2 + 64;
	esc = malloc(esc_sz);
	if (!esc) {
		free(chunk);
		response->info.code = 500;
		response->data = "{\"error\":\"oom\"}\n";
		response->size = strlen(response->data);
		return;
	}

	json_escape(esc, esc_sz, chunk);
	free(chunk);

	json_sz = strlen(esc) + 128;
	json = malloc(json_sz);
	if (!json) {
		free(esc);
		response->info.code = 500;
		response->data = "{\"error\":\"oom\"}\n";
		response->size = strlen(response->data);
		return;
	}

	snprintf(json, json_sz, "{\"data\":\"%s\",\"avail\":%d}\n", esc,
		membuf_avail(&gd->console_out));
	free(esc);

	response->data = json;
	response->size = strlen(json);
	response->session_data = json;
}

static void webconsole_exec_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	struct httpd_form_value *cmdv;
	char cmd[WEB_CONSOLE_CMD_MAX + 1];
	char *esc = NULL, *json = NULL;
	int ret;
	size_t esc_sz, json_sz;

	if (status == HTTP_CB_CLOSED) {
		failsafe_webconsole_free_session(status, response);
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";

	if (!request || request->method != HTTP_POST) {
		response->info.code = 405;
		response->data = "{\"error\":\"method\"}\n";
		response->size = strlen(response->data);
		return;
	}

	if (failsafe_webconsole_require_token(request, response))
		return;

	ret = failsafe_webconsole_ensure_recording();
	if (ret) {
		response->info.code = 503;
		response->data = "{\"error\":\"no_console\"}\n";
		response->size = strlen(response->data);
		return;
	}

	cmdv = httpd_request_find_value(request, "cmd");
	if (!cmdv || !cmdv->data || !cmdv->size) {
		response->info.code = 400;
		response->data = "{\"error\":\"no_cmd\"}\n";
		response->size = strlen(response->data);
		return;
	}

	memset(cmd, 0, sizeof(cmd));
	memcpy(cmd, cmdv->data, min((size_t)WEB_CONSOLE_CMD_MAX, cmdv->size));

	/* Echo to console so browser sees what was executed */
	{
		const char *prompt = failsafe_get_prompt();
		size_t plen = prompt ? strlen(prompt) : 0;
		bool need_space = plen && prompt[plen - 1] != ' ' && prompt[plen - 1] != '\t';

		if (!prompt || !prompt[0])
			prompt = "MTK> ";

		printf("%s%s%s\n", prompt, need_space ? " " : "", cmd);
	}
	ret = run_command(cmd, 0);

	esc_sz = strlen(cmd) * 2 + 64;
	esc = malloc(esc_sz);
	if (!esc)
		goto out_oom;

	json_escape(esc, esc_sz, cmd);
	json_sz = strlen(esc) + 128;
	json = malloc(json_sz);
	if (!json)
		goto out_oom;

	snprintf(json, json_sz, "{\"ok\":true,\"ret\":%d,\"cmd\":\"%s\"}\n", ret, esc);
	free(esc);

	response->data = json;
	response->size = strlen(json);
	response->session_data = json;
	return;

out_oom:
	free(esc);
	free(json);
	response->info.code = 500;
	response->data = "{\"error\":\"oom\"}\n";
	response->size = strlen(response->data);
}

static void webconsole_clear_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *json;

	if (status == HTTP_CB_CLOSED) {
		failsafe_webconsole_free_session(status, response);
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";

	if (!request || request->method != HTTP_POST) {
		response->info.code = 405;
		response->data = "{\"error\":\"method\"}\n";
		response->size = strlen(response->data);
		return;
	}

	if (failsafe_webconsole_require_token(request, response))
		return;

	if (failsafe_webconsole_ensure_recording()) {
		response->info.code = 503;
		response->data = "{\"error\":\"no_console\"}\n";
		response->size = strlen(response->data);
		return;
	}

	console_record_reset();

	json = malloc(64);
	if (!json) {
		response->info.code = 500;
		response->data = "{\"error\":\"oom\"}\n";
		response->size = strlen(response->data);
		return;
	}

	snprintf(json, 64, "{\"ok\":true}\n");
	response->data = json;
	response->size = strlen(json);
	response->session_data = json;
}
#endif /* CONFIG_WEBUI_FAILSAFE_CONSOLE */

#ifdef CONFIG_WEBUI_FAILSAFE_ENV
#define ENV_NAME_MAX_LEN 128

static void failsafe_env_free_session(enum httpd_uri_handler_status status,
	struct httpd_response *response)
{
	if (status != HTTP_CB_CLOSED)
		return;

	if (response->session_data) {
		free(response->session_data);
		response->session_data = NULL;
	}
}

static void failsafe_http_reply_text(struct httpd_response *response, int code,
	const char *text)
{
	response->status = HTTP_RESP_STD;
	response->data = text ? text : "";
	response->size = strlen(response->data);
	response->info.code = code;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
}

static int failsafe_env_get_form_value(struct httpd_request *request,
	const char *key, char **out, size_t max_len, bool allow_empty)
{
	struct httpd_form_value *v;
	char *buf;
	size_t n;

	if (!request || !key || !out)
		return -EINVAL;

	v = httpd_request_find_value(request, key);
	if (!v || !v->data) {
		if (allow_empty) {
			buf = strdup("");
			if (!buf)
				return -ENOMEM;
			*out = buf;
			return 0;
		}
		return -EINVAL;
	}

	n = v->size;
	if (!allow_empty && !n)
		return -EINVAL;
	if (n > max_len)
		return -E2BIG;

	buf = malloc(n + 1);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, v->data, n);
	buf[n] = '\0';
	*out = buf;
	return 0;
}

static char *failsafe_env_export_text(size_t *out_len)
{
	env_t *envbuf;
	char *out;
	size_t out_sz, i, n;

	if (out_len)
		*out_len = 0;

	envbuf = malloc(sizeof(*envbuf));
	if (!envbuf)
		return NULL;

	if (env_export(envbuf)) {
		free(envbuf);
		return NULL;
	}

	out_sz = ENV_SIZE + 2;
	out = malloc(out_sz);
	if (!out) {
		free(envbuf);
		return NULL;
	}

	n = 0;
	for (i = 0; i < ENV_SIZE - 1; i++) {
		if (!envbuf->data[i] && !envbuf->data[i + 1])
			break;

		out[n++] = envbuf->data[i] ? envbuf->data[i] : '\n';
		if (n + 1 >= out_sz)
			break;
	}

	if (n && out[n - 1] != '\n')
		out[n++] = '\n';

	out[n] = '\0';
	if (out_len)
		*out_len = n;

	free(envbuf);
	return out;
}

static void env_list_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *out;
	size_t out_len = 0;

	if (status == HTTP_CB_CLOSED) {
		failsafe_env_free_session(status, response);
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_GET) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}

	out = failsafe_env_export_text(&out_len);
	if (!out) {
		failsafe_http_reply_text(response, 500, "export failed");
		return;
	}

	response->status = HTTP_RESP_STD;
	response->data = out;
	response->size = out_len;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
	response->session_data = out;
}

static void env_set_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *name = NULL, *value = NULL;
	int ret;

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}

	ret = failsafe_env_get_form_value(request, "name", &name,
		ENV_NAME_MAX_LEN, false);
	if (ret) {
		failsafe_http_reply_text(response, 400, "bad name");
		return;
	}

	ret = failsafe_env_get_form_value(request, "value", &value,
		ENV_SIZE - 1, true);
	if (ret) {
		free(name);
		failsafe_http_reply_text(response, 400, "bad value");
		return;
	}

	ret = env_set(name, value);
	if (!ret)
		ret = env_save();

	free(name);
	free(value);

	if (ret) {
		failsafe_http_reply_text(response, 500, "save failed");
		return;
	}

	failsafe_http_reply_text(response, 200, "ok");
}

static void env_unset_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *name = NULL;
	int ret;

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}

	ret = failsafe_env_get_form_value(request, "name", &name,
		ENV_NAME_MAX_LEN, false);
	if (ret) {
		failsafe_http_reply_text(response, 400, "bad name");
		return;
	}

	ret = env_set(name, NULL);
	if (!ret)
		ret = env_save();

	free(name);

	if (ret) {
		failsafe_http_reply_text(response, 500, "save failed");
		return;
	}

	failsafe_http_reply_text(response, 200, "ok");
}

static void env_reset_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	int ret;

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}

	env_set_default(NULL, 0);
	ret = env_save();

	if (ret) {
		failsafe_http_reply_text(response, 500, "save failed");
		return;
	}

	failsafe_http_reply_text(response, 200, "ok");
}

static void env_restore_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	struct httpd_form_value *fw;
	int ret;

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}

	fw = httpd_request_find_value(request, "envfile");
	if (!fw || !fw->data || fw->size < sizeof(env_t)) {
		failsafe_http_reply_text(response, 400, "bad file");
		return;
	}

	ret = env_import((const char *)fw->data, 1, 0);
	if (!ret)
		ret = env_save();

	if (ret) {
		failsafe_http_reply_text(response, 500, "restore failed");
		return;
	}

	failsafe_http_reply_text(response, 200, "ok");
}
#endif /* CONFIG_WEBUI_FAILSAFE_ENV */

static void sysinfo_handler(enum httpd_uri_handler_status status,
							struct httpd_request *request,
							struct httpd_response *response)
{
	char *buf;
	int len = 0;
	int left = 4096;
	off_t ram_size = 0;
	ofnode root, cpus, cpu;
	const char *board_model = NULL;
	const char *board_compat = NULL;
	const char *cpu_compat = NULL;
	u64 cpu_clk_hz = 0;
	char esc_board_model[256], esc_board_compat[256], esc_cpu_compat[256];

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

	/* CPU info from DT: /cpus/<first cpu node>/compatible, clock-frequency */
	cpus = ofnode_path("/cpus");
	if (ofnode_valid(cpus) && ofnode_get_child_count(cpus))
	{
		ofnode_for_each_subnode(cpu, cpus)
		{
			cpu_compat = ofnode_read_string(cpu, "compatible");
			if (!ofnode_read_u64(cpu, "clock-frequency", &cpu_clk_hz) && cpu_clk_hz)
				break;
			if (cpu_compat && cpu_compat[0])
				break;
		}
	}

	/* RAM size from global data */
	if (gd)
		ram_size = (off_t)gd->ram_size;

	json_escape(esc_board_model, sizeof(esc_board_model), board_model ? board_model : "");
	json_escape(esc_board_compat, sizeof(esc_board_compat), board_compat ? board_compat : "");
	json_escape(esc_cpu_compat, sizeof(esc_cpu_compat), cpu_compat ? cpu_compat : "");

	len += snprintf(buf + len, left - len, "{");
	len += snprintf(buf + len, left - len,
					"\"board\":{\"model\":\"%s\",\"compatible\":\"%s\"},",
					esc_board_model, esc_board_compat);
	len += snprintf(buf + len, left - len,
					"\"cpu\":{\"compatible\":\"%s\",\"clock_hz\":%llu},",
					esc_cpu_compat, (unsigned long long)cpu_clk_hz);
	len += snprintf(buf + len, left - len,
					"\"ram\":{\"size\":%llu}",
					(unsigned long long)ram_size);
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
		st = response->session_data;
		free(st);

		/* Make sure the current HTTP session has fully closed before reset */
		mtk_tcp_close_all_conn();
		do_reset(NULL, 0, 0, NULL);
	}
}

#ifdef CONFIG_WEBUI_FAILSAFE_BACKUP

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

static void backupinfo_handler(enum httpd_uri_handler_status status,
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

static void backup_handler(enum httpd_uri_handler_status status,
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

#endif /* CONFIG_WEBUI_FAILSAFE_BACKUP */

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
			if (mtd_layout_label &&
					strcmp(get_mtd_layout_label(), mtd_layout_label) != 0) {
				printf("httpd: saving mtd_layout_label: %s\n", mtd_layout_label);
				env_set("mtd_layout_label", mtd_layout_label);
				env_save();
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

		free(response->session_data);

		if (upgrade_success)
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
		output_plain_file(response, "main.js");
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
	httpd_register_uri_handler(inst, "/reboot.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/sysinfo", &sysinfo_handler, NULL);
#ifdef CONFIG_WEBUI_FAILSAFE_BACKUP
	httpd_register_uri_handler(inst, "/backup.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/backup/info", &backupinfo_handler, NULL);
	httpd_register_uri_handler(inst, "/backup/main", &backup_handler, NULL);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_ENV
	httpd_register_uri_handler(inst, "/env.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/env/list", &env_list_handler, NULL);
	httpd_register_uri_handler(inst, "/env/set", &env_set_handler, NULL);
	httpd_register_uri_handler(inst, "/env/unset", &env_unset_handler, NULL);
	httpd_register_uri_handler(inst, "/env/reset", &env_reset_handler, NULL);
	httpd_register_uri_handler(inst, "/env/restore", &env_restore_handler, NULL);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_FACTORY
	httpd_register_uri_handler(inst, "/factory.html", &html_handler, NULL);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_CONSOLE
	httpd_register_uri_handler(inst, "/console.html", &html_handler, NULL);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_CONSOLE
	/* Enable recording early so we can stream output to the browser */
	failsafe_webconsole_ensure_recording();

	httpd_register_uri_handler(inst, "/console/poll", &webconsole_poll_handler, NULL);
	httpd_register_uri_handler(inst, "/console/exec", &webconsole_exec_handler, NULL);
	httpd_register_uri_handler(inst, "/console/clear", &webconsole_clear_handler, NULL);
#endif

	if (IS_ENABLED(CONFIG_MTK_DHCPD))
		mtk_dhcpd_start();

	net_loop(MTK_TCP);

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

	if (upgrade_success) {
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