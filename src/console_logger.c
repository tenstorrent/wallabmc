/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(console_logger, LOG_LEVEL_INF);

#include <zephyr/spinlock.h>
#include <zephyr/posix/fcntl.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/socket.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>

#define UART_RX_DELAY_US 100000

// #define UART_NODE DT_NODELABEL(host_console_uart)
#define UART_NODE DT_ALIAS(host_console_uart)
static const struct device *uart_dev = DEVICE_DT_GET(UART_NODE);

BUILD_ASSERT(DT_NODE_EXISTS(UART_NODE), "host-console-uart node missing");

/* UART mux select GPIO from device tree */
#define UARTMUXSEL_NODE DT_ALIAS(uartmuxsel)
#if DT_NODE_EXISTS(UARTMUXSEL_NODE)
static const struct gpio_dt_spec uartmuxsel_gpio = GPIO_DT_SPEC_GET(UARTMUXSEL_NODE, gpios);
static bool uartmuxsel_initialized = false;

/* Update uartmuxsel GPIO based on client connection state */
static void update_uartmuxsel_gpio(bool connected)
{
	if (uartmuxsel_initialized) {
		gpio_pin_set_dt(&uartmuxsel_gpio, connected ? 1 : 0);
	}
}
#endif

#define CONSOLE_LOG_SIZE (4*1024)
#define UART_RX_BUF_SIZE 256
#define UART_TX_BUF_SIZE 128

struct console_log {
	const struct device *uart;
	uint64_t received;
	int size;
	struct k_spinlock rx_lock;
	struct k_sem rx_sem;
	struct k_sem tx_sem;
//	uint8_t buffer[];
	uint8_t *log_buffer;
	uint8_t *tx_buffer;
};

/* DMA'ed to/from by the UART, so must be __nocache */
static __nocache uint8_t log_buffer[CONSOLE_LOG_SIZE];
static __nocache uint8_t tx_buffer[UART_TX_BUF_SIZE];

static struct console_log *host_console_log;

static void calc_rx_buf(struct console_log *log, int *off, int *len)
{
	*off = log->received % log->size;
	*len = MIN(UART_RX_BUF_SIZE, log->size - *off);
}

static void uart_rx_ready(const struct device *dev, struct uart_event *evt, struct console_log *log)
{
	k_spinlock_key_t key;

	printf("%s: received=%d@%d\n", __func__, evt->data.rx.len, evt->data.rx.offset);
	key = k_spin_lock(&log->rx_lock);
	log->received += evt->data.rx.len;
	k_spin_unlock(&log->rx_lock, key);

	k_sem_give(&log->rx_sem);
}

static void uart_rx_disabled(const struct device *dev, struct console_log *log)
{
	int off, len;
	int ret;

	printf("%s\n", __func__);

	calc_rx_buf(log, &off, &len);
	ret = uart_rx_enable(dev, log->log_buffer + off, len, UART_RX_DELAY_US);
	if (ret < 0)
		LOG_ERR("Failed to enable UART RX: %d", ret);
}

static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
	struct console_log *log = user_data;

	printf("%s %d\n", __func__, evt->type);
	switch (evt->type) {
	case UART_RX_RDY:
		uart_rx_ready(dev, evt, log);
		break;

	case UART_RX_DISABLED:
		uart_rx_disabled(dev, log);
		break;

	case UART_RX_BUF_REQUEST:
		/* XXX: implement this */
	case UART_RX_STOPPED:
		break;

	case UART_TX_DONE:
		k_sem_give(&log->tx_sem);
		break;

	default:
		break;
	}
}

static ssize_t console_log_read(struct console_log *log, uint8_t *buf, size_t size, uint64_t *ppos)
{
	uint64_t pos = *ppos;
	uint64_t start;
	int off, len;
	k_spinlock_key_t key;
	ssize_t ret = 0;
	size_t copied = 0;

	key = k_spin_lock(&log->rx_lock);
	if (pos > log->received) {
		ret = -EINVAL;
		goto out;
	}

	if (log->received < log->size) {
		start = 0;
	} else {
		calc_rx_buf(log, &off, &len);
		start = log->received - (log->size - len);
	}

	if (start > pos) /* lost characters */
		pos = start;

	while (copied < size) {
		off = pos % log->size;
		len = MIN(log->received - pos, log->size - off);
		len = MIN(len, size - copied);
		if (len == 0)
			break;
		printf("read buffer from %d to %d\n", off, off + len);
#if 0
		for (int i = 0; i < len; i++) {
			if (log->log_buffer[off + i] == '\n') {
				buf[i] = '\r';
				i++;
				len++;
			}
			buf[i] = log->log_buffer[off + i];
		}
#else
		memcpy(buf, log->log_buffer + off, len);
#endif
		pos += len;
		copied += len;
	}

out:
	k_spin_unlock(&log->rx_lock, key);

	*ppos = pos;

	return copied ? copied : ret;
}

static ssize_t console_log_write(struct console_log *log, const uint8_t *buf, size_t size)
{
	size_t copied = 0;

	while (copied < size) {
		size_t len = MIN(size - copied, UART_TX_BUF_SIZE);
		int ret;

		k_sem_take(&log->tx_sem, K_FOREVER);
		memcpy(log->tx_buffer, buf + copied, len);

		if (1) {
			log->tx_buffer[len] = '\0';
			printk("UART TX %s\n", log->tx_buffer);
		}

		ret = uart_tx(log->uart, log->tx_buffer, len, SYS_FOREVER_US);
		if (ret < 0) {
			LOG_WRN("UART TX error: %d", ret);
			k_sem_give(&log->tx_sem);
			return copied ? copied : ret;
		} else {
			k_sem_give(&log->tx_sem);
		}

		copied += len;
	}

	return copied;
}

ssize_t host_console_read(uint8_t *buf, size_t size, uint64_t *ppos, k_timeout_t timeout)
{
	size_t copied = 0;

	while (copied < size) {
		ssize_t ret;
		size_t len = size - copied; 

		ret = console_log_read(host_console_log, buf + copied, len, ppos);
		if (ret < 0)
			return copied ? copied : ret;

		copied += ret;

		if (ret < len) {
			if (copied > 0)
				break;
			/* XXX: use a k event to signal all waiters */
			if (k_sem_take(&host_console_log->rx_sem, timeout) != 0)
				break;
		}
	}

	return copied;
}

ssize_t host_console_write(const uint8_t *buf, size_t size)
{
	return console_log_write(host_console_log, buf, size);
}

static const struct uart_config uart_cfg = {
	.baudrate = 115200,
	.parity = UART_CFG_PARITY_NONE,
	.stop_bits = UART_CFG_STOP_BITS_1,
	.data_bits = UART_CFG_DATA_BITS_8,
	.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
};

int console_logger_init(void)
{
	int ret;

	LOG_INF("Starting host console logger");

	if (!device_is_ready(uart_dev)) {
		LOG_ERR("UART device not ready");
		return -1;
	}

//	host_console_log = malloc(sizeof(struct console_log) + CONSOLE_LOG_SIZE);
	host_console_log = malloc(sizeof(struct console_log));
	if (!host_console_log) {
		LOG_ERR("Could not allocate console log");
		return -1;
	}

//	memset(host_console_log, 0, sizeof(struct console_log) + CONSOLE_LOG_SIZE);
	memset(host_console_log, 0, sizeof(struct console_log));
	host_console_log->uart = uart_dev;
	host_console_log->size = CONSOLE_LOG_SIZE;
	host_console_log->log_buffer = log_buffer;
	host_console_log->tx_buffer = tx_buffer;
	k_sem_init(&host_console_log->rx_sem, 0, 1);
	k_sem_init(&host_console_log->tx_sem, 1, 1);

#if DT_NODE_EXISTS(UARTMUXSEL_NODE)
	/* Initialize uartmuxsel GPIO */
	if (!gpio_is_ready_dt(&uartmuxsel_gpio)) {
		LOG_ERR("uartmuxsel GPIO device not ready");
		return -1;
	}

	ret = gpio_pin_configure_dt(&uartmuxsel_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure uartmuxsel GPIO: %d", ret);
		return ret;
	}

	update_uartmuxsel_gpio(true);
	LOG_INF("uartmuxsel GPIO set");
#endif

	ret = uart_configure(uart_dev, &uart_cfg);
	if (ret < 0) {
		LOG_ERR("Configuration is not supported by device or driver,"
			" check the UART settings configuration");
		return -1;
	}

	/* Set up UART callback for async RX */
	ret = uart_callback_set(uart_dev, uart_callback, host_console_log);
	if (ret < 0) {
		LOG_ERR("Failed to set UART callback: %d", ret);
		return ret;
	}

	/* Enable UART RX to start receiving data */
	ret = uart_rx_enable(uart_dev, host_console_log->log_buffer, UART_RX_BUF_SIZE, UART_RX_DELAY_US);
	if (ret < 0) {
		LOG_ERR("Failed to enable UART RX: %d", ret);
		return ret;
	}

	LOG_INF("UART callback configured for console logger");

	return 0;
}
