/*
 * SPDX-FileCopyrightText: © 2025-2026 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __VPD_H__
#define __VPD_H__

int get_bmc_uuid_string(const char **str);
int vpd_init(void);

#endif
