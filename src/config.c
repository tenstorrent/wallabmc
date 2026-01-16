/* SPDX-License-Identifier: Apache-2.0 */
#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wallabmc_config, LOG_LEVEL_INF);

#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <ctype.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>

#include <zephyr/fs/fs.h>
#include <zephyr/net/hostname.h>
#include <zephyr/posix/arpa/inet.h> /* inet_ntop */

#include "config.h"
#include "main.h"
#include "net.h"
#include "dhcp.h"
#include "fs.h"

static const char CONFIG_FILE[] = "/lfs/config_data.bin";

#define MAX_HOSTNAME_LEN 15

#define MAX_PW_LEN 15

/*
 * Incrementing this allows struct config_data to be modified
 * arbitrarily, but there is no longer compatibility between
 * versions and the config will be destroyed.
 */
#define FS_CONFIG_VERSION 2

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
	uint32_t bmc_default_ip4;
	uint8_t bmc_use_dhcp4;
	uint8_t host_auto_poweron;
	char bmc_admin_password[MAX_PW_LEN + 1]; /* NULL terminated */
} __packed;

/*
 * littlefs can keep up to 64 bytes of data in the file/
 * inode rather than require a new data sector for it.
 * P550 only has 2 sectors which is not enough for a
 * data sector, so keep this within 64 bytes until we
 * work out how to rearrange the flash more optimally.
 */
BUILD_ASSERT(sizeof(struct config_data) <= 64);

static struct config_data config_data;

uint32_t config_bmc_default_ip4(void)
{
	return config_data.bmc_default_ip4;
}

bool config_bmc_use_dhcp4(void)
{
	return config_data.bmc_use_dhcp4;
}

bool config_host_auto_poweron(void)
{
	return config_data.host_auto_poweron;
}

const char *config_bmc_admin_password(void)
{
	return config_data.bmc_admin_password;
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

static int read_config(void)
{
	struct fs_file_t config_file;
	size_t remain = sizeof(config_data);
	size_t copied = 0;
	int rc;

	fs_file_t_init(&config_file);

	if (!config_exists()) {
		memset(&config_data, 0, sizeof(config_data));
		return copied;
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

	if (!fs_enabled())
		return 0;

	fs_file_t_init(&config_file);

	if (!config_exists()) {
		rc = fs_open(&config_file, CONFIG_FILE, FS_O_CREATE);
		if (rc) {
			LOG_ERR("Could not create file %s (err=%d)", CONFIG_FILE, rc);
			return rc;
		}
	} else {
		rc = fs_open(&config_file, CONFIG_FILE, FS_O_WRITE);
		if (rc) {
			LOG_ERR("Could not open file %s (err=%d)", CONFIG_FILE, rc);
			return rc;
		}
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

int config_bmc_hostname_set(const char *hostname)
{
	int rc;

	rc = net_do_set_hostname(hostname);
	if (rc)
		return rc;

	strncpy(config_data.bmc_hostname, hostname, MAX_HOSTNAME_LEN);

	rc = write_config();
	if (rc < 0) {
		LOG_ERR("Configuration could not be saved (err=%d)", rc);
		return rc;
	}

	return 0;
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

	rc = config_bmc_hostname_set(argv[1]);
	if (rc) {
		shell_error(sh, "Could not set BMC hostname (err=%d)", rc);
		return rc;
	}

	shell_info(sh, "BMC hostname set to %s", config_data.bmc_hostname);

	return 0;
}

static const char *config_default_ip4_string(void)
{
	static char default_ip4_str[INET_ADDRSTRLEN];
	static struct in_addr addr;

	addr.s_addr = config_data.bmc_default_ip4;

	if (inet_ntop(AF_INET, &addr, default_ip4_str, sizeof(default_ip4_str)) == NULL) {
		LOG_ERR("Could not convert IPv4 address 0x%08x to str", config_data.bmc_default_ip4);
		return NULL;
	}

	return default_ip4_str;
}

int config_bmc_default_ip4_set(const char *str)
{
	int rc;

	if (str) {
		static struct in_addr addr;

		rc = inet_pton(AF_INET, str, &addr);
		if (rc != 1) {
			LOG_ERR("Could not convert IPv4 address %s in_addr", str);
			return -EINVAL;
		}

		config_data.bmc_default_ip4 = addr.s_addr;
	} else {
		config_data.bmc_default_ip4 = 0;
	}

	/* Default address gets removed if this is */
	rc = net_do_set_default_ip4(config_data.bmc_default_ip4);
	if (rc) {
		LOG_ERR("Could not apply BMC default IPv4 address (err=%d)", rc);
		return rc;
	}

	rc = write_config();
	if (rc < 0) {
		LOG_ERR("Configuration could not be saved (err=%d)", rc);
		return rc;
	}

	return 0;
}

#define CMD_HELP_BMC_DEFAULT_IP4		\
	"Configure BMC default IPv4 address\n"	\
	"Usage: bmc ipv4_addr <IPv4 address>"

static int cmd_config_bmc_default_ip4(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);

	if (!is_boot_finished()) {
		shell_error(sh, "must wait for boot to finish");
		return -EAGAIN;
	}

	rc = config_bmc_default_ip4_set(argv[1]);
	if (rc) {
		shell_error(sh, "Could not set BMC default IPv4 address (err=%d)", rc);
		return rc;
	}

	shell_info(sh, "BMC default IPv4 address set to %s", argv[1]);

	return 0;
}

int config_bmc_use_dhcp4_set(bool use)
{
	int rc;

	if (use) {
		if (config_data.bmc_use_dhcp4 == 1)
			return 0;
		config_data.bmc_use_dhcp4 = 1;
		start_dhcp4();
	} else {
		if (config_data.bmc_use_dhcp4 == 0)
			return 0;
		config_data.bmc_use_dhcp4 = 0;
		stop_dhcp4();
	}

	rc = write_config();
	if (rc < 0) {
		LOG_ERR("Configuration could not be saved (err=%d)", rc);
		return rc;
	}

	return 0;
}

#define CMD_HELP_BMC_DHCP4			\
	"BMC DHCP4 enabled\n"			\
	"Usage: bmc dhcpv4 <enable|disable>"

static int cmd_config_bmc_dhcp4(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	if (!is_boot_finished()) {
		shell_error(sh, "must wait for boot to finish");
		return -EAGAIN;
	}

	if (!strcmp(argv[1], "enable")) {
		config_bmc_use_dhcp4_set(true);
		shell_info(sh, "BMC DHCPv4 enabled");
	} else if (!strcmp(argv[1], "disable")) {
		config_bmc_use_dhcp4_set(false);
		shell_info(sh, "BMC DHCPv4 disabled");
	} else {
		shell_error(sh, "bmc dhcpv4: unknown argument %s", argv[1]);
		return -EINVAL;
	}

	return 0;
}

int config_bmc_password_set(const char *password)
{
	int rc;

	strncpy(config_data.bmc_admin_password, password, MAX_PW_LEN);

	rc = write_config();
	if (rc < 0) {
		LOG_ERR("Configuration could not be saved (err=%d)", rc);
		return rc;
	}

	return 0;
}

#define CMD_HELP_BMC_ADMIN_PASSWORD		\
	"BMC admin password\n"			\
	"Usage: bmc password <pw>"

static int cmd_config_bmc_password(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	if (!is_boot_finished()) {
		shell_error(sh, "must wait for boot to finish");
		return -EAGAIN;
	}

	config_bmc_password_set(argv[1]);
	shell_info(sh, "BMC admin password updated");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_config_bmc_cmds,
	SHELL_CMD_ARG(password,		NULL, CMD_HELP_BMC_ADMIN_PASSWORD, cmd_config_bmc_password, 2, 0),
	SHELL_CMD_ARG(hostname,		NULL, CMD_HELP_BMC_HOSTNAME, cmd_config_bmc_hostname, 2, 0),
	SHELL_CMD_ARG(ipv4_addr,	NULL, CMD_HELP_BMC_DEFAULT_IP4, cmd_config_bmc_default_ip4, 2, 0),
	SHELL_CMD_ARG(dhcpv4,		NULL, CMD_HELP_BMC_DHCP4, cmd_config_bmc_dhcp4, 2, 0),
	SHELL_SUBCMD_SET_END
);

int config_host_auto_poweron_set(bool on)
{
	int rc;

	if (on) {
		if (config_data.host_auto_poweron == 1)
			return 0;
		config_data.host_auto_poweron = 1;
	} else {
		if (config_data.host_auto_poweron == 0)
			return 0;
		config_data.host_auto_poweron = 0;
	}

	rc = write_config();
	if (rc < 0) {
		LOG_ERR("Configuration could not be saved (err=%d)", rc);
		return rc;
	}

	return 0;
}

#define CMD_HELP_HOST_AUTO_POWERON		\
	"Host auto poweron enabled\n"		\
	"Usage: host auto_poweron <enable|disable>"

static int cmd_config_host_auto_poweron(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	if (!is_boot_finished()) {
		shell_error(sh, "must wait for boot to finish");
		return -EAGAIN;
	}

	if (!strcmp(argv[1], "enable")) {
		config_host_auto_poweron_set(true);
		shell_info(sh, "Host auto poweron enabled");
	} else if (!strcmp(argv[1], "disable")) {
		config_host_auto_poweron_set(false);
		shell_info(sh, "Host auto poweron disabled");
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_config_host_cmds,
	SHELL_CMD_ARG(auto_poweron,	NULL, CMD_HELP_HOST_AUTO_POWERON, cmd_config_host_auto_poweron, 2, 0),
	SHELL_SUBCMD_SET_END
);

static int cmd_config_show(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "--- Configuration ---");
	shell_print(sh, "Version: %d",		config_data.version);
	shell_print(sh, "BMC hostname: %s",	config_data.bmc_hostname);
	shell_print(sh, "BMC default IPv4: %s", config_default_ip4_string());
	shell_print(sh, "BMC use DHCPv4: %d",	config_data.bmc_use_dhcp4);
	shell_print(sh, "Host auto poweron: %d", config_data.host_auto_poweron);
	shell_print(sh, "---------------------");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_config_cmds,
	SHELL_CMD(show,	NULL, "Show configuration.", &cmd_config_show),
	SHELL_CMD(bmc,	&sub_config_bmc_cmds, "BMC configuration commands.", NULL),
	SHELL_CMD(host,	&sub_config_host_cmds, "Host configuration commands.", NULL),
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

	if (fs_enabled()) {
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
		if (config_data.version != FS_CONFIG_VERSION) {
			LOG_WRN("Config version unknown (version=%d), creating new config", config_data.version);
			memset(&config_data, 0, sizeof(config_data));
			ondisk_size = 0;
			config_data.version = FS_CONFIG_VERSION;
		}
	} else {
		config_data.version = FS_CONFIG_VERSION;
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

	if (IS_ONDISK(bmc_use_dhcp4))
		LOG_INF("BMC DHCPv4: %s", config_data.bmc_use_dhcp4 ? "enabled" : "disabled");
	else
		config_data.bmc_use_dhcp4 = 1; /* Default to enabled */

	if (IS_ONDISK(bmc_default_ip4)) {
		if (config_data.bmc_default_ip4) {
			rc = net_do_set_default_ip4(config_data.bmc_default_ip4);
			if (rc) {
				LOG_ERR("Config: could not set default IPv4");
				return rc;
			}
			LOG_INF("BMC default IPv4 set to %s", config_default_ip4_string());
		}
	} else {
		config_data.bmc_default_ip4 = 0; /* Default to not set */
	}

	if (IS_ONDISK(host_auto_poweron))
		LOG_INF("Host auto-poweron: %s", config_data.host_auto_poweron ? "enabled" : "disabled");
	else
		config_data.host_auto_poweron = 0; /* Default to disabled */

	if (!IS_ONDISK(bmc_admin_password)) {
		/* This defaults to "admin" */
		strncpy(config_data.bmc_admin_password, "admin", MAX_HOSTNAME_LEN);
	}
#undef IS_ONDISK

	/* Write back any newly initialised fields. */
	if (fs_enabled() && ondisk_size != sizeof(config_data)) {
		rc = write_config();
		if (rc < 0) {
			LOG_ERR("Could not update config file, continuing without persistent storage");
			return fs_exit();
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
