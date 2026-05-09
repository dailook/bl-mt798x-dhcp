/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 *
 * Failsafe environment management
 */

#include <env.h>
#include <env_internal.h>
#include <errno.h>
#include <malloc.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <net/mtk_httpd.h>

#include "failsafe_internal.h"

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

void env_list_handler(enum httpd_uri_handler_status status,
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

void env_set_handler(enum httpd_uri_handler_status status,
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

void env_unset_handler(enum httpd_uri_handler_status status,
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

void env_reset_handler(enum httpd_uri_handler_status status,
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

void env_restore_handler(enum httpd_uri_handler_status status,
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
