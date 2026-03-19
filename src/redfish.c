/*
 * Minimal Zephyr Redfish Implementation
 * Implements: ComputerSystem.Reset (On/ForceOff/PowerCycle)
 *
 * redfishtool Systems -r 192.168.2.55 -vvv -I system
 * redfishtool Systems -r 192.168.2.55 -vvv reset On
 * redfishtool Systems -r 192.168.2.55 -vvv reset ForceOff
 * redfishtool Systems -r 192.168.2.55 -vvv reset PowerCycle
 *
 * SPDX-FileCopyrightText: © 2025-2026 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/data/json.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/base64.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "power.h"
#include "rtc.h"

LOG_MODULE_REGISTER(redfish_app, CONFIG_LOG_DEFAULT_LEVEL);

/* This is racy because several requests could be concurrently processing */
/* XXX: must fix and make these per-client */
static char out_buffer[1024];
static char in_buffer[512];
static size_t in_buffer_len;

/*** Redfish HTTP handlers ***/

/* "Basic" (not session based) authentication, uses HTTP Authorization header */
HTTP_SERVER_REGISTER_HEADER_CAPTURE(capture_authorization, "authorization");
HTTP_SERVER_REGISTER_HEADER_CAPTURE(capture_x_auth_code, "x-auth-code");

#define CREDENTIALS_MAX_LEN 64
static int validate_auth(struct http_client_ctx *client)
{
	size_t header_count = client->header_capture_ctx.count;
	const struct http_header *headers = client->header_capture_ctx.headers;
	const char *auth_header = NULL;
	const char *prefix = "Basic ";

	for (unsigned int i = 0; i < header_count; i++) {
		if (!strcmp(headers[i].name, "authorization") ||
		    !strcmp(headers[i].name, "Authorization")) {
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

	uint8_t decoded_buf[CREDENTIALS_MAX_LEN];
	size_t decoded_len = 0;

	int ret = base64_decode(decoded_buf, sizeof(decoded_buf) - 1, &decoded_len,
				b64_token, token_len);
	if (ret < 0) {
		LOG_WRN("BASE64 decode failed");
		return ret;
	}

	decoded_buf[decoded_len] = '\0';

	uint8_t expected[CREDENTIALS_MAX_LEN];
	snprintf(expected, sizeof(expected), "%s:%s", "admin", config_bmc_admin_password());

	if (strcmp((char *)decoded_buf, expected) == 0)
		return 0; // Success!

	LOG_WRN("Authentication did not match");

	return -1;
}

static int validate_and_set_headers(struct http_client_ctx *client,
				struct http_response_ctx *ctx,
				uint32_t supported_methods)
{
	if (!(BIT(client->method) & supported_methods)) {
		ctx->status = HTTP_405_METHOD_NOT_ALLOWED;
		if (supported_methods == (BIT(HTTP_GET))) {
			static const struct http_header headers[] = {
				{ .name = "allow", .value = "GET" },
			};
			ctx->headers = headers;
			ctx->header_count = ARRAY_SIZE(headers);
		} else if (supported_methods == (BIT(HTTP_POST))) {
			static const struct http_header headers[] = {
				{ .name = "allow", .value = "POST" },
			};
			ctx->headers = headers;
			ctx->header_count = ARRAY_SIZE(headers);
		} else if (supported_methods == (BIT(HTTP_GET) | BIT(HTTP_PATCH))) {
			static const struct http_header headers[] = {
				{ .name = "allow", .value = "GET, PATCH" },
			};
			ctx->headers = headers;
			ctx->header_count = ARRAY_SIZE(headers);
		} else {
			LOG_ERR("Unsupported supported methods!");
		}
		ctx->final_chunk = true;
		ctx->body = NULL;
		ctx->body_len = 0;
		return -1;
	}

	if (client->method == HTTP_GET) {
		static const struct http_header headers[] = {
			{ .name = "content-type", .value = "application/json" },
			{ .name = "cache-control", .value = "no-cache" },
		};
		ctx->headers = headers;
		ctx->header_count = ARRAY_SIZE(headers);
	}

	return 0;
}

/*
 * XXX: Zephyr could provide a send_http1_401 response to send a
 * canonical 401 header, but this still seems to work.
 */
static void set_unauth_response(struct http_client_ctx *client,
				struct http_response_ctx *ctx)
{
	/* XXX: should this also include allow header? */
	static struct http_header extra_headers[] = {
		{ .name = "www-authenticate", .value = "Basic realm=\"BMC\"" },
	};
	size_t header_count = client->header_capture_ctx.count;
	const struct http_header *headers = client->header_capture_ctx.headers;
	bool webui = false;

	for (unsigned int i = 0; i < header_count; i++) {
		if (!strcmp(headers[i].name, "x-auth-code") &&
		    !strcmp(headers[i].value, "webui")) {
			webui = true;
			break;
		}
	}

	ctx->status = HTTP_401_UNAUTHORIZED;
	ctx->final_chunk = true;
	if (!webui) {
		/*
		 * Special case hack for the JS request coming from the WebUI.
		 * If we return a www-authenticate header here, the browser
		 * intercepts it and pops up a standard HTTP login box, which
		 * is not what we want (the JS interprets the request and
		 * displays an authentication error).
		 */
		ctx->headers = extra_headers;
		ctx->header_count = ARRAY_SIZE(extra_headers);
	}
	ctx->body = NULL;
	ctx->body_len = 0;
}

static int redfish_handler(struct http_client_ctx *client,
			   enum http_transaction_status status,
			   const struct http_request_ctx *request_ctx,
			   struct http_response_ctx *response_ctx,
			   void *user_data,
			   bool require_auth,
			   int (*get_fn)(char *out_buf, size_t out_buf_len),
			   int (*patch_fn)(char *in_buf, size_t in_buf_len),
			   int (*post_fn)(char *in_buf, size_t in_buf_len))
{
	int ret;
	uint32_t allow_methods = 0;

	if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
		in_buffer_len = 0;
		return 0;
	}

	if (get_fn)
		allow_methods |= BIT(HTTP_GET);
	if (patch_fn)
		allow_methods |= BIT(HTTP_PATCH);
	if (post_fn)
		allow_methods |= BIT(HTTP_POST);

	if (validate_and_set_headers(client, response_ctx, allow_methods) < 0)
		return 0;

	if (require_auth && validate_auth(client) < 0) {
		LOG_ERR("Failed to authenticate");
		set_unauth_response(client, response_ctx);
		return 0;
	}

	if (client->method == HTTP_PATCH || client->method == HTTP_POST) {
		/* Accumulate requests into the in_buffer, until the final request. */
		if (request_ctx->data && request_ctx->data_len > 0) {
			if (in_buffer_len + request_ctx->data_len < sizeof(in_buffer)) {
				memcpy(in_buffer + in_buffer_len, request_ctx->data, request_ctx->data_len);
				in_buffer_len += request_ctx->data_len;
			} else {
				LOG_ERR("Payload too large");
				in_buffer_len = 0;
				response_ctx->status = HTTP_400_BAD_REQUEST;
				response_ctx->final_chunk = true;
				return 0;
			}
		}

		if (status != HTTP_SERVER_REQUEST_DATA_FINAL)
			return 0;

		in_buffer[in_buffer_len] = '\0';

		if (client->method == HTTP_PATCH)
			ret = patch_fn(in_buffer, in_buffer_len);
		else /* client->method == HTTP_POST */
			ret = post_fn(in_buffer, in_buffer_len);
		in_buffer_len = 0;
		if (ret < 0)
			response_ctx->status = ret;
		else
			response_ctx->status = HTTP_204_NO_CONTENT; /* 204 is success */

	} else if (client->method == HTTP_GET && get_fn) {
		/* No support for accumulating GET requests (they should be small) */
		if (status != HTTP_SERVER_REQUEST_DATA_FINAL)
			return 0;

		ret = get_fn(out_buffer, sizeof(out_buffer));
		if (ret < 0) {
			response_ctx->status = ret;
		} else {
			response_ctx->body = (uint8_t *)out_buffer;
			response_ctx->body_len = strlen(out_buffer);
			response_ctx->status = HTTP_200_OK;
		}
	} else {
		/* Should not get here (validate should have caught it) */
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
	}

	response_ctx->final_chunk = true;
	return 0;
}

#if defined(CONFIG_APP_HTTPS)
#define ALL_HTTP_RESOURCE_DEFINE(name, url, detail)					\
HTTP_RESOURCE_DEFINE(name##_http, http_service, url, detail);				\
HTTP_RESOURCE_DEFINE(name##_https, https_service, url, detail);
#else
#define ALL_HTTP_RESOURCE_DEFINE(name, url, detail)					\
HTTP_RESOURCE_DEFINE(name##_http, http_service, url, detail);
#endif

#define REDFISH_HANDLER(name, url, require_auth, get_handler, patch_handler, post_handler) \
static int name##_handler(struct http_client_ctx *client,				\
			  enum http_transaction_status status,				\
			  const struct http_request_ctx *request_ctx,			\
			  struct http_response_ctx *response_ctx,			\
			  void *user_data)						\
{											\
	return redfish_handler(client, status, request_ctx, response_ctx, user_data,	\
				require_auth,						\
				get_handler,						\
				patch_handler,						\
				post_handler);						\
}											\
static const struct http_resource_detail_dynamic name##_detail = {			\
	.common = {									\
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,					\
		.bitmask_of_supported_http_methods = -1U,				\
	},										\
	.cb = name##_handler,								\
	.user_data = NULL,								\
};											\
ALL_HTTP_RESOURCE_DEFINE(name, url, &name##_detail);

/*** Core Redfish types/containers ***/

/* Collection: Member nested object */
struct redfish_member {
	const char *odata_id;
};
static const struct json_obj_descr member_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_member, "@odata.id",
				  odata_id, JSON_TOK_STRING),
};

#define REDFISH_COLLECTION_MEMBERS_MAX 1

/* Collection of members */
struct redfish_collection {
	const char *odata_id;
	const char *odata_type;
	const char *name;
	int32_t members_count;
	size_t members_len;
	struct redfish_member members[REDFISH_COLLECTION_MEMBERS_MAX];
};
static const struct json_obj_descr collection_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_collection, "@odata.id",
				  odata_id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_collection, "@odata.type",
				  odata_type, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_collection, "Name",
				  name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_collection,
				  "Members@odata.count", members_count,
				  JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_OBJ_ARRAY_NAMED(struct redfish_collection,
				       "Members", members,
				       REDFISH_COLLECTION_MEMBERS_MAX, members_len,
				       member_descr, ARRAY_SIZE(member_descr)),
};

/* Link (nested by others) */
struct redfish_link {
	const char *odata_id;
};
static const struct json_obj_descr link_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_link, "@odata.id",
				  odata_id, JSON_TOK_STRING),
};


/*** /redfish/ ***/
struct redfish_version {
	const char *v1;
};
static const struct json_obj_descr version_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_version, "v1",
				  v1, JSON_TOK_STRING),
};

/* GET /redfish/ */
static int redfish_version_get_handler(char *out_buf, size_t out_buf_len)
{
	const struct redfish_version version = {
		.v1 = "/redfish/v1/"
	};

	return json_obj_encode_buf(version_descr, ARRAY_SIZE(version_descr),
				   &version, out_buf, out_buf_len);
}

REDFISH_HANDLER(redfish_version, "/redfish/",
		false, /* do not require auth */
		redfish_version_get_handler, NULL, NULL);
ALL_HTTP_RESOURCE_DEFINE(redfish_version_no_slash, "/redfish", &redfish_version_detail);

/*** /redfish/v1/ ***/
struct redfish_service_root {
	const char *odata_type;
	const char *odata_id;
	const char *id;
	const char *name;
	const char *redfish_version;
	const char *uuid;
	struct redfish_link account_service;
	struct redfish_link systems;
	struct redfish_link managers;
};
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
	JSON_OBJ_DESCR_OBJECT_NAMED(struct redfish_service_root, "AccountService",
				    account_service, link_descr),
	JSON_OBJ_DESCR_OBJECT_NAMED(struct redfish_service_root, "Managers",
				    managers, link_descr),
	JSON_OBJ_DESCR_OBJECT_NAMED(struct redfish_service_root, "Systems",
				    systems, link_descr),
};

/* GET /redfish/v1/ */
static int service_root_get_handler(char *out_buf, size_t out_buf_len)
{
	const struct redfish_service_root service_root = {
		.odata_type = "#ServiceRoot.v1_16_1.ServiceRoot",
		.odata_id = "/redfish/v1/",
		.id = "RootService",
		.name = "Root Service",
		.redfish_version = "1.15.0",
		.uuid = "92384634-2938-2342-8820-489239905423",
		.account_service = {
			.odata_id = "/redfish/v1/AccountService"
		},
		.managers = {
			.odata_id = "/redfish/v1/Managers"
		},
		.systems = {
			.odata_id = "/redfish/v1/Systems"
		},
	};
	int ret;

	ret = json_obj_encode_buf(service_root_descr, ARRAY_SIZE(service_root_descr),
				  &service_root, out_buf, out_buf_len);
	if (ret < 0) {
		LOG_ERR("Failed to encode service root: %d", ret);
		return HTTP_500_INTERNAL_SERVER_ERROR;
	}

	return 0;
}

REDFISH_HANDLER(service_root, "/redfish/v1/",
		false, /* do not require auth */
		service_root_get_handler, NULL, NULL);
ALL_HTTP_RESOURCE_DEFINE(service_root_no_slash, "/redfish/v1", &service_root_detail);


/*** /redfish/v1/odata ***/
struct redfish_odata_value {
	const char *name;
	const char *kind;
	const char *url;
};
static const struct json_obj_descr odata_value_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_odata_value, "name",
				  name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_odata_value, "kind",
				  kind, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_odata_value, "url",
				  url, JSON_TOK_STRING),
};

#define REDFISH_ODATA_VALUES_MAX 4

struct redfish_odata {
	const char *odata_context;
	size_t value_count;
	struct redfish_odata_value value[REDFISH_ODATA_VALUES_MAX];
};
static const struct json_obj_descr odata_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_odata, "@odata.context",
				  odata_context, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJ_ARRAY_NAMED(struct redfish_odata, "value", value,
				       REDFISH_ODATA_VALUES_MAX, value_count,
				       odata_value_descr, ARRAY_SIZE(odata_value_descr)),
};

/* GET /redfish/v1/odata */
static int odata_get_handler(char *out_buf, size_t out_buf_len)
{
	const struct redfish_odata odata = {
		.odata_context = "/redfish/v1/$metadata",
		.value_count = 4,
		.value = {
			{ .name = "Service", .kind = "Singleton", .url = "/redfish/v1/", },
			{ .name = "Systems", .kind = "Singleton", .url = "/redfish/v1/Systems", },
			{ .name = "Managers", .kind = "Singleton", .url = "/redfish/v1/Managers", },
			{ .name = "AccountService", .kind = "Singleton", .url = "/redfish/v1/AccountService", },
		},
	};
	int ret;

	ret = json_obj_encode_buf(odata_descr, ARRAY_SIZE(odata_descr),
				  &odata, out_buf, out_buf_len);
	if (ret < 0) {
		LOG_ERR("Failed to encode odata: %d", ret);
		return HTTP_500_INTERNAL_SERVER_ERROR;
	}

	return 0;
}

REDFISH_HANDLER(odata, "/redfish/v1/odata",
		false, /* do not require auth */
		odata_get_handler, NULL, NULL);

/*** /redfish/v1/AccountService ***/
struct redfish_account_service {
	const char *odata_id;
	const char *odata_type;
	const char *id;
	const char *name;
	struct redfish_link accounts;
};
static const struct json_obj_descr account_service_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_account_service, "@odata.id", odata_id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_account_service, "@odata.type", odata_type, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_account_service, "Id", id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_account_service, "Name", name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT_NAMED(struct redfish_account_service, "Accounts", accounts, link_descr),
};

/* GET /redfish/v1/AccountService */
static int account_service_get_handler(char *out_buf, size_t out_buf_len)
{
	const struct redfish_account_service account_service = {
		.odata_id = "/redfish/v1/AccountService",
		.odata_type = "#AccountService.v1_10_0.AccountService",
		.id = "AccountService",
		.name = "Account Service",
		.accounts = {
			.odata_id = "/redfish/v1/AccountService/Accounts",
		},
	};
	int ret;

	ret = json_obj_encode_buf(account_service_descr, ARRAY_SIZE(account_service_descr),
				  &account_service, out_buf, out_buf_len);
	if (ret < 0) {
		LOG_ERR("Failed to encode account service: %d", ret);
		return HTTP_500_INTERNAL_SERVER_ERROR;
	}

	return 0;
}

REDFISH_HANDLER(account_service, "/redfish/v1/AccountService",
		false, /* do not require auth */
		account_service_get_handler, NULL, NULL);

/* GET /redfish/v1/AccountService/Accounts */
static int accounts_collection_get_handler(char *out_buf, size_t out_buf_len)
{
	const struct redfish_collection accounts_collection = {
		.odata_id = "/redfish/v1/AccountService/Accounts",
		.odata_type = "#ManagerAccountCollection.ManagerAccountCollection",
		.name = "Accounts Collection",
		.members_count = 1,
		.members = {
			{
				.odata_id = "/redfish/v1/AccountService/Accounts/1",
			},
		},
		.members_len = 1,
	};
	int ret;

	ret = json_obj_encode_buf(collection_descr, ARRAY_SIZE(collection_descr),
				  &accounts_collection, out_buf, out_buf_len);
	if (ret < 0) {
		LOG_ERR("Failed to encode manager: %d", ret);
		return HTTP_500_INTERNAL_SERVER_ERROR;
	}

	return 0;
}

REDFISH_HANDLER(accounts_collection, "/redfish/v1/AccountService/Accounts",
		true, /* require auth */
		accounts_collection_get_handler, NULL, NULL);

/*** /redfish/v1/AccountService/Account/1 ***/
struct redfish_account {
	const char *odata_id;
	const char *odata_type;
	const char *id;
	const char *role_id;
	const char *name;
	const char *user_name;
	const char *password; // Write-only
};
static const struct json_obj_descr account_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_account, "@odata.id", odata_id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_account, "@odata.type", odata_type, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_account, "Id", id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_account, "RoleId", role_id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_account, "Name", name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_account, "UserName", user_name, JSON_TOK_STRING),
	// Password is usually not returned in GET, but we need descriptor for PATCH parsing
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_account, "Password", password, JSON_TOK_STRING),
};

/* PATCH /redfish/v1/AccountService/Account/1 */
static int account_patch_handler(char *in_buf, size_t in_buf_len)
{
	struct redfish_account payload;
	int ret;

	memset(&payload, 0, sizeof(payload));
	ret = json_obj_parse(in_buf, in_buf_len,
			     account_descr, ARRAY_SIZE(account_descr), &payload);
	if (ret < 0) {
		LOG_ERR("Account: Bad JSON (err=%d)", ret);
		return HTTP_400_BAD_REQUEST;
	}

	if (payload.password) {
		config_bmc_password_set(payload.password);
		LOG_INF("Admin Password Updated");
	}

	return 0;
}

/* GET /redfish/v1/AccountService/Account/1 */
static int account_get_handler(char *out_buf, size_t out_buf_len)
{
	const struct redfish_account account = {
		.odata_id = "/redfish/v1/AccountService/Accounts/1",
		.odata_type = "#ManagerAccount.v1_9_0.ManagerAccount",
		.id = "1",
		.name = "Administrator",
		.role_id = "Administrator",
		.user_name = "admin",
		.password = NULL,
		/*
		 * Zephyr's JSON parser turns NULL password into an empty string
		 * ("") instead of null. That causes redfish validation to complain,
		 * but it shouldn't be a big deal.
		 */
	};
	int ret;

	ret = json_obj_encode_buf(account_descr, ARRAY_SIZE(account_descr),
				  &account, out_buf, out_buf_len);
	if (ret < 0) {
		LOG_ERR("Failed to encode account: %d", ret);
		return HTTP_500_INTERNAL_SERVER_ERROR;
	}

	return 0;
}

REDFISH_HANDLER(account, "/redfish/v1/AccountService/Accounts/1",
		true, /* require auth */
		account_get_handler, account_patch_handler, NULL);

/*** /redfish/v1/Managers ***/
/* GET /redfish/v1/Managers */
static int managers_collection_get_handler(char *out_buf, size_t out_buf_len)
{
	const struct redfish_collection managers_collection = {
		.odata_id = "/redfish/v1/Managers",
		.odata_type = "#ManagerCollection.ManagerCollection",
		.name = "Manager Collection",
		.members_count = 1,
		.members = {
			{
				.odata_id = "/redfish/v1/Managers/bmc"
			}
		},
		.members_len = 1
	};
	int ret;

	ret = json_obj_encode_buf(collection_descr, ARRAY_SIZE(collection_descr),
				  &managers_collection, out_buf, out_buf_len);
	if (ret < 0) {
		LOG_ERR("Failed to encode managers collection: %d", ret);
		return HTTP_500_INTERNAL_SERVER_ERROR;
	}

	return 0;
}

REDFISH_HANDLER(managers_collection, "/redfish/v1/Managers",
		true, /* require auth */
		managers_collection_get_handler, NULL, NULL);

/*** /redfish/v1/Managers/bmc ***/
struct redfish_manager {
	const char *odata_id;
	const char *odata_type;
	const char *id;
	const char *name;
	const char *uuid;
	const char *date_time;
	struct redfish_link ethernet_interfaces;
};
static const struct json_obj_descr manager_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_manager, "@odata.id", odata_id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_manager, "@odata.type", odata_type, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_manager, "Id", id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_manager, "Name", name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_manager, "UUID", uuid, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_manager, "DateTime", date_time, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT_NAMED(struct redfish_manager, "EthernetInterfaces", ethernet_interfaces, link_descr),
};

static const char *get_iso_time(void)
{
	static char str[30];
	struct timespec ts;
	struct tm *tm_info;

	clock_gettime(CLOCK_REALTIME, &ts);
	tm_info = gmtime(&ts.tv_sec);
	strftime(str, sizeof(str), "%Y-%m-%dT%H:%M:%SZ", tm_info);

	return str;
}

/* PATCH /redfish/v1/Managers/bmc */
static int manager_patch_handler(char *in_buf, size_t in_buf_len)
{
	struct redfish_manager payload;
	int ret;

	ret = json_obj_parse(in_buf, in_buf_len, manager_descr, ARRAY_SIZE(manager_descr), &payload);
	if (ret < 0) {
		LOG_ERR("Manager: Bad JSON (err=%d)", ret);
		return HTTP_400_BAD_REQUEST;
	}

	if (payload.date_time) {
		ret = time_set_from_iso_str(payload.date_time);
		if (ret) {
			LOG_ERR("Failed to set date/time (err=%d)", ret);
			return HTTP_500_INTERNAL_SERVER_ERROR;
		}
	}

	return 0;
}

/* GET /redfish/v1/Managers/bmc */
static int manager_get_handler(char *out_buf, size_t out_buf_len)
{
	const struct redfish_manager manager = {
		.odata_id = "/redfish/v1/Managers/bmc",
		.odata_type = "#Manager.v1_11_0.Manager",
		.id = "bmc",
		.uuid = "58893887-8974-2487-2389-389233423423",
		.name = "WallaBMC",
		.date_time = get_iso_time(),
		.ethernet_interfaces = {
			.odata_id = "/redfish/v1/Managers/bmc/EthernetInterfaces",
		},
	};
	int ret;

	ret = json_obj_encode_buf(manager_descr, ARRAY_SIZE(manager_descr),
				  &manager, out_buf, out_buf_len);
	if (ret < 0) {
		LOG_ERR("Failed to encode manager: %d", ret);
		return HTTP_500_INTERNAL_SERVER_ERROR;
	}

	return 0;
}

REDFISH_HANDLER(manager, "/redfish/v1/Managers/bmc",
		true, /* require auth */
		manager_get_handler, manager_patch_handler, NULL);

/*** /redfish/v1/Managers/bmc/EthernetInterfaces ***/
/* GET /redfish/v1/Managers/bmc/EthernetInterfaces */
static int ethernet_collection_get_handler(char *out_buf, size_t out_buf_len)
{
	const struct redfish_collection ethernet_collection = {
		.odata_id = "/redfish/v1/Managers/bmc/EthernetInterfaces",
		.odata_type = "#EthernetInterfaceCollection.EthernetInterfaceCollection",
		.name = "Ethernet Interface Collection",
		.members_count = 1,
		.members = {
			{
				.odata_id = "/redfish/v1/Managers/bmc/EthernetInterfaces/eth0"
			}
		},
		.members_len = 1
	};
	int ret;

	ret = json_obj_encode_buf(collection_descr, ARRAY_SIZE(collection_descr),
				  &ethernet_collection, out_buf, out_buf_len);
	if (ret < 0) {
		LOG_ERR("Failed to encode ethernet interfaces collection: %d", ret);
		return HTTP_500_INTERNAL_SERVER_ERROR;
	}

	return 0;
}

REDFISH_HANDLER(ethernet_interfaces, "/redfish/v1/Managers/bmc/EthernetInterfaces",
		true, /* require auth */
		ethernet_collection_get_handler, NULL, NULL);

/*** /redfish/v1/Managers/bmc/EthernetInterfaces/eth0 ***/
/* DHCPv4 */
struct redfish_dhcp_v4 {
	bool dhcp_enabled;
};
static const struct json_obj_descr dhcp_v4_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_dhcp_v4, "DHCPEnabled", dhcp_enabled, JSON_TOK_TRUE),
};

/* IPv4Address */
struct redfish_ipv4_addr {
	const char *address;
	const char *subnet_mask;
	const char *gateway;
	const char *address_origin;
};
static const struct json_obj_descr ipv4_addr_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_ipv4_addr, "Address", address, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_ipv4_addr, "SubnetMask", subnet_mask, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_ipv4_addr, "Gateway", gateway, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_ipv4_addr, "AddressOrigin", address_origin, JSON_TOK_STRING),
};

/* EthInterface */
struct redfish_ethernet_interface {
	const char *odata_id;
	const char *odata_type;
	const char *id;
	const char *name;
	const char *host_name;
	struct redfish_dhcp_v4 dhcp_v4;
	struct redfish_ipv4_addr ipv4_addresses[1];
	size_t ipv4_count;
	struct redfish_ipv4_addr ipv4_static_addresses[1];
	size_t ipv4_static_count;
};
static const struct json_obj_descr ethernet_interface_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_ethernet_interface, "@odata.id", odata_id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_ethernet_interface, "@odata.type", odata_type, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_ethernet_interface, "Id", id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_ethernet_interface, "Name", name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_ethernet_interface, "HostName", host_name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT_NAMED(struct redfish_ethernet_interface, "DHCPv4", dhcp_v4, dhcp_v4_descr),
	JSON_OBJ_DESCR_OBJ_ARRAY_NAMED(struct redfish_ethernet_interface, "IPv4Addresses", ipv4_addresses,
		       1, ipv4_count, ipv4_addr_descr, ARRAY_SIZE(ipv4_addr_descr)),
	JSON_OBJ_DESCR_OBJ_ARRAY_NAMED(struct redfish_ethernet_interface, "IPv4StaticAddresses", ipv4_static_addresses,
		       1, ipv4_static_count, ipv4_addr_descr, ARRAY_SIZE(ipv4_addr_descr)),
};

/* PATCH /redfish/v1/Managers/bmc/EthernetInterfaces/eth0 */
static int ethernet_patch_handler(char *in_buf, size_t in_buf_len)
{
	struct redfish_ethernet_interface payload;
	int ret;
	bool poison_bool;

	memset(&payload, 0, sizeof(payload));
	memset(&payload.dhcp_v4.dhcp_enabled, 0xe1, sizeof(bool)); /* poison */
	payload.ipv4_static_count = -1;
	ret = json_obj_parse(in_buf, in_buf_len, ethernet_interface_descr,
			     ARRAY_SIZE(ethernet_interface_descr), &payload);
	if (ret < 0) {
		LOG_ERR("eth0: Bad JSON (err=%d)", ret);
		return HTTP_400_BAD_REQUEST;
	}

	if (payload.host_name) {
		ret = config_bmc_hostname_set(payload.host_name);
		if (ret) {
			LOG_ERR("Failed to set hostname (err=%d)", ret);
			return HTTP_500_INTERNAL_SERVER_ERROR;
		}
	}

	memset(&poison_bool, 0xe1, sizeof(poison_bool));
	if (memcmp(&payload.dhcp_v4.dhcp_enabled, &poison_bool, sizeof(bool)))
		config_bmc_use_dhcp4_set(payload.dhcp_v4.dhcp_enabled);

	if (payload.ipv4_static_count != -1) {
		struct redfish_ipv4_addr *redfish_static_addr = &payload.ipv4_static_addresses[0];

		/* If count is 0 then the addresses will be 0 (which clears the static IP) */
		ret = config_bmc_default_ip4_set(redfish_static_addr->address);
		if (ret) {
			LOG_ERR("Failed to set static IP address (err=%d)", ret);
			return HTTP_500_INTERNAL_SERVER_ERROR;
		}

		ret = config_bmc_default_ip4_nm_set(redfish_static_addr->subnet_mask);
		if (ret) {
			LOG_ERR("Failed to set static IP subnet mask address (err=%d)", ret);
			return HTTP_500_INTERNAL_SERVER_ERROR;
		}

		ret = config_bmc_default_ip4_gw_set(redfish_static_addr->gateway);
		if (ret) {
			LOG_ERR("Failed to set static IP gateway address (err=%d)", ret);
			return HTTP_500_INTERNAL_SERVER_ERROR;
		}
	}

	return 0;
}

/* GET /redfish/v1/Managers/bmc/EthernetInterfaces/eth0 */
static int ethernet_get_handler(char *out_buf, size_t out_buf_len)
{
	struct redfish_ethernet_interface ethernet_interface = {
		.odata_id = "/redfish/v1/Managers/bmc/EthernetInterfaces/eth0",
		.odata_type = "#EthernetInterface.v1_5_0.EthernetInterface",
		.id = "eth0",
		.name = "BMC Ethernet Interface",
		.host_name = net_hostname_get(),
		.dhcp_v4 = { .dhcp_enabled = config_bmc_use_dhcp4(), },
		.ipv4_count = 0,
		.ipv4_static_count = 0,
		.ipv4_static_addresses = {
			{
				.address = "0.0.0.0",
				.subnet_mask = "0.0.0.0",
				.gateway = "0.0.0.0",
			},
		},
	};
	int ret;

	struct net_if *iface = net_if_get_default();
	char ip_str[NET_IPV4_ADDR_LEN];
	char nm_str[NET_IPV4_ADDR_LEN];
	char gw_str[NET_IPV4_ADDR_LEN];
	if (iface) {
		struct redfish_ipv4_addr *redfish_addr = &ethernet_interface.ipv4_addresses[0];
		const struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;

		for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
			const struct net_if_addr_ipv4 *addr = &ipv4->unicast[i];

			if (addr->ipv4.is_used) {
				ethernet_interface.ipv4_count = 1;
				net_addr_ntop(AF_INET, &addr->ipv4.address.in_addr, ip_str, sizeof(ip_str));
				redfish_addr->address = ip_str;
				net_addr_ntop(AF_INET, &addr->netmask, nm_str, sizeof(nm_str));
				redfish_addr->subnet_mask = nm_str;
				if (addr->ipv4.addr_type == NET_ADDR_DHCP)
					redfish_addr->address_origin = "DHCP";
				else if (addr->ipv4.addr_type == NET_ADDR_MANUAL ||
					 addr->ipv4.addr_type == NET_ADDR_OVERRIDABLE)
					redfish_addr->address_origin = "Static";
				else
					redfish_addr->address_origin = "Unknown";

				if (addr->ipv4.addr_state == NET_ADDR_PREFERRED)
					break;
			}
		}

		if (ethernet_interface.ipv4_count == 1) {
			const struct net_in_addr gw_addr = net_if_ipv4_get_gw(iface);

			if (gw_addr.s_addr) {
				net_addr_ntop(AF_INET, &gw_addr, gw_str, sizeof(gw_str));
				redfish_addr->gateway = gw_str;
			} else {
				redfish_addr->gateway = "0.0.0.0";
			}
		}
	}

	if (config_bmc_default_ip4()) {
		struct redfish_ipv4_addr *redfish_static_addr = &ethernet_interface.ipv4_static_addresses[0];
		char static_ip_str[NET_IPV4_ADDR_LEN];
		char static_nm_str[NET_IPV4_ADDR_LEN];
		char static_gw_str[NET_IPV4_ADDR_LEN];
		uint32_t addr;

		ethernet_interface.ipv4_static_count = 1,

		addr = config_bmc_default_ip4();
		net_addr_ntop(AF_INET, &addr, static_ip_str, sizeof(static_ip_str));
		redfish_static_addr->address = static_ip_str;

		addr = config_bmc_default_ip4_nm();
		net_addr_ntop(AF_INET, &addr, static_nm_str, sizeof(static_nm_str));
		redfish_static_addr->subnet_mask = static_nm_str;

		addr = config_bmc_default_ip4_gw();
		net_addr_ntop(AF_INET, &addr, static_gw_str, sizeof(static_gw_str));
		redfish_static_addr->gateway = static_gw_str;
	}

	ret = json_obj_encode_buf(ethernet_interface_descr, ARRAY_SIZE(ethernet_interface_descr),
				  &ethernet_interface, out_buf, out_buf_len);
	if (ret < 0) {
		LOG_ERR("Failed to encode ethernet interface: %d", ret);
		return HTTP_500_INTERNAL_SERVER_ERROR;
	}

	return 0;
}

REDFISH_HANDLER(ethernet, "/redfish/v1/Managers/bmc/EthernetInterfaces/eth0",
		true, /* require auth */
		ethernet_get_handler, ethernet_patch_handler, NULL);

/*** /redfish/v1/Managers/bmc/NetworkProtocol ***/
struct redfish_ntp {
	bool protocol_enabled;
	const char *ntp_servers[1];
	size_t ntp_servers_count;
};
static const struct json_obj_descr ntp_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_ntp , "ProtocolEnabled", protocol_enabled, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_ARRAY_NAMED(struct redfish_ntp , "NTPServers", ntp_servers, 1, ntp_servers_count, JSON_TOK_STRING),
};

/* NetworkProtocol */
struct redfish_network_protocol {
	const char *odata_type;
	const char *id;
	struct redfish_ntp ntp;
};
static const struct json_obj_descr network_protocol_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_network_protocol, "@odata.type", odata_type, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_network_protocol, "Id", id, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT_NAMED(struct redfish_network_protocol, "NTP", ntp, ntp_descr),
};

/* PATCH /redfish/v1/Managers/bmc/NetworkProtocol */
static int network_protocol_patch_handler(char *in_buf, size_t in_buf_len)
{
	struct redfish_network_protocol payload;
	int ret;
	bool poison_bool;

	memset(&payload, 0, sizeof(payload));
	memset(&payload.ntp.protocol_enabled, 0xe1, sizeof(bool)); /* poison */
	payload.ntp.ntp_servers_count = -1;
	ret = json_obj_parse(in_buf, in_buf_len,
			     network_protocol_descr, ARRAY_SIZE(network_protocol_descr), &payload);
	if (ret < 0) {
		LOG_ERR("NetworkProtocol: Bad JSON (err=%d)", ret);
		return HTTP_400_BAD_REQUEST;
	}

	if (payload.ntp.ntp_servers_count != -1) {
		/* If count is 0 then the address will be 0 (which clears the static IP) */
		ret = config_bmc_ntp_server_set(payload.ntp.ntp_servers[0]);
		if (ret) {
			LOG_ERR("Failed to set NTP server address (err=%d)", ret);
			return HTTP_500_INTERNAL_SERVER_ERROR;
		}
	}

	memset(&poison_bool, 0xe1, sizeof(poison_bool));
	if (memcmp(&payload.ntp.protocol_enabled, &poison_bool, sizeof(bool)))
		config_bmc_use_ntp_set(payload.ntp.protocol_enabled);

	return 0;
}

/* GET /redfish/v1/Managers/bmc/NetworkProtocol */
static int network_protocol_get_handler(char *out_buf, size_t out_buf_len)
{
	struct redfish_network_protocol network_protocol = {
		.odata_type = "#ManagerNetworkProtocol.v1_9_0.ManagerNetworkProtocol",
		.id = "NetworkProtocol",
		.ntp = {
			.protocol_enabled = config_bmc_use_ntp(),
		},
	};
	int ret;

	if (config_bmc_ntp_server()) {
		network_protocol.ntp.ntp_servers[0] = config_bmc_ntp_server();
		network_protocol.ntp.ntp_servers_count = 1;
	}

	ret = json_obj_encode_buf(network_protocol_descr, ARRAY_SIZE(network_protocol_descr),
				  &network_protocol, out_buf, out_buf_len);
	if (ret < 0) {
		LOG_ERR("Failed to encode network protocol: %d", ret);
		return HTTP_500_INTERNAL_SERVER_ERROR;
	}

	return 0;
}

REDFISH_HANDLER(network_protocol, "/redfish/v1/Managers/bmc/NetworkProtocol",
		true, /* require auth */
		network_protocol_get_handler, network_protocol_patch_handler, NULL);

/*** /redfish/v1/Systems ***/
/* GET /redfish/v1/Systems */
static int systems_collection_get_handler(char *out_buf, size_t out_buf_len)
{
	const struct redfish_collection systems_collection = {
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
	int ret;

	ret = json_obj_encode_buf(collection_descr, ARRAY_SIZE(collection_descr),
				  &systems_collection, out_buf, out_buf_len);
	if (ret < 0) {
		LOG_ERR("Failed to encode systems collection: %d", ret);
		return HTTP_500_INTERNAL_SERVER_ERROR;
	}

	return 0;
}

REDFISH_HANDLER(systems_collection, "/redfish/v1/Systems",
		true, /* require auth */
		systems_collection_get_handler, NULL, NULL);

/*** /redfish/v1/Systems/system ***/
static char serial_number[] = "12345";

/* System Info: ResetType array */
struct redfish_reset_action {
	const char *target;
	const char *reset_type_values[3];
	size_t reset_type_values_len;
};
static const struct json_obj_descr reset_action_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct redfish_reset_action, target, JSON_TOK_STRING),
	JSON_OBJ_DESCR_ARRAY_NAMED(struct redfish_reset_action,
				   "ResetType@Redfish.AllowableValues",
				   reset_type_values, 3, reset_type_values_len,
				   JSON_TOK_STRING),
};

/* System Info: Actions nested object */
struct redfish_actions {
	struct redfish_reset_action reset_action;
};
static const struct json_obj_descr actions_descr[] = {
	JSON_OBJ_DESCR_OBJECT_NAMED(struct redfish_actions, "#ComputerSystem.Reset",
				    reset_action, reset_action_descr),
};

/* System Info: ProcessorSummary nested object */
struct redfish_processor_summary {
	const char *odata_type;
	int32_t count;
	const char *model;
};
static const struct json_obj_descr processor_summary_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_processor_summary, "Count",
				  count, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_processor_summary, "Model",
				  model, JSON_TOK_STRING),
};

/* System Info: MemorySummary nested object */
struct redfish_memory_summary {
	int32_t total_system_GiB;
};
static const struct json_obj_descr memory_summary_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_memory_summary, "TotalSystemMemoryGiB",
				  total_system_GiB, JSON_TOK_NUMBER),
};

struct redfish_computer_system {
	const char *odata_id;
	const char *odata_type;
	const char *id;
	const char *uuid;
	const char *name;
	const char *manufacturer;
	const char *model;
	const char *host_name;
	const char *power_restore_policy;
	const char *power_state;
	const char *serial_number;
	struct redfish_processor_summary processor_summary;
	struct redfish_memory_summary memory_summary;
	struct redfish_actions actions;
};
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
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_computer_system, "PowerRestorePolicy",
				  power_restore_policy, JSON_TOK_STRING),
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

/* PATCH /redfish/v1/Systems/system */
static int system_patch_handler(char *in_buf, size_t in_buf_len)
{
	struct redfish_computer_system payload;
	int ret;

	memset(&payload, 0, sizeof(payload));
	ret = json_obj_parse(in_buf, in_buf_len, computer_system_descr,
			     ARRAY_SIZE(computer_system_descr), &payload);
	if (ret < 0) {
		LOG_ERR("System: Bad JSON (err=%d)", ret);
		return HTTP_400_BAD_REQUEST;
	}

	if (payload.power_restore_policy) {
		if (!strcmp(payload.power_restore_policy, "AlwaysOn")) {
			ret = config_host_auto_poweron_set(true);
		} else if (!strcmp(payload.power_restore_policy, "AlwaysOff")) {
			ret = config_host_auto_poweron_set(false);
		} else {
			LOG_ERR("System: Bad power restore value");
			return HTTP_400_BAD_REQUEST;
		}
		if (ret) {
			LOG_ERR("Failed to set host auto-poweron(err=%d)", ret);
			return HTTP_500_INTERNAL_SERVER_ERROR;
		}
	}

	return 0;
}

/* GET /redfish/v1/Systems/system */
static int system_get_handler(char *out_buf, size_t out_buf_len)
{
	const struct redfish_computer_system computer_system = {
		.odata_id = "/redfish/v1/Systems/system",
		.odata_type = "#ComputerSystem.v1_22_0.ComputerSystem",
		.id = "system",
		.uuid = "38947555-7742-3448-3784-823347823834",
		.name = CONFIG_REDFISH_SYSTEM_PRODUCT_NAME,
		.manufacturer = CONFIG_REDFISH_SYSTEM_MANUFACTURER,
		.model = CONFIG_REDFISH_SYSTEM_MODEL,
		.processor_summary = {
			.odata_type = "#ProcessorSummary.v1_4_0.ProcessorSummary",
			.count = CONFIG_REDFISH_SYSTEM_PROCESSOR_COUNT,
			.model = CONFIG_REDFISH_SYSTEM_PROCESSOR_MODEL,
		},
		.memory_summary = {
			.total_system_GiB = CONFIG_REDFISH_SYSTEM_MEMORY_GIB,
		},
		.serial_number = serial_number,
		.power_restore_policy = config_host_auto_poweron() ? "AlwaysOn" : "AlwaysOff",
		.power_state = power_get_state() ? "On" : "Off",
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
	int ret;

	ret = json_obj_encode_buf(computer_system_descr, ARRAY_SIZE(computer_system_descr),
				       &computer_system, out_buf, out_buf_len);
	if (ret < 0) {
		LOG_ERR("Failed to encode computer system: %d", ret);
		return HTTP_500_INTERNAL_SERVER_ERROR;
	}

	return 0;
}

REDFISH_HANDLER(system, "/redfish/v1/Systems/system",
		true, /* require auth */
		system_get_handler, system_patch_handler, NULL);

struct redfish_reset_payload {
	const char *reset_type;
};
static const struct json_obj_descr reset_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct redfish_reset_payload, "ResetType",
				  reset_type, JSON_TOK_STRING)
};

/* POST /redfish/v1/Systems/system/Actions/ComputerSystem.Reset */
static int system_reset_post_handler(char *in_buf, size_t in_buf_len)
{
	struct redfish_reset_payload payload;
	int ret;

	memset(&payload, 0, sizeof(payload));
	ret = json_obj_parse(in_buf, in_buf_len, reset_descr, ARRAY_SIZE(reset_descr), &payload);
	if (ret < 0) {
		LOG_ERR("ComputerSystem.Reset: Bad JSON (err=%d)", ret);
		return HTTP_400_BAD_REQUEST;
	}

	LOG_INF("Reset Action: %s", payload.reset_type);

	if (strcmp(payload.reset_type, "On") == 0) {
		power_set_state(true);
	} else if (strcmp(payload.reset_type, "ForceOff") == 0) {
		power_set_state(false);
	} else if (strcmp(payload.reset_type, "PowerCycle") == 0) {
		power_reset();
	} else {
		LOG_ERR("ComputerSystem.Reset: Bad reset type");
		return HTTP_400_BAD_REQUEST;
	}

	return 0;
}

REDFISH_HANDLER(system_reset, "/redfish/v1/Systems/system/Actions/ComputerSystem.Reset",
		true, /* require auth */
		NULL, NULL, system_reset_post_handler);

/*** /redfish/v1/$metadata ***/
static const uint8_t redfish_metadata_xml_gz[] = {
#include "redfish_metadata.xml.gz.inc"
};

static struct http_resource_detail_static redfish_metadata_xml_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "application/xml",
		},
	.static_data = redfish_metadata_xml_gz,
	.static_data_len = sizeof(redfish_metadata_xml_gz),
};

HTTP_RESOURCE_DEFINE(redfish_metadata_xml_gz_resource, http_service,
		"/redfish/v1/$metadata", &redfish_metadata_xml_gz_resource_detail);
#if defined(CONFIG_APP_HTTPS)
HTTP_RESOURCE_DEFINE(redfish_metadata_xml_gz_resource_https, https_service,
		"/redfish/v1/$metadata", &redfish_metadata_xml_gz_resource_detail);
#endif /* defined(CONFIG_APP_HTTPS) */
