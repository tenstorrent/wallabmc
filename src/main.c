/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(stm32_bmc, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/net/hostname.h>

#include "power.h"
#include "dhcp.h"

int main(void)
{
	LOG_INF("   Hostname: %s", net_hostname_get());

	start_dhcp4();
	power_init();
	reset_init();
	led_init();

	return 0;
}
