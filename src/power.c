/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <ctype.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(power_shell);

#define GPIO_POWER DT_ALIAS(led0)
#define GPIO_RESET DT_ALIAS(led1)

static const struct gpio_dt_spec gpio_power = GPIO_DT_SPEC_GET(GPIO_POWER, gpios);

int power_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&gpio_power)) {
		LOG_INF("Power GPIO not ready\n");
		return -1;
	}

	ret = gpio_pin_configure_dt(&gpio_power, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_INF("Could not configure power GPIO\n");
		return -1;
	}

	// Power on at BMC boot
	ret = gpio_pin_set_dt(&gpio_power, 1);
	if (ret < 0) {
		LOG_INF("Could not toggle power GPIO\n");
		return -1;
	}

	return 0;
}

static int cmd_power_on(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = gpio_pin_set_dt(&gpio_power, 1);
	if (ret < 0) {
		LOG_INF("Could not toggle power GPIO\n");
		return -1;
	}

	return 0;
}

static int cmd_power_off(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = gpio_pin_set_dt(&gpio_power, 0);
	if (ret < 0) {
		LOG_INF("Could not toggle power GPIO\n");
		return -1;
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_power_cmds,
	SHELL_CMD(on,    NULL, "Power on.", cmd_power_on),
	SHELL_CMD(off,   NULL, "Power off.", cmd_power_off),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(power, &sub_power_cmds, "Power commands", NULL);

static const struct gpio_dt_spec gpio_reset = GPIO_DT_SPEC_GET(GPIO_RESET, gpios);

int reset_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&gpio_reset)) {
		LOG_INF("Reset GPIO not ready\n");
		return -1;
	}

	ret = gpio_pin_configure_dt(&gpio_reset, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_INF("Could not configure reset GPIO\n");
		return -1;
	}

	ret = gpio_pin_set_dt(&gpio_reset, 0);
	if (ret < 0) {
		LOG_INF("Could not toggle reset GPIO\n");
		return -1;
	}

	return 0;
}

static int cmd_reset(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = gpio_pin_set_dt(&gpio_reset, 1);
	if (ret < 0) {
		LOG_INF("Could not toggle RESET GPIO\n");
		return -1;
	}

	k_msleep(1000);

	ret = gpio_pin_set_dt(&gpio_reset, 0);
	if (ret < 0) {
		LOG_INF("Could not toggle RESET GPIO\n");
		return -1;
	}

	return 0;
}

SHELL_CMD_REGISTER(reset, NULL, "Reset.", cmd_reset);

/* LED control */
static const struct gpio_dt_spec gpio_led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec gpio_led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec gpio_led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

int led_init(void)
{
	int ret;

	/* Initialize LED0 */
	if (gpio_is_ready_dt(&gpio_led0)) {
		ret = gpio_pin_configure_dt(&gpio_led0, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_INF("Could not configure LED0 GPIO\n");
			return -1;
		}
	} else {
		LOG_INF("LED0 GPIO not ready\n");
		return -1;
	}

	/* Initialize LED1 */
	if (gpio_is_ready_dt(&gpio_led1)) {
		ret = gpio_pin_configure_dt(&gpio_led1, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_INF("Could not configure LED1 GPIO\n");
			return -1;
		}
	} else {
		LOG_INF("LED1 GPIO not ready\n");
		return -1;
	}

	/* Initialize LED2 */
	if (gpio_is_ready_dt(&gpio_led2)) {
		ret = gpio_pin_configure_dt(&gpio_led2, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_INF("Could not configure LED2 GPIO\n");
			return -1;
		}
	} else {
		LOG_INF("LED2 GPIO not ready\n");
		return -1;
	}

	return 0;
}

static int cmd_led_toggle(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	/* Toggle LED0 */
	ret = gpio_pin_toggle_dt(&gpio_led0);
	if (ret < 0) {
		shell_error(sh, "Could not toggle LED0");
		return -1;
	}

	/* Toggle LED1 */
	ret = gpio_pin_toggle_dt(&gpio_led1);
	if (ret < 0) {
		shell_error(sh, "Could not toggle LED1");
		return -1;
	}

	/* Toggle LED2 */
	ret = gpio_pin_toggle_dt(&gpio_led2);
	if (ret < 0) {
		shell_error(sh, "Could not toggle LED2");
		return -1;
	}

	return 0;
}

SHELL_CMD_REGISTER(led_toggle, NULL, "Toggle LEDs on led0, led1, and led2.", cmd_led_toggle);
