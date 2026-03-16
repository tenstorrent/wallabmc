/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(host_comms, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "host.h"
#include "power.h"

/* UART device for host communication */
#define HOST_UART_NODE DT_ALIAS(host_message)
#if DT_NODE_EXISTS(HOST_UART_NODE)
static const struct device *host_uart = DEVICE_DT_GET(HOST_UART_NODE);
#else
static const struct device *host_uart = NULL;
#endif

/* Thread configuration */
#define HOST_THREAD_STACK_SIZE 2048
#define HOST_THREAD_PRIORITY K_PRIO_PREEMPT(5)

K_THREAD_STACK_DEFINE(host_thread_stack, HOST_THREAD_STACK_SIZE);
static struct k_thread host_thread_data;

/* Calculate checksum for message */
static uint8_t calculate_checksum(Message *msg)
{
	uint8_t checksum = 0;
	uint8_t *data = (uint8_t *)msg;
	size_t len = offsetof(Message, checksum);

	for (size_t i = 0; i < len; i++) {
		checksum ^= data[i];
	}

	return checksum;
}

/* Send message to host */
static int send_message(Message *msg)
{
	if (host_uart == NULL || !device_is_ready(host_uart)) {
		LOG_ERR("Host UART not ready");
		return -ENODEV;
	}

	/* Calculate and set checksum */
	msg->checksum = calculate_checksum(msg);

	/* Send entire message */
	size_t msg_size = sizeof(Message);
	const uint8_t *data = (const uint8_t *)msg;

	for (size_t i = 0; i < msg_size; i++) {
		uart_poll_out(host_uart, data[i]);
	}

	return 0;
}

/* Receive message from host */
static int receive_message(Message *msg)
{
	if (host_uart == NULL || !device_is_ready(host_uart)) {
		return -ENODEV;
	}

	uint8_t *data = (uint8_t *)msg;
	size_t msg_size = sizeof(Message);
	int timeout_ms = 100;
	uint8_t byte;
	int64_t start_time;

	/* Wait for frame header - look for 0x55 (first byte on little-endian) */
	start_time = k_uptime_get();
	while (k_uptime_get() - start_time < timeout_ms) {
		if (uart_poll_in(host_uart, &byte) == 0) {
			if (byte == 0x55) {
				/* Found potential start, read next 3 bytes */
				data[0] = byte;
				for (int i = 1; i < 4; i++) {
					start_time = k_uptime_get();
					while (k_uptime_get() - start_time < timeout_ms) {
						if (uart_poll_in(host_uart, &data[i]) == 0) {
							break;
						}
						k_msleep(1);
					}
					if (k_uptime_get() - start_time >= timeout_ms) {
						return -ETIMEDOUT;
					}
				}
				/* Verify complete header: 0xA55AAA55 on little-endian = [0x55, 0xAA, 0x5A, 0xA5] */
				if (msg->header == FRAME_HEADER) {
					break;
				}
			}
		}
		k_msleep(1);
	}

	if (msg->header != FRAME_HEADER) {
		return -ETIMEDOUT;
	}

	/* Receive rest of message */
	for (size_t i = 4; i < msg_size; i++) {
		start_time = k_uptime_get();
		while (k_uptime_get() - start_time < timeout_ms) {
			if (uart_poll_in(host_uart, &data[i]) == 0) {
				break;
			}
			k_msleep(1);
		}
		if (k_uptime_get() - start_time >= timeout_ms) {
			return -ETIMEDOUT;
		}
	}

	/* Validate message */
	if (msg->tail != FRAME_TAIL) {
		LOG_ERR("Invalid frame tail: 0x%08x", msg->tail);
		return -EINVAL;
	}

	uint8_t calculated_checksum = calculate_checksum(msg);
	if (msg->checksum != calculated_checksum) {
		LOG_ERR("Checksum mismatch: got 0x%02x, expected 0x%02x",
			msg->checksum, calculated_checksum);
		return -EINVAL;
	}

	if (msg->data_len > FRAME_DATA_MAX) {
		LOG_ERR("Invalid data length: %u", msg->data_len);
		return -EINVAL;
	}

	return 0;
}

/* Handle CMD_POWER_OFF */
static int handle_power_off(Message *req, Message *resp)
{
	LOG_INF("CMD_POWER_OFF");
	int ret = power_set_state(false);

	resp->cmd_result = (ret == 0) ? CMD_RESULT_SUCCESS : CMD_RESULT_ERROR;
	resp->data_len = 0;

	return ret;
}

/* Handle CMD_REBOOT */
static int handle_reboot(Message *req, Message *resp)
{
	LOG_INF("CMD_REBOOT");
	int ret = power_reset();

	resp->cmd_result = (ret == 0) ? CMD_RESULT_SUCCESS : CMD_RESULT_ERROR;
	resp->data_len = 0;

	return ret;
}

/* Handle CMD_READ_BOARD_INFO */
static int handle_read_board_info(Message *req, Message *resp)
{
	LOG_INF("CMD_READ_BOARD_INFO");

	/* TODO: Implement board info reading */
	const char *info = "STM32 BMC";
	size_t len = strlen(info);

	if (len > FRAME_DATA_MAX) {
		len = FRAME_DATA_MAX;
	}

	memcpy(resp->data, info, len);
	resp->data_len = len;
	resp->cmd_result = CMD_RESULT_SUCCESS;

	return 0;
}

/* Handle CMD_CONTROL_LED */
static int handle_control_led(Message *req, Message *resp)
{
	LOG_INF("CMD_CONTROL_LED");

	/* TODO: Implement LED control based on req->data */
	/* For now, just acknowledge */
	resp->cmd_result = CMD_RESULT_SUCCESS;
	resp->data_len = 0;

	return 0;
}

/* Handle CMD_PVT_INFO */
static int handle_pvt_info(Message *req, Message *resp)
{
	LOG_INF("CMD_PVT_INFO");

	/* TODO: Implement PVT (Process, Voltage, Temperature) info reading */
	resp->cmd_result = CMD_RESULT_NOT_SUPPORTED;
	resp->data_len = 0;

	return 0;
}

/* Handle CMD_BOARD_STATUS */
static int handle_board_status(Message *req, Message *resp)
{
	LOG_INF("CMD_BOARD_STATUS");

	bool power_state = power_get_state();
	resp->data[0] = power_state ? 1 : 0;
	resp->data_len = 1;
	resp->cmd_result = CMD_RESULT_SUCCESS;

	return 0;
}

/* Handle CMD_POWER_INFO */
static int handle_power_info(Message *req, Message *resp)
{
	LOG_INF("CMD_POWER_INFO");

	/* TODO: Implement detailed power information */
	bool power_state = power_get_state();
	resp->data[0] = power_state ? 1 : 0;
	resp->data_len = 1;
	resp->cmd_result = CMD_RESULT_SUCCESS;

	return 0;
}

/* Handle CMD_RESTART */
static int handle_restart(Message *req, Message *resp)
{
	LOG_INF("CMD_RESTART");

	/* Cold reboot: power off, wait, power on */
	int ret = power_set_state(false);
	if (ret == 0) {
		k_msleep(1000);
		ret = power_set_state(true);
	}

	resp->cmd_result = (ret == 0) ? CMD_RESULT_SUCCESS : CMD_RESULT_ERROR;
	resp->data_len = 0;

	return ret;
}

/* Process incoming message and send response */
int host_process_message(Message *msg)
{
	Message resp = {0};

	/* Initialize response */
	resp.header = FRAME_HEADER;
	resp.xTaskToNotify = msg->xTaskToNotify;
	resp.msg_type = MSG_REPLY;
	resp.cmd_type = msg->cmd_type;
	resp.cmd_result = CMD_RESULT_INVALID;
	resp.data_len = 0;
	resp.tail = FRAME_TAIL;

	/* Only process requests */
	if (msg->msg_type != MSG_REQUEST) {
		LOG_WRN("Received non-request message type: 0x%02x", msg->msg_type);
		resp.cmd_result = CMD_RESULT_INVALID;
		send_message(&resp);
		return -EINVAL;
	}

	/* Handle command */
	switch (msg->cmd_type) {
	case CMD_POWER_OFF:
		handle_power_off(msg, &resp);
		break;
	case CMD_REBOOT:
		handle_reboot(msg, &resp);
		break;
	case CMD_READ_BOARD_INFO:
		handle_read_board_info(msg, &resp);
		break;
	case CMD_CONTROL_LED:
		handle_control_led(msg, &resp);
		break;
	case CMD_PVT_INFO:
		handle_pvt_info(msg, &resp);
		break;
	case CMD_BOARD_STATUS:
		handle_board_status(msg, &resp);
		break;
	case CMD_POWER_INFO:
		handle_power_info(msg, &resp);
		break;
	case CMD_RESTART:
		handle_restart(msg, &resp);
		break;
	default:
		LOG_WRN("Unknown command type: 0x%02x", msg->cmd_type);
		resp.cmd_result = CMD_RESULT_NOT_SUPPORTED;
		break;
	}

	/* Send response */
	return send_message(&resp);
}

/* Host communication thread */
static void host_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	Message msg;

	if (host_uart == NULL || !device_is_ready(host_uart)) {
		LOG_ERR("Host UART not available, thread exiting");
		return;
	}

	LOG_INF("Host communication thread started");

	while (1) {
		if (receive_message(&msg) == 0) {
			LOG_DBG("Received message: type=0x%02x, cmd=0x%02x",
				msg.msg_type, msg.cmd_type);
			host_process_message(&msg);
		} else {
			/* No message received, sleep a bit */
			k_msleep(10);
		}
	}
}

/* Initialize host communication */
int host_init(void)
{
	if (!DT_NODE_EXISTS(HOST_UART_NODE)) {
		LOG_INF("Host UART alias 'host-message' not defined in device tree - host comms disabled");
		return 0; /* Not an error, just not configured */
	}

	if (host_uart == NULL || !device_is_ready(host_uart)) {
		LOG_WRN("Host UART device not ready - host comms disabled");
		return 0; /* Not an error, device may not be available */
	}

	LOG_INF("Host UART initialized");

	/* Start host communication thread */
	k_thread_create(&host_thread_data, host_thread_stack,
			K_THREAD_STACK_SIZEOF(host_thread_stack),
			host_thread,
			NULL, NULL, NULL,
			HOST_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&host_thread_data, "host_comms");

	return 0;
}

