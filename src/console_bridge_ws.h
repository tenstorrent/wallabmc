/*
 * SPDX-FileCopyrightText: © 2025-2026 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef CONFIG_CONSOLE_BRIDGE
int console_bridge_ws_init(void);
#else
static inline int console_bridge_ws_init(void) { return 0; }
#endif
