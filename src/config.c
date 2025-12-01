#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(stm32_bmc_config, LOG_LEVEL_INF);

#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <ctype.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>

#include <zephyr/fs/fs.h>
#include <zephyr/net/hostname.h>

#include "config.h"
#include "main.h"
#include "net.h"

static const char CONFIG_FILE[] = "/lfs/config_data.bin";

#define MAX_HOSTNAME_LEN 31

/*
 * Add fields to the end, do not remove or change meaning.
 * Ignoring is okay if a field becomes unused, but gracefully
 * handling deprecated fiels would be better. Version should
 * not be bumped unless there is unavoidable incompatible
 * change.
 */
struct config_data {
	uint8_t version;
	char bmc_hostname[MAX_HOSTNAME_LEN + 1]; /* NULL terminated */
} __packed;

static struct config_data config_data;

/* Config is updated but not saved to flash */
static bool config_dirty = false;

static bool config_exists(void)
{
	struct fs_dirent *entry; /* don't put this on stack */
	int rc;

	if (!IS_ENABLED(CONFIG_PERSISTENT_STORAGE))
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

static int read_config(void)
{
	struct fs_file_t config_file;
	size_t remain = sizeof(config_data);
	size_t copied = 0;
	int rc;

	fs_file_t_init(&config_file);

	if (!config_exists()) {
		rc = fs_open(&config_file, CONFIG_FILE, FS_O_CREATE);
		if (rc) {
			LOG_ERR("Could not create file %s (err=%d)", CONFIG_FILE, rc);
			return rc;
		}
		memset(&config_data, 0, sizeof(config_data));
		goto out_close;
	}

	rc = fs_open(&config_file, CONFIG_FILE, FS_O_READ);
	if (rc) {
		LOG_ERR("Could not open or create file %s (err=%d)", CONFIG_FILE, rc);
		return rc;
	}

	memset(&config_data, 0, sizeof(config_data));

	while (remain) {
		rc = fs_read(&config_file, (void *)(((unsigned long)&config_data) + copied), remain);
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

static int write_config(void)
{
	struct fs_file_t config_file;
	size_t remain = sizeof(config_data);
	size_t copied = 0;
	int rc;

	fs_file_t_init(&config_file);

	rc = fs_open(&config_file, CONFIG_FILE, FS_O_WRITE);
	if (rc) {
		LOG_ERR("Could not open file %s (err=%d)", CONFIG_FILE, rc);
		return rc;
	}

	while (remain) {
		rc = fs_write(&config_file, (void *)(((unsigned long)&config_data) + copied), remain);
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

#define CMD_HELP_BMC_HOSTNAME			\
	"Configure BMC hostname\n"		\
	"Usage: bmc hostname <hostname>"

static int cmd_config_bmc_hostname(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);

	if (!is_boot_finished()) {
		shell_error(sh, "must wait for boot to finish");
		return -EAGAIN;
	}

	rc = net_do_set_hostname(argv[1]);
	if (rc) {
		shell_error(sh, "Could not set BMC hostname (err=%d)", rc);
		return rc;
	}

	strncpy(config_data.bmc_hostname, argv[1], MAX_HOSTNAME_LEN);
	shell_info(sh, "BMC hostname set to %s", config_data.bmc_hostname);

	config_dirty = true;

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_config_bmc_cmds,
	SHELL_CMD_ARG(hostname,    NULL, CMD_HELP_BMC_HOSTNAME, cmd_config_bmc_hostname, 2, 0),
	SHELL_SUBCMD_SET_END
);

static int cmd_config_show(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "--- Configuration ---");
	shell_print(sh, "Version: %d",		config_data.version);
	shell_print(sh, "BMC hostname: %s",	config_data.bmc_hostname);
	shell_print(sh, "---------------------");
	if (config_dirty) {
		if (IS_ENABLED(CONFIG_PERSISTENT_STORAGE))
			shell_print(sh, "Configuration changes have not been saved");
		else
			shell_print(sh, "Configuration changes have been made");
	}

	return 0;
}

#ifdef CONFIG_PERSISTENT_STORAGE
static int cmd_config_save(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!config_dirty) {
		shell_info(sh, "Configuration is unchanged, skipping save");
		return 0;
	}

	rc = write_config();
	if (rc < 0) {
		shell_error(sh, "Configuration could not be saved (err=%d)", rc);
		return rc;
	}

	config_dirty = false;

	shell_info(sh, "Configuration saved to flash");

	return 0;
}
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(sub_config_cmds,
	SHELL_CMD(show,	NULL, "Show configuration.", &cmd_config_show),
#ifdef CONFIG_PERSISTENT_STORAGE
	SHELL_CMD(save,	NULL, "Save configuration.", &cmd_config_save),
#endif
	SHELL_CMD(bmc,	&sub_config_bmc_cmds, "BMC configuration commands.", NULL),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(config, &sub_config_cmds, "Configuration commands", NULL);

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

int config_init(void)
{
	int ondisk_size;
	int rc;

	if (IS_ENABLED(CONFIG_PERSISTENT_STORAGE)) {
		rc = read_config();
		if (rc < 0) {
			LOG_ERR("Error loading config");
			return rc;
		}

		ondisk_size = rc;
	} else {
		memset(&config_data, 0, sizeof(config_data));
		ondisk_size = 0;
	}

	/*
	 * Fields either come from ondisk, in which case they should be verified and
	 * applied, or they are new fields in which case they should be initialised
	 * and defaults applied.
	 */
#define IS_ONDISK(field)			\
	(ondisk_size >= offsetof(struct config_data, field) + sizeof(config_data.field))

	if (IS_ONDISK(version)) {
		if (config_data.version != 1) {
			LOG_ERR("Config version unknown (version=%d)", config_data.version);
			return -EINVAL;
		}
	} else {
		config_data.version = 1;
	}

	if (IS_ONDISK(bmc_hostname)) {
		rc = net_do_set_hostname(config_data.bmc_hostname);
		if (rc) {
			LOG_ERR("Config: could not set hostname to %s", config_data.bmc_hostname);
			return rc;
		}
		LOG_INF("BMC hostname set to %s", config_data.bmc_hostname);
	} else {
		/* This defaults to CONFIG_NET_HOSTNAME */
		strncpy(config_data.bmc_hostname, net_hostname_get(), MAX_HOSTNAME_LEN);
	}
#undef IS_ONDISK

	/* Write back any newly initialised fields. */
	if (IS_ENABLED(CONFIG_PERSISTENT_STORAGE) &&
			ondisk_size != sizeof(config_data)) {
		rc = write_config();
		if (rc < 0) {
			LOG_ERR("Could not update config file");
			return rc;
		}
	}

	if (ondisk_size == 0) {
		LOG_INF("Initialised new config");
	} else {
		if (ondisk_size == sizeof(config_data))
			LOG_INF("Loaded config from flash");
		else
			LOG_INF("Loaded config from flash, initialised new fields");
	}

	return 0;
}
