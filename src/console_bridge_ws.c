/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(host_console_ws, LOG_LEVEL_INF);

#include <zephyr/posix/fcntl.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/socket_service.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/websocket.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>

#include "console_logger.h"

/* WebSocket buffer size */
#define WS_RX_BUF_SIZE 256

struct host_websocket {
        /** Array for sockets used by the websocket service. */
        struct zsock_pollfd fds[1];
	bool new_client;
};

static struct host_websocket host_websocket;

static K_SEM_DEFINE(client_sem, 0, 1);

static void ws_server_cb(struct net_socket_service_event *evt);

NET_SOCKET_SERVICE_SYNC_DEFINE_STATIC(host_console_ws_server, ws_server_cb, 1);

static void ws_end_client_connection(struct host_websocket *ws)
{
	int ret;

	LOG_INF("Closing connection to #%d", ws->fds[0].fd);

	(void)websocket_unregister(ws->fds[0].fd);

	ws->fds[0].fd = -1;

	// k_work_cancel_delayable_sync(&ws->send_work, &ws->work_sync);

	ret = net_socket_service_register(&host_console_ws_server, ws->fds,
					  ARRAY_SIZE(ws->fds), NULL);
	if (ret < 0) {
		LOG_ERR("Failed to re-register socket service (%d)", ret);
	}
}

static ssize_t ws_send(struct host_websocket *ws, const void *buf, size_t size, bool block)
{
	size_t copied = 0;
	ssize_t ret;

	if (ws->fds[0].fd < 0) {
		return -ENOTCONN;
	}

	while (copied < size) {
		ret = zsock_send(ws->fds[0].fd, (uint8_t *)buf + copied, size - copied,
				 block ? 0 : ZSOCK_MSG_DONTWAIT);
		if (ret < 0) {
			if (errno == EAGAIN && !block)
				return copied;
			if (copied)
				ret = copied;
			else
				ret = -errno;
			LOG_ERR("Failed to send (err %zd), shutting down", ret);
			ws_end_client_connection(ws);
			return ret;
		}

		copied += ret;
	}

	return 0;
}

static ssize_t ws_recv(struct host_websocket *ws, void *buf, size_t size, bool block)
{
	ssize_t ret;

	ret = zsock_recv(ws->fds[0].fd, buf, size,
				 block ? 0 : ZSOCK_MSG_DONTWAIT);
	if (ret < 0) {
		LOG_DBG("Websocket client error %d", ret);
		if (errno == EAGAIN && !block)
			return -EAGAIN;
		ws_end_client_connection(ws);
		return -errno;
	} else if (ret == 0) {
		LOG_DBG("Websocket client closed connection");
		ws_end_client_connection(ws);
		return 0;
	}

	return ret;
}

#define CONSOLE_BRIDGE_STACK_SIZE K_THREAD_STACK_SIZEOF(socket_send_thread_stack_area)
#define CONSOLE_BRIDGE_PRIORITY   CONFIG_CONSOLE_BRIDGE_PRIORITY

static K_THREAD_STACK_DEFINE(socket_send_thread_stack_area, 4096); /* XXX: config */
static struct k_thread socket_send_thread_data;

static void socket_send_thread(void *a, void *b, void *c)
{
	struct host_websocket *ws = &host_websocket;
	uint64_t pos = 0;
	uint8_t buf[64];

	while (1) {
		ssize_t nr, ret;
		size_t copied;

		if (ws->fds[0].fd == -1 || ws->new_client) {
			k_sem_take(&client_sem, K_FOREVER);
			pos = 0;
			ws->new_client = false;
			continue;
		}

		/* Don't wait forever so we poll for a new client */
		ret = host_console_read(buf, sizeof(buf), &pos, K_MSEC(1000));
		if (ret <= 0)
			continue;

		nr = ret;
		copied = 0;
again:
		if (ws->fds[0].fd == -1)
			continue;

		ret = ws_send(ws, buf + copied, nr - copied, true);
		if (ret < 0) {
			LOG_WRN("Socket send error: %d ret: %zd", errno, ret);
			ws_end_client_connection(ws);
			continue;
		}

		copied += ret;
		if (ret < copied) {
			LOG_WRN("Socket send short: %d ret: %zd", errno, ret);
			k_msleep(100);
			goto again;
		}
	}
}

static uint8_t ws_recv_buf[256];

static void ws_server_cb(struct net_socket_service_event *evt)
{
	net_socklen_t optlen = sizeof(int);
	struct host_websocket *ws;
	int sock_error;

	ws = (struct host_websocket *)evt->user_data;

	if ((evt->event.revents & ZSOCK_POLLERR) ||
	    (evt->event.revents & ZSOCK_POLLNVAL)) {
		(void)zsock_getsockopt(evt->event.fd, ZSOCK_SOL_SOCKET,
				       ZSOCK_SO_ERROR, &sock_error, &optlen);
		LOG_ERR("Websocket socket %d error (%d)", evt->event.fd, sock_error);

		if (evt->event.fd == ws->fds[0].fd) {
			return ws_end_client_connection(ws);
		}

		return;
	}

	if (!(evt->event.revents & ZSOCK_POLLIN)) {
		return;
	}

	if (evt->event.fd == ws->fds[0].fd) {
		ssize_t ret;

		ret = ws_recv(ws, ws_recv_buf, sizeof(ws_recv_buf), false);
		if (ret <= 0)
			return;

		ret = host_console_write(ws_recv_buf, ret);
	}
}

static int console_ws_http_cb(int ws_socket, struct http_request_ctx *request_ctx, void *user_data)
{
	struct host_websocket *ctx = user_data;
	int ret;

	if (ws_socket < 0) {
		LOG_ERR("Invalid socket %d", ws_socket);
		return -EBADF;
	}

	if (ctx->fds[0].fd >= 0) {
		/* There is already a websocket connection to this shell,
		 * kick the previous connection out.
		 */
		ws_end_client_connection(ctx);
	}

	ctx->new_client = true;
	ctx->fds[0].fd = ws_socket;
	ctx->fds[0].events = ZSOCK_POLLIN;

	ret = net_socket_service_register(&host_console_ws_server, ctx->fds,
					  ARRAY_SIZE(ctx->fds), ctx);
	if (ret < 0) {
		LOG_ERR("Failed to register socket service, %d", ret);
		goto error;
	}

	LOG_INF("%s connected client", __func__);
	k_sem_give(&client_sem);

	return 0;

error:
	if (ctx->fds[0].fd >= 0) {
		(void)zsock_close(ctx->fds[0].fd);
		ctx->fds[0].fd = -1;
	}

	return ret;
}

static uint8_t ws_recv_buf_console_service[256];
struct http_resource_detail_websocket ws_res_detail_console_service = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_WEBSOCKET,

		/* We need HTTP/1.1 GET method for upgrading */
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = console_ws_http_cb,
	.data_buffer = ws_recv_buf_console_service,
	.data_buffer_len = sizeof(ws_recv_buf_console_service),
	.user_data = &host_websocket,
};
HTTP_RESOURCE_DEFINE(host_console_ws_http, http_service,
		     "/console/host", &ws_res_detail_console_service);
#if defined(CONFIG_APP_HTTPS)
HTTP_RESOURCE_DEFINE(host_console_ws_https, https_service,
		     "/console/host", &ws_res_detail_console_service);
#endif

int console_bridge_ws_init(void)
{
	LOG_INF("Starting host console websocket");

	memset(&host_websocket, 0, sizeof(host_websocket));
	host_websocket.fds[0].fd = -1;

	k_thread_create(&socket_send_thread_data, socket_send_thread_stack_area,
			CONSOLE_BRIDGE_STACK_SIZE,
			socket_send_thread,
			NULL, NULL, NULL,
			CONSOLE_BRIDGE_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(&socket_send_thread_data, "websocket_send");

	return 0;
}

