/*
 * SPDX-License-Identifier: Apache-2.0
 */

 #include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(console_bridge, LOG_LEVEL_INF);

#include <zephyr/posix/fcntl.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/socket.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>

#define CONSOLE_BRIDGE_PORT 22
#define CONSOLE_BRIDGE_STACK_SIZE K_THREAD_STACK_SIZEOF(console_bridge_stack_area)
#define CONSOLE_BRIDGE_PRIORITY   CONFIG_CONSOLE_BRIDGE_PRIORITY
#define UART_RX_TIMEOUT_US 100000

K_THREAD_STACK_DEFINE(console_bridge_stack_area, CONFIG_CONSOLE_BRIDGE_STACK_SIZE);
static struct k_thread console_bridge_thread_data;

K_THREAD_STACK_DEFINE(uart_tx_thread_stack_area, CONFIG_CONSOLE_BRIDGE_STACK_SIZE);
static struct k_thread uart_tx_thread_data;

/* USART6 device from device tree */
#define USART6_NODE DT_NODELABEL(usart6)
static const struct device *usart6_dev = DEVICE_DT_GET(USART6_NODE);

BUILD_ASSERT(DT_NODE_EXISTS(USART6_NODE), "usart6 node missing");

/* UART mux select GPIO from device tree */
#define UARTMUXSEL_NODE DT_ALIAS(uartmuxsel)
#if DT_NODE_EXISTS(UARTMUXSEL_NODE)
static const struct gpio_dt_spec uartmuxsel_gpio = GPIO_DT_SPEC_GET(UARTMUXSEL_NODE, gpios);
static bool uartmuxsel_initialized = false;
#else
static bool uartmuxsel_initialized = false;
#endif

static volatile int active_client_fd = -1;
static volatile bool client_connected = false;

#define UART_RX_BUF_SIZE 256
uint8_t uart_rx_buf[UART_RX_BUF_SIZE];

/* Semaphore to synchronize UART TX operations */
K_SEM_DEFINE(uart_tx_sem, 1, 1);

/* Structure to hold UART RX data for FIFO */
struct uart_rx_data {
	void *fifo_reserved;
	uint8_t *buf;
	size_t offset;
	size_t len;
	int client_fd;
};

/* FIFO for UART RX data */
K_FIFO_DEFINE(uart_rx_fifo);

/* Update uartmuxsel GPIO based on client connection state */
static void update_uartmuxsel_gpio(bool connected)
{
#if DT_NODE_EXISTS(UARTMUXSEL_NODE)
	if (uartmuxsel_initialized) {
		gpio_pin_set_dt(&uartmuxsel_gpio, connected ? 1 : 0);
	}
#endif
}

/* Handle UART RX ready event: enqueue data to FIFO for processing */
/* FIXME this needs some backpressure when the fifo isn't consumed */
static void handle_uart_rx_ready(const struct uart_event *evt)
{
	/* We can't call send() here because it will block and we don't want to block the UART RX thread */
	/* Data received on UART - enqueue to FIFO for processing in separate thread */
	if (client_connected && active_client_fd >= 0 && evt->data.rx.len > 0) {
		struct uart_rx_data *rx_data = k_malloc(sizeof(struct uart_rx_data));
		if (rx_data != NULL) {
			/* Allocate buffer for the data */
			rx_data->buf = k_malloc(evt->data.rx.len);
			if (rx_data->buf != NULL) {
				/* Copy data from UART buffer */
				memcpy(rx_data->buf, evt->data.rx.buf + evt->data.rx.offset,
				       evt->data.rx.len);
				rx_data->offset = 0;
				rx_data->len = evt->data.rx.len;
				rx_data->client_fd = active_client_fd;
				k_fifo_put(&uart_rx_fifo, rx_data);
			} else {
				LOG_WRN("Failed to allocate buffer for UART RX data");
				k_free(rx_data);
			}
		} else {
			LOG_WRN("Failed to allocate UART RX data structure");
		}
	}
}

/* UART callback function: called when data is ready on UART */
static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
	switch (evt->type) {
	case UART_RX_RDY:
		handle_uart_rx_ready(evt);
		break;

	case UART_RX_BUF_REQUEST:
	case UART_RX_DISABLED:
	case UART_RX_STOPPED:
		break;

	case UART_TX_DONE:
		/* Transmission completed - release semaphore */
		k_sem_give(&uart_tx_sem);
		break;

	default:
		break;
	}
}

/* Thread function to process UART RX data from FIFO and send to socket */
static void uart_tx_thread(void *a, void *b, void *c)
{
	struct uart_rx_data *rx_data;
	int rc;

	while (1) {
		/* Wait for data in FIFO */
		rx_data = k_fifo_get(&uart_rx_fifo, K_FOREVER);

		/* Check if client is still connected */
		if (client_connected && rx_data->client_fd >= 0 &&
		    rx_data->client_fd == active_client_fd) {
			rc = send(rx_data->client_fd, rx_data->buf + rx_data->offset,
				  rx_data->len, 0);
			if (rc <= 0) {
				LOG_WRN("Socket send error: %d len: %zu", errno, rx_data->len);
				client_connected = false;
				active_client_fd = -1;
				update_uartmuxsel_gpio(false);
			}
		}

		/* Free allocated buffers */
		if (rx_data->buf != NULL) {
			k_free(rx_data->buf);
		}
		k_free(rx_data);
	}
}

static void handle_client(int client_fd)
{
	/*
	 * Improve performance by enabling TCP_NODELAY (ie disabling Nagle)
	 */
	int opt = 1;
	if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
		LOG_WRN("Failed to set TCP_NODELAY: %d", errno);
	}

	LOG_INF("Client connected to console bridge (USART6 -> TCP port %d)",
		CONSOLE_BRIDGE_PORT);

	/* Set active client */
	active_client_fd = client_fd;
	client_connected = true;
	update_uartmuxsel_gpio(true);

	/* Enable UART RX to start receiving data */
	int ret = uart_rx_enable(usart6_dev, uart_rx_buf, sizeof(uart_rx_buf), UART_RX_TIMEOUT_US);
	if (ret < 0) {
		LOG_ERR("Failed to enable UART RX: %d", ret);
	}

	/* Main loop: read from socket and write to UART */
	uint8_t socket_buf[256];
	bool finished = false;

	while (!finished && client_connected) {
		int rc = recv(client_fd, socket_buf, sizeof(socket_buf), 0);
		if (rc <= 0) {
			if (rc == 0) {
				LOG_INF("Client disconnected gracefully");
			} else {
				LOG_WRN("Socket recv() error: %d", errno);
			}
			finished = true;
			client_connected = false;
			active_client_fd = -1;
			update_uartmuxsel_gpio(false);
			break;
		}

		/* Write received data to UART */
		/* Wait for UART TX to be ready */
		k_sem_take(&uart_tx_sem, K_FOREVER);
		int tx_ret = uart_tx(usart6_dev, socket_buf, rc, SYS_FOREVER_US);
		if (tx_ret < 0) {
			LOG_WRN("UART TX error: %d", tx_ret);
			/* Release semaphore on error */
			k_sem_give(&uart_tx_sem);
		}
	}

	/* Disable UART RX when client disconnects */
	uart_rx_disable(usart6_dev);

	client_connected = false;
	active_client_fd = -1;
	update_uartmuxsel_gpio(false);
	LOG_INF("Console bridge client disconnected");
}

static void console_bridge_daemon_thread(void *a, void *b, void *c)
{
	int server_fd;
	struct sockaddr_in server_addr;
	int opt;

	server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server_fd < 0) {
		LOG_ERR("socket() failed: %d", errno);
		return;
	}

	/* Set SO_REUSEADDR to allow rebinding */
	opt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		LOG_WRN("Failed to set SO_REUSEADDR: %d", errno);
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(CONSOLE_BRIDGE_PORT);
	server_addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		LOG_ERR("bind() failed: %d", errno);
		close(server_fd);
		return;
	}

	if (listen(server_fd, 1) < 0) {
		LOG_ERR("listen() failed: %d", errno);
		close(server_fd);
		return;
	}

	LOG_INF("Console bridge daemon listening on port %d...", CONSOLE_BRIDGE_PORT);

	while (1) {
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		int client_fd;

		client_fd = accept(server_fd, (struct sockaddr *)&client_addr,
				   &client_addr_len);
		if (client_fd < 0) {
			LOG_ERR("accept() failed: %d", errno);
			continue;
		}

		handle_client(client_fd);

		close(client_fd);
		LOG_INF("Client disconnected. Waiting for new connection...");
	}

	/* Unreachable */
	close(server_fd);
}

const struct uart_config uart_cfg = {.baudrate = 115200,
	.parity = UART_CFG_PARITY_NONE,
	.stop_bits = UART_CFG_STOP_BITS_1,
	.data_bits = UART_CFG_DATA_BITS_8,
	.flow_ctrl = UART_CFG_FLOW_CTRL_NONE};

int console_bridge_init(void)
{
	int ret;

	LOG_INF("Starting console bridge daemon (USART6 -> TCP port %d)...", CONSOLE_BRIDGE_PORT);

	if (!device_is_ready(usart6_dev)) {
		LOG_ERR("USART6 device not ready");
		return -1;
	}

	/* Initialize uartmuxsel GPIO */
#if DT_NODE_EXISTS(UARTMUXSEL_NODE)
	if (!gpio_is_ready_dt(&uartmuxsel_gpio)) {
		LOG_ERR("uartmuxsel GPIO device not ready");
		return -1;
	}

	ret = gpio_pin_configure_dt(&uartmuxsel_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure uartmuxsel GPIO: %d", ret);
		return ret;
	}

	/* Initialize to cleared state (no client connected) */
	uartmuxsel_initialized = true;
	update_uartmuxsel_gpio(false);
	LOG_INF("uartmuxsel GPIO initialized");
#endif

	ret = uart_configure(usart6_dev, &uart_cfg);

	if (ret < 0) {
		LOG_ERR("Configuration is not supported by device or driver,"
			" check the UART settings configuration");
		return -1;
	}
	/* Set up UART callback for async RX */
	ret = uart_callback_set(usart6_dev, uart_callback, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to set UART callback: %d", ret);
		return ret;
	}

	LOG_INF("USART6 callback configured for console bridge");

	/* Start the console bridge daemon thread */
	k_thread_create(&console_bridge_thread_data, console_bridge_stack_area,
			CONSOLE_BRIDGE_STACK_SIZE,
			console_bridge_daemon_thread,
			NULL, NULL, NULL,
			CONSOLE_BRIDGE_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(&console_bridge_thread_data, "console_bridge");

	/* Start the UART TX thread for processing FIFO data */
	k_thread_create(&uart_tx_thread_data, uart_tx_thread_stack_area,
			CONFIG_CONSOLE_BRIDGE_STACK_SIZE,
			uart_tx_thread,
			NULL, NULL, NULL,
			CONSOLE_BRIDGE_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(&uart_tx_thread_data, "uart_tx");

	return 0;
}
