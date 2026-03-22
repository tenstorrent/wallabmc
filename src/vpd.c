/*
 * SPDX-FileCopyrightText: © 2025-2026 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wallabmc_vpd, LOG_LEVEL_INF);

#include <zephyr/sys/uuid.h>
#include <zephyr/drivers/hwinfo.h>

#include "vpd.h"

#ifdef CONFIG_APP_BMC_UUID
static char bmc_uuid_str[UUID_STR_LEN];
#else
static const char *bmc_uuid_str = "58893887-8974-2487-2389-389233423423";
#endif

int get_bmc_uuid_string(const char **str)
{
	*str = bmc_uuid_str;
	return 0;
}

int vpd_init(void)
{
#ifdef CONFIG_APP_BMC_UUID
	struct uuid uuid_v5_ns;
	struct uuid bmc_uuid;
	uint8_t mcu_uid[16];
	ssize_t length;
	int ret;

	ret = uuid_from_string(CONFIG_WALLABMC_UUID_NS, &uuid_v5_ns);
	if (ret < 0) {
		LOG_ERR("Could not parse namespace UUID %s", CONFIG_WALLABMC_UUID_NS);
		return -EINVAL;
	}

	length = hwinfo_get_device_id(mcu_uid, sizeof(mcu_uid));
	if (length < 0) {
		LOG_ERR("Could not get device UID");
		return -ENODEV;
	}

	ret = uuid_generate_v5(&uuid_v5_ns, mcu_uid, length, &bmc_uuid);
	if (ret < 0) {
		LOG_ERR("Could not generate device UUID");
		return -EINVAL;
	}

	ret = uuid_to_string(&bmc_uuid, bmc_uuid_str);
	if (ret < 0) {
		LOG_ERR("Could not convert UUID to string");
		return -EINVAL;
	}
#endif
	return 0;
}
