/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CONFIG_H__
#define __CONFIG_H__
int config_init(void);
int config_clear(void);
int config_bmc_hostname_set(const char *hostname);
int config_bmc_password_set(const char *password);
uint32_t config_bmc_default_ip4(void);
int config_bmc_default_ip4_set(const char *str);
bool config_bmc_use_dhcp4(void);
int config_bmc_use_dhcp4_set(bool use);
bool config_host_auto_poweron(void);
int config_host_auto_poweron_set(bool on);
const char *config_bmc_admin_password(void);
#endif
