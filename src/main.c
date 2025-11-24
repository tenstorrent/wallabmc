/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(stm32_bmc, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/net/hostname.h>

#include "power.h"
#include "dhcp.h"
#include "jtag.h"
#include "redfish.h"

int main(void)
{
	LOG_INF("   Hostname: %s", net_hostname_get());

	if (start_dhcp4() < 0) {
		LOG_ERR("DHCP4 init failed");
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

	if (led_init() < 0) {
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
