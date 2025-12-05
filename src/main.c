/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
LOG_MODULE_REGISTER(stm32_bmc, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/net/hostname.h>
#include <zephyr/sys/reboot.h>

#include "fs.h"
#include "config.h"
#include "button.h"
#include "net.h"
#include "power.h"
#include "jtag.h"
#include "redfish.h"

static bool boot_finished = false;

bool is_boot_finished(void)
{
	return boot_finished;
}

FUNC_NORETURN int bmc_reboot(void)
{
	fs_exit();

	LOG_WRN("Rebooting BMC");
	/* log_flush() appears to be needed in order to get the logs out. */
	log_flush();
	k_msleep(1000);

	/*
	 * It is said that not all platforms support all reboot types.
	 * Not sure the best way to do this, but try a warm reboot first,
	 * then cold.
	 */
	sys_reboot(SYS_REBOOT_WARM);
	sys_reboot(SYS_REBOOT_COLD);
	k_panic();
	for (;;);
}

int main(void)
{
	if (fs_init() < 0) {
		LOG_ERR("Filesystem init failed");
		return -1;
	}

	if (config_init() < 0) {
		LOG_ERR("Config init failed");
		return -1;
	}

	if (button_init() < 0) {
		LOG_ERR("Button init failed");
		/* Continue */
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

	boot_finished = true;

	return 0;
}
