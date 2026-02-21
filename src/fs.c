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

#if defined(CONFIG_FILE_SYSTEM_LITTLEFS)
#include <zephyr/fs/littlefs.h>

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(lfs_data);

static const char CONFIG_FILE[] = "/lfs/config_data.bin";

static struct fs_mount_t lfs_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &lfs_data,
	.storage_dev = (void *)STORAGE_PARTITION_ID,
	.mnt_point = "/lfs",
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};

static int mount_fs(void)
{
	int rc;

	LOG_INF("Mounting Filesystem...");

	rc = fs_mount(&lfs_mnt);
	if (rc) {
		LOG_DBG("Mount failed (err=%d)", rc);
		LOG_INF("Formatting new filesystem");

		// This is essentially what removing the NO_FORMAT flag would do.
		rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)lfs_mnt.storage_dev, lfs_mnt.fs_data, 0);
		if (rc) {
			LOG_ERR("Format failed (err=%d)", rc);
			return rc;
		}

		// Try mounting again now that it is formatted
		rc = fs_mount(&lfs_mnt);
		if (rc) {
			LOG_ERR("Mount failed (err=%d)", rc);
			return rc;
		}
	}

	fs_is_enabled = true;

	LOG_INF("Filesystem mounted at %s", lfs_mnt.mnt_point);

	return 0;
}

static int umount_fs(void)
{
	int rc;

	if (!fs_is_enabled)
		return 0;

	fs_is_enabled = false;

	rc = fs_unmount(&lfs_mnt);
	if (rc) {
		LOG_ERR("Unmount failed (err=%d)", rc);
		return rc;
	}

	return 0;
}

static bool config_exists(void)
{
	struct fs_dirent *entry; /* don't put this on stack */
	int rc;

	if (!fs_enabled())
		return false;

	entry = malloc(sizeof(*entry));

	rc = fs_stat(CONFIG_FILE, entry);
	if (rc) {
		if (rc != -ENOENT)
			LOG_ERR("Could not stat file %s (err=%d)", CONFIG_FILE, rc);
		free(entry);
		return false;
	}

	if (entry->type != FS_DIR_ENTRY_FILE) {
		LOG_ERR("Config %s is not a regular file! (err=%d)", CONFIG_FILE, rc);
		free(entry);
		return false;
        }

	free(entry);

	return true;
}

int config_clear(void)
{
	int rc;

	if (!config_exists())
		return 0; /* Already cleared */

	rc = fs_unlink(CONFIG_FILE);
	if (rc) {
		LOG_ERR("Could not remove file %s (err=%d)", CONFIG_FILE, rc);
		return rc;
	}

	return 0;
}

ssize_t config_read(void *buf, size_t size)
{
	struct fs_file_t config_file;
	size_t remain = size;
	size_t copied = 0;
	int rc;

	fs_file_t_init(&config_file);

	if (!config_exists()) {
		rc = fs_open(&config_file, CONFIG_FILE, FS_O_CREATE);
		if (rc) {
			LOG_ERR("Could not create file %s (err=%d)", CONFIG_FILE, rc);
			return rc;
		}
		memset(buf, 0, size);
		goto out_close;
	}

	rc = fs_open(&config_file, CONFIG_FILE, FS_O_READ);
	if (rc) {
		LOG_ERR("Could not open or create file %s (err=%d)", CONFIG_FILE, rc);
		return rc;
	}

	memset(buf, 0, size);

	while (remain) {
		rc = fs_read(&config_file, buf + copied, remain);
		if (rc < 0) {
			LOG_ERR("Could not read file %s (err=%d)", CONFIG_FILE, rc);
			return rc;
		}
		if (rc == 0)
			break;

		remain -= rc;
		copied += rc;
	}

out_close:
	rc = fs_close(&config_file);
	if (rc) {
		LOG_ERR("Could not close file %s (err=%d)", CONFIG_FILE, rc);
		return rc;
	}

	return copied;
}

ssize_t config_write(const void *buf, size_t size)
{
	struct fs_file_t config_file;
	size_t remain = size;
	size_t copied = 0;
	int rc;

	fs_file_t_init(&config_file);

	rc = fs_open(&config_file, CONFIG_FILE, FS_O_WRITE);
	if (rc) {
		LOG_ERR("Could not open file %s (err=%d)", CONFIG_FILE, rc);
		return rc;
	}

	while (remain) {
		rc = fs_write(&config_file, buf + copied, remain);
		if (rc <= 0) {
			LOG_ERR("Could not write file %s (err=%d)", CONFIG_FILE, rc);
			return rc;
		}

		remain -= rc;
		copied += rc;
	}

	rc = fs_close(&config_file);
	if (rc) {
		LOG_ERR("Could not close file %s (err=%d)", CONFIG_FILE, rc);
		return rc;
	}

	return copied;
}

#define BUF_SIZE 32

static int increment_boot_file(void)
{
	struct fs_file_t file;
	char buf[BUF_SIZE] = {0};
	int rc;
	int count;

	fs_file_t_init(&file);

	// Note the path "/lfs/" matches the mount point.
	rc = fs_open(&file, "/lfs/boot_count.txt", FS_O_CREATE | FS_O_RDWR);
	if (rc) {
		LOG_ERR("Could not open file /lfs/boot_count.txt (err=%d)", rc);
		return rc;
	}

	rc = fs_read(&file, buf, sizeof(buf) - 1);
	if (rc < 0) {
		LOG_ERR("Could not read file /lfs/boot_count.txt (err=%d)", rc);
		return rc;
	}
	LOG_DBG("Read %d bytes from /lfs/boot_count.txt", rc);

	if (rc == 0)
		count = 0;
	else
		count = atoi(buf);
	count++;

	LOG_INF("Boot count: %d", count);

	snprintf(buf, BUF_SIZE, "%d", count);

	rc = fs_seek(&file, 0, FS_SEEK_SET);
	if (rc) {
		LOG_ERR("Could not seek file /lfs/boot_count.txt (err=%d)", rc);
		return rc;
	}

	rc = fs_write(&file, buf, strlen(buf));
	if (rc < 0) {
		LOG_ERR("Could not write file /lfs/boot_count.txt (err=%d)", rc);
		return rc;
	}
	LOG_DBG("Wrote %d bytes to /lfs/boot_count.txt", rc);

	rc = fs_close(&file);
	if (rc) {
		LOG_ERR("Could not close file /lfs/boot_count.txt (err=%d)", rc);
		return rc;
	}

	return 0;
}

#elif defined(CONFIG_NVS) /* CONFIG_FILE_SYSTEM_LITTLEFS */
#include <zephyr/kvss/nvs.h>

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

	memset(buf, 0, size);

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

#else /* CONFIG_FILE_SYSTEM_LITTLEFS */
#error "No storage system defined (LITTLEFS or NVS)"
#endif /* CONFIG_FILE_SYSTEM_LITTLEFS */

/* Useful for debugging persistence, set this 0 to reduce flash writes */
#define BOOT_COUNTER 0

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

	if (BOOT_COUNTER) {
		rc = increment_boot_file();
		if (rc) {
			umount_fs();
			return rc;
		}
	}

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
