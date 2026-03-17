/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef HOST_H
#define HOST_H

#include <stdint.h>
#include <stdbool.h>

/* U84 and stm32 communication definition, from opensbi */
#define FRAME_HEADER		0xA55AAA55
#define FRAME_TAIL		0xBDBABDBA
#define FRAME_DATA_MAX		250

/* Message structure */
typedef struct {
	uint32_t header;		/* Frame header */
	uint32_t xTaskToNotify;		/* id */
	uint8_t msg_type;		/* Message type */
	uint8_t cmd_type;		/* Command type */
	uint8_t cmd_result;		/* command result */
	uint8_t data_len;		/* Data length */
	uint8_t data[FRAME_DATA_MAX];	/* Data */
	uint8_t checksum;		/* Checksum */
	uint32_t tail;			/* Frame tail */
} __attribute__((packed)) Message;

/* Define message types */
typedef enum {
	MSG_REQUEST = 0x01,
	MSG_REPLY,
	MSG_NOTIFY,
} MsgType;

/* Define command types */
typedef enum {
	CMD_POWER_OFF = 0x01,
	CMD_REBOOT,
	CMD_READ_BOARD_INFO,
	CMD_CONTROL_LED,
	CMD_PVT_INFO,
	CMD_BOARD_STATUS,
	CMD_POWER_INFO,
	CMD_RESTART,
	/* cold reboot with power off/on
	 * You can continue adding other command types
	 */
} CommandType;

/* Command result codes */
#define CMD_RESULT_SUCCESS	0x00
#define CMD_RESULT_ERROR	0x01
#define CMD_RESULT_INVALID	0x02
#define CMD_RESULT_NOT_SUPPORTED	0x03

int host_init(void);
int host_process_message(Message *msg);

#endif /* HOST_H */

