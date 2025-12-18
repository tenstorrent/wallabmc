/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
LOG_MODULE_REGISTER(wallabmc, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/version.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/net/hostname.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/shell/shell.h>

#include "fs.h"
#include "config.h"
#include "button.h"
#include "net.h"
#include "http.h"
#include "power.h"
#include "jtag.h"

static bool boot_finished = false;

bool is_boot_finished(void)
{
	return boot_finished;
}

/* Taken from zephyr/kernel/banner.c */
#if defined(BUILD_VERSION) && !IS_EMPTY(BUILD_VERSION)
#define BANNER_VERSION STRINGIFY(BUILD_VERSION)
#else
#define BANNER_VERSION KERNEL_VERSION_STRING
#endif /* BUILD_VERSION */

static void print_banner(void)
{
	int pad = 20;

	printk("\n");
	printk("%*s    +---------------------+\n", pad, "");
	printk("%*s    | Welcome to WallaBMC |\n", pad, "");
	printk("%*s    +---------------------+\n", pad, "");
	printk("%*s                 \\\n", pad, "");
	printk("%*s                   \\     /)\n", pad, "");
	printk("%*s                      <.' \\_\n", pad, "");
	printk("%*s                        / ( )\\\n", pad, "");
	printk("%*s                        __|/  \\__\n", pad, "");
	printk("%*s.,.,.,,,..,,.,.,.,.,..,,.,.,.,,.,..,,.,.,.,.\n", pad, "");

	LOG_INF("Project build: %s", PROJECT_GIT_SHA);
	LOG_INF("Zephyr OS build: %s", BANNER_VERSION);
}

FUNC_NORETURN int bmc_reboot(void)
{
	fs_exit();

	LOG_WRN("Rebooting BMC");
	/* log_panic() appears to be needed in order to get the logs out. */
	log_panic();
	k_msleep(100);

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

static FUNC_NORETURN int bmc_poweroff(void)
{
	fs_exit();

	LOG_WRN("Poweroff BMC");
	/* log_panic() appears to be needed in order to get the logs out. */
	log_panic();
	k_msleep(100);

#ifdef CONFIG_POWEROFF
	sys_poweroff();
	k_panic();
#endif
	for (;;);
}

static int cmd_bmc_reboot(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	bmc_reboot();
	return 0;
}

static int cmd_bmc_poweroff(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	bmc_poweroff();
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_bmc_cmds,
	SHELL_CMD(reboot,	NULL, "Reboot BMC.", cmd_bmc_reboot),
	SHELL_CMD(poweroff,	NULL, "Power off BMC.", cmd_bmc_poweroff),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(bmc, &sub_bmc_cmds, "BMC system commands", NULL);

#if defined(CONFIG_RTC)
static int rtc_init(void)
{
	static const struct device *rtc = DEVICE_DT_GET(DT_NODELABEL(rtc));
	static struct rtc_time tm;
	static struct timespec ts;
	int ret;

	if (!device_is_ready(rtc)) {
		LOG_WRN("No RTC device");
		return 0;
	}

	ret = rtc_get_time(rtc, &tm);
	if (ret == -ENODATA) {
		LOG_INF("RTC is uninitialised, setting to 1 Jan 2026");
		memset(&tm, 0, sizeof(tm));
		tm.tm_year = 2026 - 1900;
		tm.tm_mday = 1;
		ret = rtc_set_time(rtc, &tm);
		if (ret < 0) {
			LOG_ERR("Can not set RTC (err=%d)", ret);
			return ret;
		}
		ret = rtc_get_time(rtc, &tm);
	}
	if (ret < 0) {
		LOG_ERR("Can not get RTC (err=%d)", ret);
		return ret;
	}

	LOG_INF("RTC: %04d-%02d-%02d %02d:%02d:%02d",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);

	ts.tv_sec = timeutil_timegm(rtc_time_to_tm(&tm));
	if (ts.tv_sec == -1) {
		LOG_ERR("Failed to calculate UNIX time from RTC");
		return -EINVAL;
	}
	ts.tv_nsec = tm.tm_nsec;

	ret = sys_clock_settime(SYS_CLOCK_REALTIME, &ts);
	if (ret != 0) {
		LOG_ERR("sys_clock_settime failed (err=%d)", ret);
		return ret;
	}

	return ret;
}
#else /* defined(CONFIG_RTC) */
static inline int rtc_init(void) { return 0; }
#endif /* defined(CONFIG_RTC) */

int main(void)
{
	print_banner();

	LOG_DBG("RTC init");
	if (rtc_init() < 0) {
		LOG_ERR("RTC init failed");
		return -1;
	}

	LOG_DBG("Filesystem init");
	if (fs_init() < 0) {
		LOG_ERR("Filesystem init failed, continuing without persistent storage");
	}

	LOG_DBG("Config init");
	if (config_init() < 0) {
		LOG_ERR("Config init failed");
		return -1;
	}

	LOG_DBG("Button init");
	if (button_init() < 0) {
		LOG_ERR("Button init failed");
		/* Continue */
	}

	LOG_DBG("Network init");
	if (net_init() < 0) {
		LOG_ERR("Network init failed");
		return -1;
	}

	LOG_DBG("Power init");
	if (power_init() < 0) {
		LOG_ERR("Power init failed");
		return -1;
	}

	LOG_DBG("Reset init");
	if (reset_init() < 0) {
		LOG_ERR("Reset init failed");
		return -1;
	}

	LOG_DBG("LED init");
	if (status_led_init() < 0) {
		LOG_ERR("LED init failed");
		return -1;
	}

	LOG_DBG("JTAG init");
	if (jtag_init() < 0) {
		LOG_ERR("JTAG init failed");
		return -1;
	}

	LOG_DBG("HTTP server init");
	if (app_http_server_init() < 0) {
		LOG_ERR("HTTP server init failed");
		return -1;
	}

	boot_finished = true;

	LOG_INF("BMC boot complete");

	return 0;
}
