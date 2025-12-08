/*
 * HTTP web server
 *
 * Copyright (c) 2023, Emna Rekik
 * Copyright (c) 2024, Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <stdio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(http_server, LOG_LEVEL_DBG);

#include "http.h"

static uint16_t http_service_port = 80;
HTTP_SERVICE_DEFINE(http_service, NULL, &http_service_port,
		    2, 5, NULL, NULL, NULL);


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
		     2, 5, NULL, NULL, NULL, sec_tag_list_verify_none,
		     sizeof(sec_tag_list_verify_none));

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

int app_http_server_init(void)
{
	int err;

	err = setup_tls();
	if (err)
		return err;

	return http_server_start();
}
