/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <arpa/inet.h>
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/if_ether.h>

#include <kconfig.h>
#include <log.h>
#include <os_nif.h>
#include <ovsdb_sync.h>
#include <ovsdb_table.h>
#include <schema.h>

ovsdb_table_t table_DHCP_leased_IP;
ovsdb_table_t table_Bridge;

static ev_timer g_arping_clients_timer; /* Periodic arping to keep sleepy ethernet clients awake */

struct arping_frame /* ARP Request who-has IP */
{
    struct ether_header header;
    struct ether_arp req;
};

static int arping_ovs_mac_learn_aging_time_get()
{
    int ret = CONFIG_MANAGER_NM_ARPING_CLIENTS_PERIOD_DEFAULT * 2;
    int count;
    struct schema_Bridge *bridge;
    json_t *where;
    where = ovsdb_where_simple_typed(SCHEMA_COLUMN(Bridge, name), CONFIG_TARGET_LAN_BRIDGE_NAME, OCLM_STR);
    bridge = ovsdb_table_select_where(&table_Bridge, where, &count);
    if (count == 1)
    {
        int i;
        for (i = 0; i < bridge->other_config_len; i++)
        {
            if (strcmp(bridge->other_config_keys[i], "mac-aging-time") == 0)
            {
                int mac_aging_time = atoi(bridge->other_config[i]);
                if (mac_aging_time <= 0)
                {
                    LOGD("arping: Could not convert %s to integer.", bridge->other_config_keys[i]);
                    break;
                }
                ret = mac_aging_time;
                LOGT("arping: Found mac-aging-time in Bridge other_config: %d seconds.", ret);
                break;
            }
        }
    }
    FREE(bridge);
    return ret;
}

static int arping_brctl_mac_learn_aging_time_get()
{
    int ret = CONFIG_MANAGER_NM_ARPING_CLIENTS_PERIOD_DEFAULT * 2;
    char path[C_MAXPATH_LEN] = {0};
    snprintf(path, C_MAXPATH_LEN, "/sys/class/net/%s/bridge/ageing_time", CONFIG_TARGET_LAN_BRIDGE_NAME);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        LOGD("arping: Could not open /sys/class/net/%s/bridge/ageing_time. Error: %s",
             CONFIG_TARGET_LAN_BRIDGE_NAME,
             strerror(errno));
        return ret;
    }
    char buf[8] = {0};
    if (read(fd, buf, 8) < 0)
    {
        LOGD("arping: Could not read /sys/class/net/%s/bridge/ageing_time. Error: %s",
             CONFIG_TARGET_LAN_BRIDGE_NAME,
             strerror(errno));
        close(fd);
        return ret;
    }
    close(fd);
    int ageing_time = atoi(buf);
    if (ageing_time <= 0)
    {
        LOGD("arping: Could not convert %s to integer.", buf);
        return ret;
    }
    ret = ageing_time / 100;
    return ret;
}

static int arping_period_get()
{
    int period = CONFIG_MANAGER_NM_ARPING_CLIENTS_PERIOD_DEFAULT;
    if (kconfig_enabled(CONFIG_TARGET_USE_NATIVE_BRIDGE))
    {
        period = arping_brctl_mac_learn_aging_time_get() / 2;
        LOGT("arping: native bridge, period %d seconds", period);
    }
    else
    {
        period = arping_ovs_mac_learn_aging_time_get() / 2;
        LOGT("arping: ovs bridge, period %d seconds", period);
    }
    return period;
}

static bool arping_frame_fill_common_part(struct arping_frame *frame)
{
    MEMZERO(*frame);
    frame->header.ether_type = htons(ETH_P_ARP);
    frame->req.arp_hrd = htons(ARPHRD_ETHER);
    frame->req.arp_pro = htons(ETH_P_IP);
    frame->req.arp_hln = ETHER_ADDR_LEN;
    frame->req.arp_pln = sizeof(in_addr_t);
    frame->req.arp_op = htons(ARPOP_REQUEST);
    MEMZERO(frame->req.arp_tha);

    /* common part includes source mac and ip (while destination mac and ip are per DHCP_leased_IP entry) */
    os_macaddr_t src_mac;
    if (os_nif_macaddr_get(CONFIG_TARGET_LAN_BRIDGE_NAME, &src_mac) == false)
    {
        LOGD("arping: Cannot get source mac address on interface %s", CONFIG_TARGET_LAN_BRIDGE_NAME);
        return false;
    }
    memcpy(frame->header.ether_shost, src_mac.addr, sizeof(frame->header.ether_shost));
    memcpy(frame->req.arp_sha, src_mac.addr, sizeof(frame->req.arp_sha));

    os_ipaddr_t src_ip;
    if (os_nif_ipaddr_get(CONFIG_TARGET_LAN_BRIDGE_NAME, &src_ip) == false)
    {
        LOGD("arping: Cannot get source ip address on interface %s", CONFIG_TARGET_LAN_BRIDGE_NAME);
        return false;
    }
    memcpy(frame->req.arp_spa, src_ip.addr, sizeof(frame->req.arp_spa));
    return true;
}

static bool arping_frame_fill_dest_mac_ip(const char *hwaddr, const char *inet_addr, struct arping_frame *frame)
{
    uint8_t hwaddr_n[ETH_ALEN] = {0};
    if (hwaddr_aton(hwaddr, hwaddr_n) != 0)
    {
        LOGD("arping: %s is not a valid MAC address", hwaddr);
        return false;
    }
    memcpy(frame->header.ether_dhost, hwaddr_n, sizeof(frame->header.ether_dhost));
    memcpy(frame->req.arp_tha, hwaddr_n, sizeof(frame->req.arp_tha));

    struct in_addr target_ip_addr = {0};
    if (!inet_aton(inet_addr, &target_ip_addr))
    {
        LOGD("arping: %s is not a valid IPv4 address", inet_addr);
        return false;
    }
    memcpy(&(frame->req.arp_tpa), &target_ip_addr.s_addr, sizeof(frame->req.arp_tpa));
    return true;
}

static void arping_ipv4_clients()
{
    struct schema_DHCP_leased_IP *leased_ip;
    void *leased_ips;

    int count;
    struct arping_frame frame;
    leased_ips = ovsdb_table_select_where(&table_DHCP_leased_IP, NULL, &count);
    if ((count <= 0) || (arping_frame_fill_common_part(&frame) == false))
    {
        FREE(leased_ips);
        return;
    }

    const int fd = socket(AF_PACKET, SOCK_RAW, 0);
    if (fd < 0)
    {
        LOGE("arping: Cannot create raw socket: error %d, %s", errno, strerror(errno));
        FREE(leased_ips);
        return;
    }
    const unsigned int if_index = if_nametoindex(CONFIG_TARGET_LAN_BRIDGE_NAME);
    struct sockaddr_ll saddr;
    MEMZERO(saddr);
    saddr.sll_ifindex = if_index;
    saddr.sll_halen = ETH_ALEN;

    int i;
    for (i = 0; i < count; i++)
    {
        leased_ip = (struct schema_DHCP_leased_IP *)(leased_ips + table_DHCP_leased_IP.schema_size * i);
        if (strncmp(leased_ip->inet_addr, "169.254", 7) == 0)
        {
            LOGT("arping: Skipped arpinging IPv4 link local address %s", leased_ip->inet_addr);
            continue;
        }
        LOGT("arping: leased_ip inet_addr %s, hwaddr %s, interface %s",
             leased_ip->inet_addr,
             leased_ip->hwaddr,
             CONFIG_TARGET_LAN_BRIDGE_NAME);

        if (arping_frame_fill_dest_mac_ip(leased_ip->hwaddr, leased_ip->inet_addr, &frame) == false)
        {
            continue;
        }
        memcpy(&saddr.sll_addr, &frame, ETH_ALEN); /* First 6 bytes in frame is destination mac address */

        const int send_err = sendto(fd, &frame, sizeof(frame), 0, (struct sockaddr *)&saddr, sizeof(saddr));
        if (send_err < 0)
        {
            LOGD("arping: sendto for %s failed with error %d, %s", leased_ip->inet_addr, errno, strerror(errno));
        }
    }
    FREE(leased_ips);
    close(fd);
}

static void arping_ipv6_clients()
{
    /* ff02::1 - IPv6 multicast all nodes */
    const char *result = strexa("ping6", "-I", CONFIG_TARGET_LAN_BRIDGE_NAME, "-c", "1", "ff02::1");
    if (result != NULL)
    {
        char *round_trip = strstr(result, "round-trip");
        if (round_trip != NULL)
        {
            LOGT("arping: ipv6 clients: ping6 -I %s -c 1 ff02::1; Success, %s",
                 CONFIG_TARGET_LAN_BRIDGE_NAME,
                 round_trip);
        }
        else
        {
            LOGD("arping: ipv6 clients: No round-trip found in response: %s", result);
        }
    }
    else
    {
        LOGD("arping: ipv6 clients: strexa returned NULL");
    }
}

static void arping_clients_timer_fn(struct ev_loop *loop, ev_timer *w, int revents)
{
    g_arping_clients_timer.repeat = arping_period_get();
    ev_timer_again(EV_DEFAULT, &g_arping_clients_timer);

    arping_ipv6_clients();
    arping_ipv4_clients();
}

bool nm2_arping_clients_init()
{
    OVSDB_TABLE_INIT(DHCP_leased_IP, _uuid);
    OVSDB_TABLE_INIT(Bridge, _uuid);
    ev_init(&g_arping_clients_timer, arping_clients_timer_fn);
    g_arping_clients_timer.repeat = arping_period_get();
    ev_timer_again(EV_DEFAULT, &g_arping_clients_timer);
    return true;
}
