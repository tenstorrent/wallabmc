/*
 * HTTP web server
 *
 * Copyright (c) 2023, Emna Rekik
 * Copyright (c) 2024, Nordic Semiconductor
 *
 * SPDX-FileCopyrightText: © 2025-2026 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#if defined(CONFIG_SHELL_BACKEND_WEBSOCKET)
#include <zephyr/shell/shell_websocket.h>
#endif
#include <stdio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wallabmc_http, LOG_LEVEL_INF);

#include "http.h"
#include "config.h"

static uint16_t http_service_port = 80;
HTTP_SERVICE_DEFINE(http_service, NULL, &http_service_port,
		    3, 10, NULL, NULL, NULL);

#if defined(CONFIG_SHELL_BACKEND_WEBSOCKET)
DEFINE_WEBSOCKET_SERVICE(http_service);
#endif

#if defined(CONFIG_APP_HTTPS)
#include "certificate.h"

static const sec_tag_t sec_tag_list_verify_none[] = {
		HTTP_SERVER_CERTIFICATE_TAG,
#if defined(CONFIG_MBEDTLS_KEY_EXCHANGE_PSK_ENABLED)
		PSK_TAG,
#endif
	};

static uint16_t https_service_port = 443;
HTTPS_SERVICE_DEFINE(https_service, NULL, &https_service_port,
		     3, 10, NULL, NULL, NULL, sec_tag_list_verify_none,
		     sizeof(sec_tag_list_verify_none));

#if defined(CONFIG_SHELL_BACKEND_WEBSOCKET)
DEFINE_WEBSOCKET_SERVICE(https_service);
#endif

static int setup_tls(void)
{
	int err = 0;

#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
	err = tls_credential_add(HTTP_SERVER_CERTIFICATE_TAG,
				 TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
				 server_certificate,
				 sizeof(server_certificate));
	if (err < 0) {
		LOG_ERR("Failed to register public certificate: %d", err);
		return err;
	}

	err = tls_credential_add(HTTP_SERVER_CERTIFICATE_TAG,
				 TLS_CREDENTIAL_PRIVATE_KEY,
				 private_key, sizeof(private_key));
	if (err < 0) {
		LOG_ERR("Failed to register private key: %d", err);
		return err;
	}

#if defined(CONFIG_MBEDTLS_KEY_EXCHANGE_PSK_ENABLED)
	err = tls_credential_add(PSK_TAG,
				 TLS_CREDENTIAL_PSK,
				 psk,
				 sizeof(psk));
	if (err < 0) {
		LOG_ERR("Failed to register PSK: %d", err);
		return err;
	}

	err = tls_credential_add(PSK_TAG,
				 TLS_CREDENTIAL_PSK_ID,
				 psk_id,
				 sizeof(psk_id) - 1);
	if (err < 0) {
		LOG_ERR("Failed to register PSK ID: %d", err);
		return err;
	}
#endif /* defined(CONFIG_MBEDTLS_KEY_EXCHANGE_PSK_ENABLED) */
#endif /* defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS) */
	return err;
}
#else /* defined(CONFIG_APP_HTTPS) */
static int setup_tls(void)
{
	return 0;
}
#endif /* defined(CONFIG_APP_HTTPS) */

/* Use the Sec-WebSocket-Protocol header to send authentication for the web socket */
HTTP_SERVER_REGISTER_HEADER_CAPTURE(capture_sec_websocket_protocol, "Sec-WebSocket-Protocol");

#define CREDENTIALS_MAX_LEN 64
static int ws_validate_auth(const struct http_header *headers, size_t header_count)
{
	const char *auth_header = NULL;
	const char *prefix = "Basic_";

	for (unsigned int i = 0; i < header_count; i++) {
		if (!strcmp(headers[i].name, "Sec-WebSocket-Protocol")) {
			auth_header = headers[i].value;
			break;
		}
	}

	if (auth_header == NULL) {
		LOG_WRN("No auth header");
		return -1;
	}

	if (strncmp(auth_header, prefix, strlen(prefix)) != 0) {
		LOG_WRN("Unexpected Sec-WebSocket-Protocol");
		return -1;
	}

	// Build the expected string "user_pass"
	static uint8_t expected[CREDENTIALS_MAX_LEN];
	snprintf(expected, sizeof(expected), "%s_%s", "admin", config_bmc_admin_password());

	if (strcmp(auth_header + strlen(prefix), expected) == 0)
		return 0; // Success!

	LOG_WRN("Authentication did not match");

	return -1;
}

static int (*shell_http_ws_cb)(int ws_socket, struct http_request_ctx *request_ctx, void *user_data);
static int (*shell_https_ws_cb)(int ws_socket, struct http_request_ctx *request_ctx, void *user_data);

int shell_http_ws_auth_cb(int ws_socket, struct http_request_ctx *request_ctx, void *user_data)
{
	if (ws_validate_auth(request_ctx->headers, request_ctx->header_count) < 0) {
		return -EPERM;
	}
	return shell_http_ws_cb(ws_socket, request_ctx, user_data);
}

int shell_https_ws_auth_cb(int ws_socket, struct http_request_ctx *request_ctx, void *user_data)
{
	if (ws_validate_auth(request_ctx->headers, request_ctx->header_count) < 0) {
		return -EPERM;
	}
	return shell_https_ws_cb(ws_socket, request_ctx, user_data);
}

int app_http_server_init(void)
{
	int err;

	err = setup_tls();
	if (err)
		return err;

#if defined(CONFIG_SHELL_BACKEND_WEBSOCKET)
	shell_http_ws_cb = GET_WS_DETAIL_NAME(http_service).cb;
	GET_WS_DETAIL_NAME(http_service).cb = shell_http_ws_auth_cb;
	err = shell_websocket_enable(&GET_WS_SHELL_NAME(http_service));
	if (err)
		return err;
#if defined(CONFIG_APP_HTTPS)
	shell_https_ws_cb = GET_WS_DETAIL_NAME(https_service).cb;
	GET_WS_DETAIL_NAME(https_service).cb = shell_https_ws_auth_cb;
	err = shell_websocket_enable(&GET_WS_SHELL_NAME(https_service));
	if (err)
		return err;
#endif
#endif

	return http_server_start();
}
