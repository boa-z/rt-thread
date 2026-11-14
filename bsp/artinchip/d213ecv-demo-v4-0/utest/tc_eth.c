/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ethernet GMAC0 / DHCP regression.
 *
 * The d213-eth-utest image enables BSP_USING_GMAC0 and lwIP with DHCP.
 * The MAC driver registers netif "e0"; its PHY poll thread brings the
 * link up and the DHCP client obtains an address from the router.  A
 * lease proves the PHY/MAC configuration and both DMA directions.
 */

#include <rtthread.h>

#include "utest.h"

#ifdef BSP_USING_GMAC0

#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/ip4_addr.h"

#define TC_ETH_NETIF_NAME    "e0"
#define TC_ETH_LINK_WAIT_MS  10000
#define TC_ETH_DHCP_WAIT_MS  20000

static void tc_eth_dhcp(void)
{
    struct netif *netif = RT_NULL;
    rt_tick_t deadline;
    char ip[16];
    char mask[16];
    char gw[16];

    deadline = rt_tick_get() + rt_tick_from_millisecond(TC_ETH_LINK_WAIT_MS);
    do {
        netif = netif_find(TC_ETH_NETIF_NAME);
        if (netif != RT_NULL && netif_is_link_up(netif))
            break;
        rt_thread_mdelay(100);
    } while (rt_tick_get() < deadline);

    uassert_not_null(netif);
    if (netif == RT_NULL)
        return;
    uassert_true(netif_is_link_up(netif));

    deadline = rt_tick_get() + rt_tick_from_millisecond(TC_ETH_DHCP_WAIT_MS);
    while (rt_tick_get() < deadline && ip_addr_isany_val(netif->ip_addr))
        rt_thread_mdelay(100);

    uassert_false(ip_addr_isany_val(netif->ip_addr));
    uassert_false(ip_addr_isany_val(netif->netmask));

    rt_strncpy(ip, ip4addr_ntoa(ip_2_ip4(&netif->ip_addr)), sizeof(ip));
    rt_strncpy(mask, ip4addr_ntoa(ip_2_ip4(&netif->netmask)), sizeof(mask));
    rt_strncpy(gw, ip4addr_ntoa(ip_2_ip4(&netif->gw)), sizeof(gw));
    rt_kprintf("eth: e0 link up, ip %s mask %s gw %s\n", ip, mask, gw);
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_eth_dhcp);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.eth", RT_NULL, RT_NULL, 40);

#endif /* BSP_USING_GMAC0 */
