/*
 * SPDX-FileCopyrightText: © 2025-2026 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wallabmc_fs, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kvss/nvs.h>

#include "fs.h"

#define STORAGE_PARTITION_LABEL		storage_partition
#if defined(CONFIG_PERSISTENT_STORAGE) && FIXED_PARTITION_EXISTS(STORAGE_PARTITION_LABEL)
#define STORAGE_PARTITION_ID		FIXED_PARTITION_ID(STORAGE_PARTITION_LABEL)
#define STORAGE_PARTITION_DEVICE	FIXED_PARTITION_DEVICE(STORAGE_PARTITION_LABEL)
#define STORAGE_PARTITION_OFFSET	FIXED_PARTITION_OFFSET(STORAGE_PARTITION_LABEL)
#define STORAGE_PARTITION_SIZE		FIXED_PARTITION_SIZE(STORAGE_PARTITION_LABEL)

static bool fs_is_enabled;

bool fs_enabled(void)
{
	return fs_is_enabled;
}

static const int CONFIG_ID = 1;

static struct nvs_fs nvs_fs = {
	.flash_device = STORAGE_PARTITION_DEVICE,
	.offset = STORAGE_PARTITION_OFFSET,
};

static int mount_fs(void)
{
	struct flash_pages_info info;
	int rc;

	LOG_INF("Mounting NVS...");

	rc = flash_get_page_info_by_offs(nvs_fs.flash_device, nvs_fs.offset, &info);
	if (rc) {
		LOG_ERR("Unable to get flash info (err=%d)", rc);
		return rc;
	}

	nvs_fs.sector_size = info.size;
	nvs_fs.sector_count = STORAGE_PARTITION_SIZE / nvs_fs.sector_size;
	LOG_INF("  NVS using flash at 0x%08lx sector size 0x%08x sector count %u",
		nvs_fs.offset, nvs_fs.sector_size, nvs_fs.sector_count);

	/*
	 * CONFIG_NVS_INIT_BAD_MEMORY_REGION is set, so this will try to init
	 * storage if there was no NVS filesystem there.
	 */
	rc = nvs_mount(&nvs_fs);
	if (rc) {
		LOG_DBG("Mount failed (err=%d)", rc);
		return rc;
	}

	fs_is_enabled = true;

	LOG_INF("NVS mounted");

	return 0;
}

static int umount_fs(void)
{
	fs_is_enabled = false;

	return 0;
}

int config_clear(void)
{
	int rc;

	if (!fs_enabled())
		return 0;

	rc = nvs_clear(&nvs_fs);
	if (rc) {
		LOG_ERR("Could not clear config (err=%d)", rc);
		return rc;
	}

	return 0;
}

ssize_t config_read(void *buf, size_t size)
{
	int rc;

	rc = nvs_read(&nvs_fs, CONFIG_ID, buf, size);
	if (rc < 0) {
		if (rc == -ENOENT)
			return 0;
		LOG_ERR("Could not read config (err=%d)", rc);
		return rc;
	}

	if (rc > size) {
		LOG_INF("Read newer version of config (size=%d)", rc);
		rc = size;
	}

	return rc;
}

ssize_t config_write(const void *buf, size_t size)
{
	int rc;

	rc = nvs_write(&nvs_fs, CONFIG_ID, buf, size);
	if (rc < 0) {
		LOG_ERR("Could not write config (err=%d)", rc);
		return rc;
	}
	if (rc == 0) {
		LOG_DBG("Writing unchanged data");
		return size;
	}

	return rc;
}

static int increment_boot_file(void)
{
	return -ENOTSUP;
}

int fs_init(void)
{
	int rc;

	if (!device_is_ready(STORAGE_PARTITION_DEVICE)) {
		LOG_ERR("Flash device not ready");
		return -ENODEV;
	}

	rc = mount_fs();
	if (rc)
		return rc;

	return 0;
}

int fs_exit(void)
{
	return umount_fs();
}
#else /* defined(CONFIG_PERSISTENT_STORAGE) && FIXED_PARTITION_EXISTS(STORAGE_PARTITION_LABEL) */

int fs_init(void)
{
	LOG_INF("Filesystem storage not enabled for this board");
	return 0;
}

int fs_exit(void)
{
	LOG_INF("Filesystem storage not enabled for this board");
	return 0;
}

bool fs_enabled(void)
{
	return false;
}

ssize_t config_read(void *buf, size_t size)
{
	return -ENODEV;
}

ssize_t config_write(const void *buf, size_t size)
{
	return -ENODEV;
}

int config_clear(void)
{
	return 0;
}

#endif /* defined(CONFIG_PERSISTENT_STORAGE) && FIXED_PARTITION_EXISTS(STORAGE_PARTITION_LABEL) */
