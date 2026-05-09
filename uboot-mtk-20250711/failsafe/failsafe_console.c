/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 *
 * Failsafe Web console
 */

#include <command.h>
#include <env.h>
#include <malloc.h>
#include <console.h>
#include <membuf.h>
#include <asm/global_data.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <net/mtk_httpd.h>

#include "failsafe_internal.h"

DECLARE_GLOBAL_DATA_PTR;

#define WEB_CONSOLE_CMD_MAX		256
#define WEB_CONSOLE_POLL_MAX	8192

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

int failsafe_webconsole_ensure_recording(void)
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

void webconsole_poll_handler(enum httpd_uri_handler_status status,
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

void webconsole_exec_handler(enum httpd_uri_handler_status status,
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
	{
		const char *prompt = failsafe_get_prompt();

		if (!prompt || !prompt[0])
			prompt = "MTK> ";

		if (prompt[0] != '\n')
			printf("\n%s", prompt);
		else
			printf("%s", prompt);
	}

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

void webconsole_clear_handler(enum httpd_uri_handler_status status,
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
