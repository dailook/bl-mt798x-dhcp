/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 *
 * Failsafe theme and picture handlers
 */

#include <env.h>
#include <errno.h>
#include <malloc.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <net/mtk_httpd.h>

#include "fs.h"
#include "failsafe_internal.h"

#define THEME_COLOR_ENV "failsafe_theme_color"
#define THEME_COLOR_MAX_LEN 8
#define THEME_MODE_ENV "failsafe_theme_mode"
#define THEME_MODE_MAX_LEN 8

static void failsafe_http_reply_json(struct httpd_response *response, int code,
	const char *json)
{
	response->status = HTTP_RESP_STD;
	response->data = json ? json : "{}";
	response->size = strlen(response->data);
	response->info.code = code;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";
}

static int failsafe_theme_normalize_hex(const char *in, char *out, size_t out_sz)
{
	const char *p;
	size_t len, i;

	if (!in || !out || out_sz < THEME_COLOR_MAX_LEN)
		return -EINVAL;

	p = in;
	while (*p && isspace((unsigned char)*p))
		p++;

	if (*p == '#')
		p++;

	len = strlen(p);
	while (len && isspace((unsigned char)p[len - 1]))
		len--;

	if (len != 3 && len != 6)
		return -EINVAL;

	for (i = 0; i < len; i++) {
		if (!isxdigit((unsigned char)p[i]))
			return -EINVAL;
	}

	out[0] = '#';
	if (len == 3) {
		for (i = 0; i < 3; i++) {
			char c = tolower((unsigned char)p[i]);
			out[1 + i * 2] = c;
			out[2 + i * 2] = c;
		}
		out[7] = '\0';
		return 0;
	}

	for (i = 0; i < 6; i++)
		out[1 + i] = tolower((unsigned char)p[i]);
	out[7] = '\0';
	return 0;
}

static int theme_get_form_value(struct httpd_request *request,
	const char *key, char **out, size_t max_len, bool allow_empty,
	bool allow_missing)
{
	struct httpd_form_value *v;
	char *buf;
	size_t n;

	if (!request || !key || !out)
		return -EINVAL;

	v = httpd_request_find_value(request, key);
	if (!v || !v->data) {
		if (allow_missing) {
			*out = NULL;
			return 0;
		}
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

static bool failsafe_theme_valid_mode(const char *mode)
{
	if (!mode || !mode[0])
		return false;
	return !strcmp(mode, "auto") || !strcmp(mode, "light") ||
		!strcmp(mode, "dark");
}

static const char *failsafe_guess_content_type(const char *path)
{
	const char *ext;

	if (!path)
		return "application/octet-stream";

	ext = strrchr(path, '.');
	if (!ext)
		return "application/octet-stream";

	if (!strcasecmp(ext, ".svg"))
		return "image/svg+xml";
	if (!strcasecmp(ext, ".png"))
		return "image/png";
	if (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg"))
		return "image/jpeg";
	if (!strcasecmp(ext, ".gif"))
		return "image/gif";
	if (!strcasecmp(ext, ".ico"))
		return "image/x-icon";

	return "application/octet-stream";
}

static int output_binary_file(struct httpd_response *response,
	const char *filename, const char *content_type)
{
	const struct fs_desc *file;
	int ret = 0;

	file = fs_find_file(filename);

	response->status = HTTP_RESP_STD;

	if (file) {
		response->data = file->data;
		response->size = file->size;
	} else {
		response->data = "Not Found";
		response->size = strlen(response->data);
		response->info.code = 404;
		ret = 1;
	}

	if (!response->info.code)
		response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = content_type ? content_type : "application/octet-stream";

	return ret;
}

void picture_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	const char *uri;
	const char *file;
	const char *fallback = NULL;
	const char *ctype;

	if (status != HTTP_CB_NEW)
		return;

	uri = request && request->urih ? request->urih->uri : NULL;
	if (!uri || !uri[0]) {
		response->status = HTTP_RESP_STD;
		response->data = "Not Found";
		response->size = strlen(response->data);
		response->info.code = 404;
		response->info.connection_close = 1;
		response->info.content_type = "text/plain";
		return;
	}

	file = uri[0] == '/' ? uri + 1 : uri;
	if (!strcmp(file, "favicon.ico"))
		fallback = "favicon.svg";

	ctype = failsafe_guess_content_type(file);
	if (output_binary_file(response, file, ctype) && fallback) {
		ctype = failsafe_guess_content_type(fallback);
		output_binary_file(response, fallback, ctype);
	}
}

void theme_get_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	const char *val;
	const char *theme;
	char color[THEME_COLOR_MAX_LEN] = "";
	static char resp[96];

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_GET) {
		failsafe_http_reply_json(response, 405,
			"{\"ok\":false,\"error\":\"method\"}");
		return;
	}

	val = env_get(THEME_COLOR_ENV);
	if (!val || failsafe_theme_normalize_hex(val, color, sizeof(color)))
		color[0] = '\0';

	theme = env_get(THEME_MODE_ENV);
	if (!failsafe_theme_valid_mode(theme))
		theme = "";

	snprintf(resp, sizeof(resp),
		"{\"ok\":true,\"color\":\"%s\",\"theme\":\"%s\"}",
		color, theme ? theme : "");

	failsafe_http_reply_json(response, 200, resp);
}

void theme_set_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *color = NULL;
	char *theme = NULL;
	char norm[THEME_COLOR_MAX_LEN];
	int ret;
	bool changed = false;
	const char *err = NULL;

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_json(response, 405,
			"{\"ok\":false,\"error\":\"method\"}");
		return;
	}

	ret = theme_get_form_value(request, "color", &color,
		THEME_COLOR_MAX_LEN, true, true);
	if (ret) {
		failsafe_http_reply_json(response, 400,
			"{\"ok\":false,\"error\":\"bad_color\"}");
		return;
	}

	ret = theme_get_form_value(request, "theme", &theme,
		THEME_MODE_MAX_LEN, true, true);
	if (ret) {
		free(color);
		failsafe_http_reply_json(response, 400,
			"{\"ok\":false,\"error\":\"bad_theme\"}");
		return;
	}

	if (color) {
		if (!color[0]) {
			ret = env_set(THEME_COLOR_ENV, NULL);
			if (ret)
				goto out_free;
			changed = true;
		} else {
			if (failsafe_theme_normalize_hex(color, norm, sizeof(norm))) {
				ret = -EINVAL;
				err = "bad_color";
				goto out_free;
			}
			ret = env_set(THEME_COLOR_ENV, norm);
			if (ret)
				goto out_free;
			changed = true;
		}
	}

	if (theme) {
		if (!theme[0]) {
			ret = env_set(THEME_MODE_ENV, NULL);
			if (ret)
				goto out_free;
			changed = true;
		} else {
			if (!failsafe_theme_valid_mode(theme)) {
				ret = -EINVAL;
				err = "bad_theme";
				goto out_free;
			}
			ret = env_set(THEME_MODE_ENV, theme);
			if (ret)
				goto out_free;
			changed = true;
		}
	}

	if (changed) {
		ret = env_save();
		if (ret)
			goto out_free;
	}

	free(color);
	free(theme);
	failsafe_http_reply_json(response, 200, "{\"ok\":true}");
	return;

out_free:
	free(color);
	free(theme);
	if (ret == -EINVAL) {
		failsafe_http_reply_json(response, 400,
			err && !strcmp(err, "bad_color") ?
			"{\"ok\":false,\"error\":\"bad_color\"}" :
			"{\"ok\":false,\"error\":\"bad_theme\"}");
	}
	else
		failsafe_http_reply_json(response, 500,
			"{\"ok\":false,\"error\":\"save\"}");
}
