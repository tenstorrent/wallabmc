/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(stm32_bmc_fs, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>

#include <zephyr/fs/littlefs.h>

#define STORAGE_PARTITION_LABEL slot0_partition
#define STORAGE_PARTITION_ID FIXED_PARTITION_ID(STORAGE_PARTITION_LABEL)

#if FIXED_PARTITION_EXISTS(STORAGE_PARTITION_LABEL)
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(lfs_data);

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

	LOG_INF("Filesystem mounted at %s", lfs_mnt.mnt_point);

	return 0;
}

static int umount_fs(void)
{
	int rc;

	rc = fs_unmount(&lfs_mnt);
	if (rc) {
		LOG_ERR("Unmount failed (err=%d)", rc);
		return rc;
	}

	return 0;
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

/* Useful for debugging persistence, set this 0 to reduce flash writes */
#define BOOT_COUNTER 0

int fs_init(void)
{
	int rc;

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
#else /* FIXED_PARTITION_EXISTS(STORAGE_PARTITION_LABEL) */
int fs_init(void)
{
	LOG_INF("Filesystem storage not enabled for this board");
	return 0;
}
#endif /* FIXED_PARTITION_EXISTS(STORAGE_PARTITION_LABEL) */
