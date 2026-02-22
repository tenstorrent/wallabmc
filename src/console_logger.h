/*
 * SPDX-FileCopyrightText: © 2025-2026 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef CONFIG_CONSOLE_LOGGER
int console_logger_init(void);
ssize_t host_console_read(uint8_t *buf, size_t size, uint64_t *ppos, k_timeout_t timeout);
ssize_t host_console_write(const uint8_t *buf, size_t size);
#else
static inline int console_logger_init(void) { return 0; }
#endif
