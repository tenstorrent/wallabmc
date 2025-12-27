/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
LOG_MODULE_REGISTER(wallabmc_rtc, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/sys/timeutil.h>

#include "rtc.h"

int rtc_init(void)
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
