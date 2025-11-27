/*
 * Minimal Zephyr Redfish Implementation
 * Implements: ComputerSystem.Reset (On/ForceOff)
 *
 * redfishtool Systems -r 192.168.2.55 -vvv -I system
 * redfishtool Systems -r 192.168.2.55 -vvv reset On
 * redfishtool Systems -r 192.168.2.55 -vvv reset ForceOff
 */

#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/data/json.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include "redfish.h"
#include "certificate.h"
#include "power.h"

LOG_MODULE_REGISTER(redfish_app, CONFIG_LOG_DEFAULT_LEVEL);

static char serial_number[] = "12345";

void set_power_state(bool on)
{
	int ret;

	if (on)
		ret = power_on();
	else
		ret = power_off();

	if (ret < 0) {
		LOG_ERR("Failed to set power state: %d", ret);
		return;
	}

	LOG_INF("System Power State changed to: %s", get_power_state() ? "ON" : "OFF");
}

struct redfish_reset_payload {
	const char *reset_type;
};

static const struct json_obj_descr reset_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_reset_payload, "ResetType",
				  reset_type, JSON_TOK_STRING)
};

static uint16_t redfish_http_port = 80;

HTTP_SERVICE_DEFINE(redfish_http_service, NULL, &redfish_http_port, 5, 10,
		    NULL, NULL, NULL);

static const sec_tag_t sec_tag_list_verify_none[] = {
	HTTP_SERVER_CERTIFICATE_TAG,
};

static uint16_t redfish_https_port = 443;

HTTPS_SERVICE_DEFINE(redfish_https_service, NULL, &redfish_https_port,
		     5, 10, NULL, NULL, NULL, sec_tag_list_verify_none,
		     sizeof(sec_tag_list_verify_none));

/* Service Root: GET /redfish/v1/ */
static int service_root_handler(struct http_client_ctx *client,
		enum http_data_status status,
		const struct http_request_ctx *request_ctx,
		struct http_response_ctx *response_ctx,
		void *user_data)
{
	static const char *response =
		"{\n"
		"  \"@odata.type\": \"#ServiceRoot.v1_16_1.ServiceRoot\",\n"
		"  \"@odata.id\": \"/redfish/v1/\",\n"
		"  \"Id\": \"RootService\",\n"
		"  \"Name\": \"Root Service\",\n"
		"  \"RedfishVersion\": \"1.15.0\",\n"
		"  \"UUID\": \"92384634-2938-2342-8820-489239905423\",\n"
		"  \"Systems\": { \"@odata.id\": \"/redfish/v1/Systems\" }\n"
		"}\n";

	if (status == HTTP_SERVER_DATA_ABORTED)
		return 0;

	/* Only handle GET */
	if (client->method != HTTP_GET) {
		response_ctx->status = HTTP_405_METHOD_NOT_ALLOWED;
		return 0;
	}

	response_ctx->body = (uint8_t *)response;
	response_ctx->body_len = strlen(response);
	response_ctx->status = HTTP_200_OK;
	response_ctx->final_chunk = true;

	/* Note: Headers are usually set via a separate callback or helper in complex apps,
	   but basic content-type is often assumed or handled by default in simple resources.
	   For strict Redfish compliance, we might need a custom header callback,
	   but for this example, we rely on the body. */

	return 0;
}

/* Systems Collection: GET /redfish/v1/Systems */
static int systems_collection_handler(struct http_client_ctx *client,
				      enum http_data_status status,
				      const struct http_request_ctx *request_ctx,
				      struct http_response_ctx *response_ctx,
				      void *user_data)
{
	// Ok to hardcode ../system, or shouold it be the serial number?
	static const char *response =
		"{\n"
		"  \"@odata.id\": \"/redfish/v1/Systems\",\n"
		"  \"@odata.type\": \"#ComputerSystemCollection.ComputerSystemCollection\",\n"
		"  \"Name\": \"Computer System Collection\",\n"
		"  \"Members@odata.count\": 1,\n"
		"  \"Members\": [\n"
		"    { \"@odata.id\": \"/redfish/v1/Systems/system\" }\n"
		"  ]\n"
		"}\n";

	if (status == HTTP_SERVER_DATA_ABORTED) return 0;

	if (client->method != HTTP_GET) {
		response_ctx->status = HTTP_405_METHOD_NOT_ALLOWED;
		return 0;
	}

	response_ctx->body = (uint8_t *)response;
	response_ctx->body_len = strlen(response);
	response_ctx->status = HTTP_200_OK;
	response_ctx->final_chunk = true;

	return 0;
}

/* System Info: GET /redfish/v1/Systems/system */
static int system_info_handler(struct http_client_ctx *client,
			       enum http_data_status status,
			       const struct http_request_ctx *request_ctx,
			       struct http_response_ctx *response_ctx,
			       void *user_data)
{
	static char buffer[1024]; // Static to persist for response duration

	if (status == HTTP_SERVER_DATA_ABORTED) return 0;

	if (client->method != HTTP_GET) {
		response_ctx->status = HTTP_405_METHOD_NOT_ALLOWED;
		return 0;
	}

	snprintf(buffer, sizeof(buffer),
			"{\n"
			"  \"@odata.id\": \"/redfish/v1/Systems/system\",\n"
			"  \"@odata.type\": \"#ComputerSystem.v1_22_0.ComputerSystem\",\n"
			"  \"Id\": \"system\",\n"
			"  \"UUID\": \"38947555-7742-3448-3784-823347823834\",\n"
			"  \"Name\": \"Atlantis\",\n"
			"  \"Description\": \"Atlantis\",\n"
			"  \"PowerState\": \"%s\",\n"
			"  \"SerialNumber\": \"%s\",\n"
			"  \"Actions\": {\n"
			"    \"#ComputerSystem.Reset\": {\n"
			"      \"target\": \"/redfish/v1/Systems/system/Actions/ComputerSystem.Reset\",\n"
			"      \"ResetType@Redfish.AllowableValues\": [\n"
			"        \"On\",\n"
			"        \"ForceOff\"\n"
			"      ]\n"
			"    }\n"
			"  }\n"
			"}\n",
			get_power_state() ? "On" : "Off", serial_number
				);
	response_ctx->body = (uint8_t *)buffer;
	response_ctx->body_len = strlen(buffer);
	response_ctx->status = HTTP_200_OK;
	response_ctx->final_chunk = true;

	return 0;
}

/* 3. Action: POST /redfish/v1/Systems/system/Actions/ComputerSystem.Reset */
static char payload_buf[128]; // Buffer for incoming JSON
static size_t payload_len = 0;

static int system_reset_handler(struct http_client_ctx *client,
				enum http_data_status status,
				const struct http_request_ctx *request_ctx,
				struct http_response_ctx *response_ctx,
				void *user_data)
{
	if (status == HTTP_SERVER_DATA_ABORTED) {
		payload_len = 0;
		return 0;
	}

	if (client->method != HTTP_POST) {
		response_ctx->status = HTTP_405_METHOD_NOT_ALLOWED;
		return 0;
	}

	// Accumulate data
	if (request_ctx->data && request_ctx->data_len > 0) {
		if (payload_len + request_ctx->data_len < sizeof(payload_buf)) {
			memcpy(payload_buf + payload_len, request_ctx->data, request_ctx->data_len);
			payload_len += request_ctx->data_len;
		} else {
			LOG_ERR("Payload too large");
			response_ctx->status = HTTP_400_BAD_REQUEST;
			return 0;
		}
	}

	if (status == HTTP_SERVER_DATA_FINAL) {
		payload_buf[payload_len] = '\0';

		struct redfish_reset_payload payload;
		int ret = json_obj_parse(payload_buf, payload_len, reset_descr,
				ARRAY_SIZE(reset_descr), &payload);

		if (ret < 0) {
			response_ctx->status = HTTP_400_BAD_REQUEST;
		} else {
			LOG_INF("Reset Action: %s", payload.reset_type);

			if (strcmp(payload.reset_type, "On") == 0) {
				set_power_state(true);
				response_ctx->status = HTTP_204_NO_CONTENT; // Standard Redfish success
			} else if (strcmp(payload.reset_type, "ForceOff") == 0) {
				set_power_state(false);
				response_ctx->status = HTTP_204_NO_CONTENT;
			} else {
				response_ctx->status = HTTP_400_BAD_REQUEST;
			}
		}

		payload_len = 0; // Reset for next request
		response_ctx->final_chunk = true;
	}

	return 0;
}

// Root
static struct http_resource_detail_dynamic root_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = service_root_handler,
	.user_data = NULL,
};
HTTP_RESOURCE_DEFINE(redfish_root, redfish_http_service, "/redfish/v1/",
		&root_detail);
HTTP_RESOURCE_DEFINE(redfish_root_https, redfish_https_service, "/redfish/v1/",
		&root_detail);

// Systems Collection Registration
static struct http_resource_detail_dynamic systems_coll_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = systems_collection_handler,
	.user_data = NULL,
};
HTTP_RESOURCE_DEFINE(redfish_systems_coll, redfish_http_service,
		"/redfish/v1/Systems", &systems_coll_detail);
HTTP_RESOURCE_DEFINE(redfish_systems_coll_https, redfish_https_service,
		"/redfish/v1/Systems", &systems_coll_detail);

// System
static struct http_resource_detail_dynamic sys_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = system_info_handler,
	.user_data = NULL,
};
HTTP_RESOURCE_DEFINE(redfish_sys, redfish_http_service,
		"/redfish/v1/Systems/system", &sys_detail);
HTTP_RESOURCE_DEFINE(redfish_sys_https, redfish_https_service,
		"/redfish/v1/Systems/system", &sys_detail);

// Action
static struct http_resource_detail_dynamic action_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = system_reset_handler,
	.user_data = NULL,
};
HTTP_RESOURCE_DEFINE(redfish_action, redfish_http_service,
		"/redfish/v1/Systems/system/Actions/ComputerSystem.Reset",
		&action_detail);
HTTP_RESOURCE_DEFINE(redfish_action_https, redfish_https_service,
		"/redfish/v1/Systems/system/Actions/ComputerSystem.Reset",
		&action_detail);

static void setup_tls(void)
{
	int err;

	err = tls_credential_add(HTTP_SERVER_CERTIFICATE_TAG,
			TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
			server_certificate,
			sizeof(server_certificate));
	if (err < 0) {
		LOG_ERR("Failed to register public certificate: %d", err);
	}

	err = tls_credential_add(HTTP_SERVER_CERTIFICATE_TAG,
			TLS_CREDENTIAL_PRIVATE_KEY,
			private_key, sizeof(private_key));
	if (err < 0) {
		LOG_ERR("Failed to register private key: %d", err);
	}
}

int redfish_init(void)
{
	LOG_INF("Starting Redfish HTTP server");

	setup_tls();
	http_server_start();

	return 0;
}
