/* Networking */

/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(stm32_bmc_net, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <errno.h>
#include <stdio.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>

#include "net.h"
#include "dhcp.h"

int net_init(void)
{
	int rc;

	LOG_INF("   Hostname: %s", net_hostname_get());

	rc = start_dhcp4();
	if (rc) {
		LOG_ERR("DHCP4 init failed");
		return rc;
	}

	return 0;
}
