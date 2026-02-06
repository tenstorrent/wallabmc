/* Networking */

/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wallabmc_net, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <errno.h>
#include <stdio.h>

#include <zephyr/net/net_config.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>

#include "net.h"
#include "dhcp.h"
#include "config.h"
#include "ntp.h"

int net_do_set_hostname(const char *hostname)
{
	int rc;

	rc = net_hostname_set(hostname, strlen(hostname));
	if (rc) {
		return rc;
	}

	if (config_bmc_use_dhcp4()) {
		rc = restart_dhcp4();
		if (rc) {
			return rc;
		}
	}

	return rc;
}

int net_do_set_default_ip4(uint32_t ip4_addr)
{
	struct net_if *iface = net_if_get_default();
	struct in_addr addr;
	struct net_if_addr *if_addr;
	struct net_if_ipv4 *ipv4;

	if (!iface) {
		LOG_ERR("No default interface to set IPv4 address");
		return -ENOENT;
	}

	/* Remove existing MANUAL/OVERRIDABLE */
	ipv4 = iface->config.ip.ipv4;
	if (ipv4) {
		for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
			if (!ipv4->unicast[i].ipv4.is_used)
				continue;

			if (ipv4->unicast[i].ipv4.addr_type == NET_ADDR_MANUAL ||
			    ipv4->unicast[i].ipv4.addr_type == NET_ADDR_OVERRIDABLE) {
				/* XXX: Check return value? */
				net_if_ipv4_addr_rm(iface, &ipv4->unicast[i].ipv4.address.in_addr);
			}
		}
	}

	/* Just remove any manual address if this was 0 */
	if (!ip4_addr)
		return 0;

	addr.s_addr = ip4_addr;
	if_addr = net_if_ipv4_addr_add(iface, &addr, NET_ADDR_OVERRIDABLE, 0);
	if (!if_addr) {
		LOG_ERR("Failed to add IPv4 address");
		return -EINVAL;
	}

	/*
	 * Could also allow netmask and gw set -
	 * net_if_ipv4_set_netmask(iface, &addr);
	 * net_if_ipv4_set_gw(iface, &addr);
	 */

	return 0;
}

int net_start_dhcp4(void)
{
	return start_dhcp4();
}

int net_stop_dhcp4(void)
{
	int rc;

	rc = stop_dhcp4();
	if (rc)
		return rc;

	return 0;
}

int net_init(void)
{
	uint32_t ip4_addr;
	int rc;

	ip4_addr = config_bmc_default_ip4();
	if (ip4_addr) {
		rc = net_do_set_default_ip4(ip4_addr);
		if (rc) {
			LOG_ERR("Static IPv4 init failed");
			return rc;
		}
	}

	rc = dhcp4_init();
	if (rc) {
		LOG_ERR("DHCPv4 init failed");
		return rc;
	}

	rc = net_config_init_app(NULL, "Initializing network");
	if (rc) {
		LOG_ERR("Network init failed");
		return rc;
	}

	/*
	 * Net init always starts dhcp if it is in the Kconfig.
	 * Stop it if our config does not want it. This is
	 * somewhat hacky.
	 */
	if (!config_bmc_use_dhcp4()) {
		if (net_stop_dhcp4())
			LOG_ERR("DHCPv4 stop failed, continuing");
	}

	LOG_INF("Network hostname: %s", net_hostname_get());

	LOG_DBG("NTP init");
	rc = ntp_init();
	if (rc)
		LOG_ERR("NTP init failed");

	return 0;
}
