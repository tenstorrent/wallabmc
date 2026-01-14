/*
 * Minimal Zephyr Redfish Implementation
 * Implements: ComputerSystem.Reset (On/ForceOff/PowerCycle)
 *
 * redfishtool Systems -r 192.168.2.55 -vvv -I system
 * redfishtool Systems -r 192.168.2.55 -vvv reset On
 * redfishtool Systems -r 192.168.2.55 -vvv reset ForceOff
 * redfishtool Systems -r 192.168.2.55 -vvv reset PowerCycle
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/data/json.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/base64.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
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

static const struct http_header json_header[] = {
	{.name = "Content-Type", .value = "application/json"}
};

struct redfish_reset_payload {
	const char *reset_type;
};

static const struct json_obj_descr reset_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_reset_payload, "ResetType",
				  reset_type, JSON_TOK_STRING)
};

/* Structures for JSON encoding */

/* Redfish Version response */
struct redfish_version {
	const char *v1;
};

/* Service Root: Systems nested object */
struct redfish_systems_ref {
	const char *odata_id;
};

/* Service Root response */
struct redfish_service_root {
	const char *odata_type;
	const char *odata_id;
	const char *id;
	const char *name;
	const char *redfish_version;
	const char *uuid;
	struct redfish_systems_ref systems;
};

/* Systems Collection: Member object */
struct redfish_system_member {
	const char *odata_id;
};

/* Systems Collection response */
struct redfish_systems_collection {
	const char *odata_id;
	const char *odata_type;
	const char *name;
	int32_t members_count;
	struct redfish_system_member members[1];
	size_t members_len;
};

/* System Info: ResetType array */
struct redfish_reset_action {
	const char *target;
	const char *reset_type_values[3];
	size_t reset_type_values_len;
};

/* System Info: Actions nested object */
struct redfish_actions {
	struct redfish_reset_action reset_action;
};

/* System Info: ProcessorSummary nested object */
struct redfish_processor_summary {
	int32_t count;
	const char *description;
};

/* System Info: MemorySummary nested object */
struct redfish_memory_summary {
	int32_t total_system_GiB;
};


/* System Info response */
struct redfish_computer_system {
	const char *odata_id;
	const char *odata_type;
	const char *id;
	const char *uuid;
	const char *name;
	const char *manufacturer;
	const char *model;
	const char *host_name;
	const char *power_state;
	const char *serial_number;
	struct redfish_processor_summary processor_summary;
	struct redfish_memory_summary memory_summary;
	struct redfish_actions actions;
};

/* JSON descriptors for encoding */

/* Redfish Version descriptor */
static const struct json_obj_descr redfish_version_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_version, "v1",
				  v1, JSON_TOK_STRING),
};

/* Service Root: Systems nested object descriptor */
static const struct json_obj_descr systems_ref_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_systems_ref, "@odata.id",
				  odata_id, JSON_TOK_STRING),
};

/* Service Root descriptor */
static const struct json_obj_descr service_root_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_service_root, "@odata.type",
				  odata_type, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_service_root, "@odata.id",
				  odata_id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_service_root, "Id",
				  id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_service_root, "Name",
				  name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_service_root, "RedfishVersion",
				  redfish_version, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_service_root, "UUID",
				  uuid, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT_NAMED(struct redfish_service_root, "Systems",
				    systems, systems_ref_descr),
};

/* Systems Collection: Member object descriptor */
static const struct json_obj_descr system_member_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_system_member, "@odata.id",
				  odata_id, JSON_TOK_STRING),
};

/* Systems Collection descriptor */
static const struct json_obj_descr systems_collection_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_systems_collection, "@odata.id",
				  odata_id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_systems_collection, "@odata.type",
				  odata_type, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_systems_collection, "Name",
				  name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_systems_collection,
				  "Members@odata.count", members_count,
				  JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_OBJ_ARRAY_NAMED(struct redfish_systems_collection,
				       "Members", members, 1,
				       members_len, system_member_descr,
				       ARRAY_SIZE(system_member_descr)),
};

/* System Info: Reset action descriptor */
static const struct json_obj_descr reset_action_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct redfish_reset_action, target, JSON_TOK_STRING),
	JSON_OBJ_DESCR_ARRAY_NAMED(struct redfish_reset_action,
				   "ResetType@Redfish.AllowableValues",
				   reset_type_values, 3, reset_type_values_len,
				   JSON_TOK_STRING),
};

/* System Info: Actions nested object descriptor */
static const struct json_obj_descr actions_descr[] = {
	JSON_OBJ_DESCR_OBJECT_NAMED(struct redfish_actions, "#ComputerSystem.Reset",
				    reset_action, reset_action_descr),
};

/* System Info: ProcessorSummary nested object descriptor */
static const struct json_obj_descr processor_summary_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_processor_summary, "Count",
				  count, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_processor_summary, "Description",
				  description, JSON_TOK_STRING),
};

/* System Info: MemorySummary nested object descriptor */
static const struct json_obj_descr memory_summary_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_memory_summary, "TotalSystemMemoryGiB",
				  total_system_GiB, JSON_TOK_NUMBER),
};

/* System Info descriptor */
static const struct json_obj_descr computer_system_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_computer_system, "@odata.id",
				  odata_id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_computer_system, "@odata.type",
				  odata_type, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_computer_system, "Id",
				  id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_computer_system, "UUID",
				  uuid, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_computer_system, "Name",
				  name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_computer_system, "Manufacturer",
				  manufacturer, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_computer_system, "Model",
				  model, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_computer_system, "PowerState",
				  power_state, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_computer_system, "SerialNumber",
				  serial_number, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT_NAMED(struct redfish_computer_system, "ProcessorSummary",
				    processor_summary, processor_summary_descr),
	JSON_OBJ_DESCR_OBJECT_NAMED(struct redfish_computer_system, "MemorySummary",
				    memory_summary, memory_summary_descr),
	JSON_OBJ_DESCR_OBJECT_NAMED(struct redfish_computer_system, "Actions",
				    actions, actions_descr),
};

/* "Basic" (not session based) authentication, uses HTTP Authorization header */
HTTP_SERVER_REGISTER_HEADER_CAPTURE(capture_authorization, "Authorization");

#define CREDENTIALS_MAX_LEN 64
static int validate_auth(struct http_client_ctx *client)
{
	size_t header_count = client->header_capture_ctx.count;
	const struct http_header *headers = client->header_capture_ctx.headers;
	const char *auth_header = NULL;
	const char *prefix = "Basic ";

	for (unsigned int i = 0; i < header_count; i++) {
		if (!strcmp(headers[i].name, "Authorization")) {
			auth_header = headers[i].value;
			break;
		}
	}

	if (auth_header == NULL) {
		LOG_WRN("No auth header");
		return -1;
	}

	if (strncmp(auth_header, prefix, strlen(prefix)) != 0) {
		LOG_WRN("Not basic auth");
		return -1; // Not Basic auth
	}

	// Extract the Base64 token part
	const char *b64_token = auth_header + strlen(prefix);
	size_t token_len = strlen(b64_token);

	static uint8_t decoded_buf[CREDENTIALS_MAX_LEN];
	size_t decoded_len = 0;

	int ret = base64_decode(decoded_buf, sizeof(decoded_buf) - 1, &decoded_len,
				b64_token, token_len);
	if (ret < 0) {
		LOG_WRN("BASE64 decode failed");
		return ret;
	}

	// Null-terminate the decoded string for safety
	decoded_buf[decoded_len] = '\0';

	// Build the expected string "user:pass"
	static uint8_t expected[CREDENTIALS_MAX_LEN];
	snprintf(expected, sizeof(expected), "%s:%s", "admin", config_bmc_admin_password());

	if (strcmp((char *)decoded_buf, expected) == 0)
		return 0; // Success!

	LOG_WRN("Authentication did not match");

	return -1;
}

/*
 * XXX: Zephyr could provide a send_http1_401 response to send a
 * canonical 401 header, but this still seems to work.
 */
static void set_unauth_response(struct http_response_ctx *ctx)
{
	static struct http_header extra_headers[] = {
		{ .name = "WWW-Authenticate", .value = "Basic realm=\"BMC\"" },
	};

	ctx->status = HTTP_401_UNAUTHORIZED;
	ctx->final_chunk = true;
	ctx->headers = extra_headers;
	ctx->header_count = ARRAY_SIZE(extra_headers);
	ctx->body = NULL;
	ctx->body_len = 0;
}

/* Redfish Version: GET /redfish */
static int redfish_version_handler(struct http_client_ctx *client,
				   enum http_transaction_status status,
				   const struct http_request_ctx *request_ctx,
				   struct http_response_ctx *response_ctx,
				   void *user_data)
{
	static char buffer[256];
	const struct redfish_version version = {
		.v1 = "/redfish/v1/"
	};

	/* Must not require auth */

	if (status == HTTP_SERVER_TRANSACTION_ABORTED)
		return 0;

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL)
		return 0;

	int ret = json_obj_encode_buf(redfish_version_descr,
				       ARRAY_SIZE(redfish_version_descr),
				       &version, buffer, sizeof(buffer));
	if (ret < 0) {
		LOG_ERR("Failed to encode redfish version: %d", ret);
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->final_chunk = true;
		return 0;
	}

	response_ctx->body = (uint8_t *)buffer;
	response_ctx->body_len = strlen(buffer);
	response_ctx->headers = json_header;
	response_ctx->header_count = ARRAY_SIZE(json_header);
	response_ctx->status = HTTP_200_OK;
	response_ctx->final_chunk = true;

	return 0;
}

/* Service Root: GET /redfish/v1/ */
static int service_root_handler(struct http_client_ctx *client,
		enum http_transaction_status status,
		const struct http_request_ctx *request_ctx,
		struct http_response_ctx *response_ctx,
		void *user_data)
{
	static char buffer[512];
	const struct redfish_service_root service_root = {
		.odata_type = "#ServiceRoot.v1_16_1.ServiceRoot",
		.odata_id = "/redfish/v1/",
		.id = "RootService",
		.name = "Root Service",
		.redfish_version = "1.15.0",
		.uuid = "92384634-2938-2342-8820-489239905423",
		.systems = {
			.odata_id = "/redfish/v1/Systems"
		}
	};

	/* Must not require auth */

	if (status == HTTP_SERVER_TRANSACTION_ABORTED)
		return 0;

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL)
		return 0;

	int ret = json_obj_encode_buf(service_root_descr,
				       ARRAY_SIZE(service_root_descr),
				       &service_root, buffer, sizeof(buffer));
	if (ret < 0) {
		LOG_ERR("Failed to encode service root: %d", ret);
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->final_chunk = true;
		return 0;
	}

	response_ctx->body = (uint8_t *)buffer;
	response_ctx->body_len = strlen(buffer);
	response_ctx->headers = json_header;
	response_ctx->header_count = ARRAY_SIZE(json_header);
	response_ctx->status = HTTP_200_OK;
	response_ctx->final_chunk = true;

	return 0;
}

/* Systems Collection: GET /redfish/v1/Systems */
static int systems_collection_handler(struct http_client_ctx *client,
				      enum http_transaction_status status,
				      const struct http_request_ctx *request_ctx,
				      struct http_response_ctx *response_ctx,
				      void *user_data)
{
	static char buffer[512];
	struct redfish_systems_collection systems_collection = {
		.odata_id = "/redfish/v1/Systems",
		.odata_type = "#ComputerSystemCollection.ComputerSystemCollection",
		.name = "Computer System Collection",
		.members_count = 1,
		.members = {
			{
				.odata_id = "/redfish/v1/Systems/system"
			}
		},
		.members_len = 1
	};

	if (validate_auth(client) < 0) {
		LOG_ERR("Failed to authenticate");
		set_unauth_response(response_ctx);
		return 0;
	}

	if (status == HTTP_SERVER_TRANSACTION_ABORTED)
		return 0;

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL)
		return 0;

	int ret = json_obj_encode_buf(systems_collection_descr,
				       ARRAY_SIZE(systems_collection_descr),
				       &systems_collection, buffer, sizeof(buffer));
	if (ret < 0) {
		LOG_ERR("Failed to encode systems collection: %d", ret);
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->final_chunk = true;
		return 0;
	}

	response_ctx->body = (uint8_t *)buffer;
	response_ctx->body_len = strlen(buffer);
	response_ctx->headers = json_header;
	response_ctx->header_count = ARRAY_SIZE(json_header);
	response_ctx->status = HTTP_200_OK;
	response_ctx->final_chunk = true;

	return 0;
}

/* System Info: GET /redfish/v1/Systems/system */
static int system_info_handler(struct http_client_ctx *client,
			       enum http_transaction_status status,
			       const struct http_request_ctx *request_ctx,
			       struct http_response_ctx *response_ctx,
			       void *user_data)
{
	static char buffer[1024]; // Static to persist for response duration

	if (validate_auth(client) < 0) {
		LOG_ERR("Failed to authenticate");
		set_unauth_response(response_ctx);
		return 0;
	}

	if (status == HTTP_SERVER_TRANSACTION_ABORTED)
		return 0;

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL)
		return 0;

	struct redfish_computer_system computer_system = {
		.odata_id = "/redfish/v1/Systems/system",
		.odata_type = "#ComputerSystem.v1_22_0.ComputerSystem",
		.id = "system",
		.uuid = "38947555-7742-3448-3784-823347823834",
		.name = "Dev Board 7",
		.manufacturer = "Tenstorrent",
		.model = "Atlantis",
		.processor_summary = {
			.count = 8,
			.description = "Tenstorrent Ascalon X™",
		},
		.memory_summary = {
			.total_system_GiB = 32,
		},
		.serial_number = serial_number,
		.power_state = get_power_state() ? "On" : "Off",
		.actions = {
			.reset_action = {
				.target = "/redfish/v1/Systems/system/Actions/ComputerSystem.Reset",
				.reset_type_values = {
					"On",
					"ForceOff",
					"PowerCycle"
				},
				.reset_type_values_len = 3
			}
		}
	};

	int ret = json_obj_encode_buf(computer_system_descr,
				       ARRAY_SIZE(computer_system_descr),
				       &computer_system, buffer, sizeof(buffer));
	if (ret < 0) {
		LOG_ERR("Failed to encode computer system: %d", ret);
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->final_chunk = true;
		return 0;
	}

	response_ctx->body = (uint8_t *)buffer;
	response_ctx->body_len = strlen(buffer);
	response_ctx->headers = json_header;
	response_ctx->header_count = ARRAY_SIZE(json_header);
	response_ctx->status = HTTP_200_OK;
	response_ctx->final_chunk = true;

	return 0;
}

/* 3. Action: POST /redfish/v1/Systems/system/Actions/ComputerSystem.Reset */
static char payload_buf[128]; // Buffer for incoming JSON
static size_t payload_len = 0;

static int system_reset_handler(struct http_client_ctx *client,
				enum http_transaction_status status,
				const struct http_request_ctx *request_ctx,
				struct http_response_ctx *response_ctx,
				void *user_data)
{
	if (validate_auth(client) < 0) {
		LOG_ERR("Failed to authenticate");
		set_unauth_response(response_ctx);
		return 0;
	}

	if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
		payload_len = 0;
		return 0;
	}

	if (client->method != HTTP_POST) {
		response_ctx->status = HTTP_405_METHOD_NOT_ALLOWED;
		response_ctx->final_chunk = true;
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
			response_ctx->final_chunk = true;
			return 0;
		}
	}

	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		payload_buf[payload_len] = '\0';

		struct redfish_reset_payload payload;
		int ret = json_obj_parse(payload_buf, payload_len, reset_descr,
				ARRAY_SIZE(reset_descr), &payload);

		if (ret < 0) {
			response_ctx->status = HTTP_400_BAD_REQUEST;
			response_ctx->final_chunk = true;
		} else {
			LOG_INF("Reset Action: %s", payload.reset_type);

			if (strcmp(payload.reset_type, "On") == 0) {
				set_power_state(true);
				response_ctx->status = HTTP_204_NO_CONTENT; // Standard Redfish success
			} else if (strcmp(payload.reset_type, "ForceOff") == 0) {
				set_power_state(false);
				response_ctx->status = HTTP_204_NO_CONTENT;
			} else if (strcmp(payload.reset_type, "PowerCycle") == 0) {
				power_reset();
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

// Redfish Version
static struct http_resource_detail_dynamic version_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = redfish_version_handler,
	.user_data = NULL,
};
// Root
static struct http_resource_detail_dynamic root_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = service_root_handler,
	.user_data = NULL,
};

// Systems Collection Registration
static struct http_resource_detail_dynamic systems_coll_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = systems_collection_handler,
	.user_data = NULL,
};

// System
static struct http_resource_detail_dynamic sys_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = system_info_handler,
	.user_data = NULL,
};

// Action
static struct http_resource_detail_dynamic action_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = system_reset_handler,
	.user_data = NULL,
};

HTTP_RESOURCE_DEFINE(redfish_version, http_service, "/redfish/", &version_detail);
HTTP_RESOURCE_DEFINE(redfish_version_no_slash, http_service, "/redfish", &version_detail);
HTTP_RESOURCE_DEFINE(redfish_root, http_service, "/redfish/v1/",
		&root_detail);
HTTP_RESOURCE_DEFINE(redfish_root_no_slash, http_service, "/redfish/v1",
		&root_detail);
HTTP_RESOURCE_DEFINE(redfish_systems_coll, http_service,
		"/redfish/v1/Systems", &systems_coll_detail);
HTTP_RESOURCE_DEFINE(redfish_sys, http_service,
		"/redfish/v1/Systems/system", &sys_detail);
HTTP_RESOURCE_DEFINE(redfish_action, http_service,
		"/redfish/v1/Systems/system/Actions/ComputerSystem.Reset",
		&action_detail);

#if defined(CONFIG_APP_HTTPS)
HTTP_RESOURCE_DEFINE(redfish_version_https, https_service, "/redfish/", &version_detail);
HTTP_RESOURCE_DEFINE(redfish_version_no_slash_https, https_service, "/redfish", &version_detail);
HTTP_RESOURCE_DEFINE(redfish_root_https, https_service, "/redfish/v1/",
		&root_detail);
HTTP_RESOURCE_DEFINE(redfish_root_no_slash_https, https_service, "/redfish/v1",
		&root_detail);
HTTP_RESOURCE_DEFINE(redfish_systems_coll_https, https_service,
		"/redfish/v1/Systems", &systems_coll_detail);
HTTP_RESOURCE_DEFINE(redfish_sys_https, https_service,
		"/redfish/v1/Systems/system", &sys_detail);
HTTP_RESOURCE_DEFINE(redfish_action_https, https_service,
		"/redfish/v1/Systems/system/Actions/ComputerSystem.Reset",
		&action_detail);
#endif /* defined(CONFIG_APP_HTTPS) */
