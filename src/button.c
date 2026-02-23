/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-FileCopyrightText: © 2025-2026 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wallabmc_button, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <inttypes.h>

#include "button.h"
#include "main.h"
#include "config.h"
#include "fs.h"

#define USER_BUTTON_NODE	DT_ALIAS(reset_button)
#if DT_NODE_HAS_STATUS_OKAY(USER_BUTTON_NODE)

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(USER_BUTTON_NODE, gpios,
							      {0});
static struct gpio_callback button_cb_data;

static void button_work_fn(struct k_work *work)
{
	int rc;

	rc = config_clear();
	if (rc) {
		LOG_ERR("Error: could not clear config");
		return;
	}

	bmc_reboot();
}

static K_WORK_DEFINE(button_work, button_work_fn);

static void button_pressed(const struct device *dev,
			   struct gpio_callback *cb, uint32_t pins)
{
	static bool was_pressed = false;

	/*
	 * Interrupt fires a lot while the button is held, so only run this once,
	 * to avoid spamming the log or writing a lot to the fs.
	 */
	if (was_pressed)
		return;
	was_pressed = true;

	LOG_INF("Button pressed: resetting to factory config and rebooting");

	k_work_submit(&button_work);
}

int button_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("Error: button device %s is not ready",
			button.port->name);
		return -ENOSYS;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("Error: button device %s failed to configure pin %d (err=%d)",
			button.port->name, button.pin, ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&button,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		LOG_ERR("Error: button device %s failed to configure interrutp on pin %d (err=%d)",
			button.port->name, button.pin, ret);
		return ret;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);

	LOG_INF("Button device initialised");

	return 0;
}
#else
int button_init(void)
{
	LOG_INF("Button device not defined for this board");
	return 0;
}
#endif
