/*
 * SPDX-FileCopyrightText: © 2025-2026 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __FS_H__
#define __FS_H__

#ifdef CONFIG_PERSISTENT_STORAGE
int fs_init(void);
int fs_exit(void);
bool fs_enabled(void);
ssize_t config_read(void *buf, size_t size);
ssize_t config_write(const void *buf, size_t size);
int config_clear(void);
#else
static inline int fs_init(void) { return 0; }
static inline int fs_exit(void) { return 0; }
static inline bool fs_enabled(void) { return false; }
static inline ssize_t config_read(void *buf, size_t size) { return -EIO; }
static inline ssize_t config_write(const void *buf, size_t size) { return -EIO; }
static inline int config_clear(void) { return 0; }
#endif

#endif
