/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_telnet_sample, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>

#include "power.h"

int main(void)
{
	power_init();
	reset_init();

	return 0;
}
