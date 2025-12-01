/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(stm32_bmc, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/net/hostname.h>

#include "fs.h"
#include "power.h"
#include "dhcp.h"
#include "jtag.h"
#include "redfish.h"

int main(void)
{
	if (fs_init() < 0) {
		LOG_ERR("Filesystem init failed");
		return -1;
	}

	if (net_init() < 0) {
		LOG_ERR("Network init failed");
		return -1;
	}

	if (power_init() < 0) {
		LOG_ERR("Power init failed");
		return -1;
	}

	if (reset_init() < 0) {
		LOG_ERR("Reset init failed");
		return -1;
	}

	if (status_led_init() < 0) {
		LOG_ERR("LED init failed");
		return -1;
	}

	if (redfish_init() < 0) {
		LOG_ERR("Redfish init failed");
		return -1;
	}

	if (jtag_init() < 0) {
		LOG_ERR("JTAG init failed");
		return -1;
	}

	return 0;
}
