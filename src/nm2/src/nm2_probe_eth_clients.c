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
#include <netdb.h>
#include <netinet/icmp6.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>

#include <ds_tree.h>
#include <kconfig.h>
#include <log.h>
#include <os_nif.h>
#include <ovsdb_cache.h>
#include <ovsdb_sync.h>
#include <ovsdb_table.h>
#include <schema.h>

#define HWADDR_LEN  18 /* hwaddr MaxLength 17 in opensync.ovsschema */
#define ADDRESS_LEN 50 /* address MaxLength 49 in opensync.ovsschema */

/*
Periodic probing to prevent ethernet clients being deleted from OVS_MAC_Learning table
(and consequently from NOC topology view) due to inactivity .

Ethernet clients that have ageing time more than half of ageout timeout, are being probed for
response, this way to reset their ageing time value in the FDB.
Probing methods are arping (ARP Request) for IPv4 and ndisc6 (ICMP6 Neighbor Solicitation) for IPv6.

For legacy OVS-bridge platforms, all ethernet clients are being probed regardless of their
current ageing time. A big enough ageing time is set at client creation and then never updated
with readings from FDB.
*/
static ev_timer g_probe_eth_clients_timer;

ovsdb_table_t table_Bridge;
ovsdb_table_t table_OVS_MAC_Learning;
ovsdb_table_t table_IPv4_Neighbors;
ovsdb_table_t table_IPv6_Neighbors;

struct eth_client /* MAC and IP address of ethernet client */
{
    char hwaddr[HWADDR_LEN];
    char address[ADDRESS_LEN];
    uint32_t ageing_time; /* in seconds */

    uint8_t hwaddr_n[ETH_ALEN];
    union
    {
        struct in_addr v4;
        struct in6_addr v6;
    } address_n;

    ds_tree_node_t tnode;
};

struct arping_frame
{
    struct ether_header header;
    struct ether_arp req;
};

struct solicit_packet
{
    struct nd_neighbor_solicit ns_hdr;
    struct nd_opt_hdr opt;
    uint8_t hw_addr[6];
};

static ds_tree_t g_ipv4_eth_clients = DS_TREE_INIT(ds_str_cmp, struct eth_client, tnode);
static ds_tree_t g_ipv6_eth_clients = DS_TREE_INIT(ds_str_cmp, struct eth_client, tnode);

static int ovs_mac_learn_aging_time_get(void)
{
    int ret = CONFIG_MANAGER_NM_PROBE_ETH_CLIENTS_PERIOD_DEFAULT * 2;
    struct schema_Bridge *bridge = ovsdb_cache_find_by_key(&table_Bridge, CONFIG_TARGET_LAN_BRIDGE_NAME);
    if (bridge != NULL)
    {
        int i;
        for (i = 0; i < bridge->other_config_len; i++)
        {
            if (strcmp(bridge->other_config_keys[i], "mac-aging-time") == 0)
            {
                int mac_aging_time = atoi(bridge->other_config[i]);
                if (mac_aging_time <= 0)
                {
                    LOGD("probe_eth_clients: could not convert %s to integer.", bridge->other_config_keys[i]);
                    break;
                }
                ret = mac_aging_time;
                LOGT("probe_eth_clients: found mac-aging-time in Bridge other_config: %d seconds.", ret);
                break;
            }
        }
    }
    return ret;
}

static int brctl_mac_learn_aging_time_get(void)
{
    int ret = CONFIG_MANAGER_NM_PROBE_ETH_CLIENTS_PERIOD_DEFAULT * 2;
    char path[C_MAXPATH_LEN] = {0};
    snprintf(path, C_MAXPATH_LEN, "/sys/class/net/%s/bridge/ageing_time", CONFIG_TARGET_LAN_BRIDGE_NAME);
    char *ageing_time_str = file_geta(path);
    int ageing_time = atoi(ageing_time_str);
    if (ageing_time <= 0)
    {
        LOGD("probe_eth_clients: could not convert %s to integer.", ageing_time_str);
        return ret;
    }
    ret = ageing_time / 100;
    return ret;
}

static int probe_period_get(void)
{
    int period = CONFIG_MANAGER_NM_PROBE_ETH_CLIENTS_PERIOD_DEFAULT;
    if (kconfig_enabled(CONFIG_TARGET_USE_NATIVE_BRIDGE))
    {
        period = brctl_mac_learn_aging_time_get() / 2;
        LOGT("probe_eth_clients: native bridge, period %d seconds", period);
    }
    else
    {
        period = ovs_mac_learn_aging_time_get() / 2;
        LOGT("probe_eth_clients: ovs bridge, period %d seconds", period);
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

    /* common part includes source mac and ip */
    os_macaddr_t src_mac;
    if (os_nif_macaddr_get(CONFIG_TARGET_LAN_BRIDGE_NAME, &src_mac) == false)
    {
        LOGD("probe_eth_clients: cannot get source mac address on interface %s", CONFIG_TARGET_LAN_BRIDGE_NAME);
        return false;
    }
    memcpy(frame->header.ether_shost, src_mac.addr, sizeof(frame->header.ether_shost));
    memcpy(frame->req.arp_sha, src_mac.addr, sizeof(frame->req.arp_sha));

    os_ipaddr_t src_ip;
    if (os_nif_ipaddr_get(CONFIG_TARGET_LAN_BRIDGE_NAME, &src_ip) == false)
    {
        LOGD("probe_eth_clients: cannot get source ip address on interface %s", CONFIG_TARGET_LAN_BRIDGE_NAME);
        return false;
    }
    memcpy(frame->req.arp_spa, src_ip.addr, sizeof(frame->req.arp_spa));
    return true;
}

static bool arping_frame_fill_dest_mac_ip(const struct eth_client *ec, struct arping_frame *frame)
{
    memcpy(frame->header.ether_dhost, ec->hwaddr_n, sizeof(frame->header.ether_dhost));
    memcpy(frame->req.arp_tha, ec->hwaddr_n, sizeof(frame->req.arp_tha));
    memcpy(&(frame->req.arp_tpa), &ec->address_n.v4.s_addr, sizeof(frame->req.arp_tpa));
    return true;
}

static struct eth_client *new_client(char *hwaddr, char *address)
{
    struct eth_client *ethclient = CALLOC(1, sizeof(struct eth_client)); /* CALLOC requires FREE */
    STRSCPY(ethclient->hwaddr, hwaddr);
    STRSCPY(ethclient->address, address);
    ethclient->ageing_time = 2 * g_probe_eth_clients_timer.repeat;
    if (hwaddr_aton(hwaddr, ethclient->hwaddr_n) != 0)
    {
        LOGD("probe_eth_clients: %s is not a valid MAC address", hwaddr);
    }
    struct in_addr inaddr;
    MEMZERO(inaddr);
    struct in6_addr in6addr;
    MEMZERO(in6addr);
    if (inet_pton(AF_INET, address, &inaddr) == 1)
    {
        memcpy(&ethclient->address_n.v4, &inaddr, sizeof(ethclient->address_n.v4));
    }
    else if (inet_pton(AF_INET6, address, &in6addr) == 1)
    {
        memcpy(&ethclient->address_n.v6, &in6addr, sizeof(ethclient->address_n.v6));
    }
    else
    {
        if (strlen(address) != 0)
        {
            LOGD("probe_eth_clients: %s is not a valid IP address", address);
        }
    }

    return ethclient;
}

static int ipv4_get_raw_unblocking_socket(void)
{
    const int fd = socket(AF_PACKET, SOCK_RAW, 0);
    if (fd < 0)
    {
        LOGE("probe_eth_clients: error creating socket: %d, %s", errno, strerror(errno));
        return fd;
    }
    int flags = fcntl(fd, F_GETFL);
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        LOGE("probe_eth_clients: error setting socket to non-blocking mode: %d, %s", errno, strerror(errno));
        close(fd);
        return -1;
    }
    return fd; /* requires close(fd) after use */
}

static void probe_ipv4(void)
{
    if (ds_tree_is_empty(&g_ipv4_eth_clients))
    {
        LOGT("probe_eth_clients: ipv4 client list is empty");
        return;
    }

    LOGT("probe_eth_clients: dumping ipv4 clients list");
    struct eth_client *ec;
    ds_tree_foreach (&g_ipv4_eth_clients, ec)
    {
        LOGT("probe_eth_clients: [hwaddr: %s, address: %s, age: %d]", ec->hwaddr, ec->address, ec->ageing_time);
    }

    struct arping_frame frame;
    if (arping_frame_fill_common_part(&frame) == false) return;

    const int fd = ipv4_get_raw_unblocking_socket();
    if (fd < 0) return;

    struct sockaddr_ll saddr;
    const unsigned int if_index = if_nametoindex(CONFIG_TARGET_LAN_BRIDGE_NAME);
    if (if_index == 0)
    {
        LOGD("probe_eth_clients: if_nametoindex error %d, %s", errno, strerror(errno));
        return;
    }
    MEMZERO(saddr);
    saddr.sll_ifindex = if_index;
    saddr.sll_halen = ETH_ALEN;

    char hwaddr[HWADDR_LEN] = {0};
    char address[ADDRESS_LEN] = {0};

    ds_tree_foreach (&g_ipv4_eth_clients, ec)
    {
        if (strlen(ec->address) == 0) continue;
        if (ec->ageing_time < g_probe_eth_clients_timer.repeat) continue;
        STRSCPY(hwaddr, ec->hwaddr);
        STRSCPY(address, ec->address);
        if (arping_frame_fill_dest_mac_ip(ec, &frame) == false) continue;
        memcpy(&saddr.sll_addr, &frame, ETH_ALEN); /* First 6 bytes in frame is destination mac address */
        const int send_err = sendto(fd, &frame, sizeof(frame), 0, (struct sockaddr *)&saddr, sizeof(saddr));
        if (send_err < 0)
        {
            LOGI("probe_eth_clients: sendto for [%s, %s] failed with error %d, %s",
                 hwaddr,
                 address,
                 errno,
                 strerror(errno));
            continue;
        }
        LOGT("probe_eth_clients: ARP Request sent to ipv4 eth client [%s, %s] ", hwaddr, address);
    }
    close(fd);
}

static void ipv6_ll_address_from_hwaddr(const char *hwaddr, char *address)
{
    os_macaddr_t macaddress;
    if (os_nif_macaddr_from_str(&macaddress, hwaddr) == false)
    {
        LOGD("probe_eth_clients: can not convert to mac address: %s", hwaddr);
        return;
    }
    struct in6_addr lladdr;
    lladdr.s6_addr[0] = 0xfe;
    lladdr.s6_addr[1] = 0x80;
    for (int i = 2; i < 8; i++)
    {
        lladdr.s6_addr[i] = 0x00;
    }
    lladdr.s6_addr[8] = macaddress.addr[0] ^ 2;
    lladdr.s6_addr[9] = macaddress.addr[1];
    lladdr.s6_addr[10] = macaddress.addr[2];
    lladdr.s6_addr[11] = 0xff;
    lladdr.s6_addr[12] = 0xfe;
    lladdr.s6_addr[13] = macaddress.addr[3];
    lladdr.s6_addr[14] = macaddress.addr[4];
    lladdr.s6_addr[15] = macaddress.addr[5];
    inet_ntop(AF_INET6, &lladdr, address, INET6_ADDRSTRLEN);
}

/* from ndisc6 */
static int getipv6byname(const char *name, const char *ifname, struct sockaddr_in6 *addr)
{
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_RAW;
    hints.ai_flags = AI_NUMERICHOST;

    int val = getaddrinfo(name, NULL, &hints, &res);
    if (val)
    {
        LOGD("probe_eth_clients: getaddrinfo failed, error %d: %s", errno, strerror(errno));
        return -1;
    }
    memcpy(addr, res->ai_addr, sizeof(struct sockaddr_in6));
    freeaddrinfo(res);

    val = if_nametoindex(ifname);
    if (val == 0)
    {
        LOGT("probe_eth_clients: if_nametoindex failed with error %d: %s", errno, strerror(errno));
        return -1;
    }
    addr->sin6_scope_id = val;
    return 0;
}

/* from ndisc6 */
static size_t buildsol(struct solicit_packet *ns, struct sockaddr_in6 *tgt, const char *ifname, const char *hwaddr)
{
    /* builds ICMPv6 Neighbor Solicitation packet */
    ns->ns_hdr.nd_ns_type = ND_NEIGHBOR_SOLICIT;
    ns->ns_hdr.nd_ns_code = 0;
    ns->ns_hdr.nd_ns_cksum = 0; /* computed by the kernel */
    ns->ns_hdr.nd_ns_reserved = 0;

    memcpy(&ns->ns_hdr.nd_ns_target, &tgt->sin6_addr, sizeof(ns->ns_hdr.nd_ns_target));

    os_macaddr_t src_mac;
    if (os_nif_macaddr_get(ifname, &src_mac) == false)
    {
        LOGT("probe_eth_clients: can not get source mac address on interface %s", ifname);
        return sizeof(ns->ns_hdr);
    }
    memcpy(ns->hw_addr, &src_mac, sizeof(ns->hw_addr));

    ns->opt.nd_opt_type = ND_OPT_SOURCE_LINKADDR;
    ns->opt.nd_opt_len = 1;
    return sizeof(*ns);
}

/* from ndisc6 */
static int sethoplimit(int fd, int value)
{
    return (setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &value, sizeof(value))
            || setsockopt(fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &value, sizeof(value)))
                   ? -1
                   : 0;
}

static int ipv6_get_icmpv6_unblocking_socket(void)
{
    const int fd = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
    if (fd < 0)
    {
        LOGE("probe_eth_clients: error creating icmpv6 socket: %d, %s", errno, strerror(errno));
        return fd;
    }
    int flags = fcntl(fd, F_GETFL);
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        LOGE("probe_eth_clients: error setting icmpv6 socket to non-blocking mode: %d, %s", errno, strerror(errno));
        close(fd);
        return -1;
    }
    return fd; /* requires close(fd) after use */
}

static void ipv6_set_sock_opts(const int fd)
{
    struct icmp6_filter f;
    ICMP6_FILTER_SETBLOCKALL(&f);
    ICMP6_FILTER_SETPASS(ND_NEIGHBOR_ADVERT, &f);
    setsockopt(fd, IPPROTO_ICMPV6, ICMP6_FILTER, &f, sizeof(f));
    setsockopt(fd, SOL_SOCKET, SO_DONTROUTE, &(int){1}, sizeof(int));
    sethoplimit(fd, 255);
    setsockopt(fd, IPPROTO_IPV6, IPV6_RECVHOPLIMIT, &(int){1}, sizeof(int));
}

static void probe_ipv6(void)
{
    if (ds_tree_is_empty(&g_ipv6_eth_clients))
    {
        LOGT("probe_eth_clients: ipv6 client list is empty");
        return;
    }

    struct eth_client *ec;
    LOGT("probe_eth_clients: dumping ipv6 clients list");
    ds_tree_foreach (&g_ipv6_eth_clients, ec)
    {
        LOGT("probe_eth_clients: [hwaddr: %s, address: %s, age: %d]", ec->hwaddr, ec->address, ec->ageing_time);
    }
    int fd = ipv6_get_icmpv6_unblocking_socket();
    if (fd < 0) return;
    ipv6_set_sock_opts(fd);

    char hwaddr[HWADDR_LEN] = {0};
    char address[ADDRESS_LEN] = {0};
    ds_tree_foreach (&g_ipv6_eth_clients, ec)
    {
        if (ec->ageing_time < g_probe_eth_clients_timer.repeat) continue;
        MEMZERO(hwaddr);
        MEMZERO(address);
        STRSCPY(hwaddr, ec->hwaddr);
        if (strlen(ec->address) == 0)
            ipv6_ll_address_from_hwaddr(hwaddr, address);
        else
            STRSCPY(address, ec->address);

        /* from ndisc6 */
        struct solicit_packet packet;
        ssize_t plen;
        struct sockaddr_in6 tgt;
        MEMZERO(tgt);
        if (getipv6byname(address, CONFIG_TARGET_LAN_BRIDGE_NAME, &tgt)) continue;
        plen = buildsol(&packet, &tgt, CONFIG_TARGET_LAN_BRIDGE_NAME, hwaddr);
        if (plen <= 0) continue;
        const int send_err = sendto(fd, &packet, plen, 0, (const struct sockaddr *)&tgt, sizeof(tgt));
        if (send_err < 0)
        {
            LOGI("probe_eth_clients: sendto for [%s, %s] failed with error %d, %s",
                 hwaddr,
                 address,
                 errno,
                 strerror(errno));
            continue;
        }
        LOGT("probe_eth_clients: Neighbor Solicitation sendto called, ipv6 eth client [%s, %s]", hwaddr, address);
    }
    close(fd);
}

/* from if_bridge.h, used in bridge-utils, fits brforward file content */
struct __fdb_entry
{
    __u8 mac_addr[ETH_ALEN];
    __u8 port_no;
    __u8 is_local;
    __u32 ageing_timer_value; /* in hundredths of second, value 100 means 1 second */
    __u8 port_hi;
    __u8 pad0;
    __u16 unused;
};

static void update_ageing_times(void)
{
    if (!kconfig_enabled(CONFIG_TARGET_USE_NATIVE_BRIDGE))
    {
        LOGT("probe_eth_clients: ovs bridge, omitting update ageing times");
        return;
    }

    FILE *f;
    int n = 0;
    const size_t chunk = 128;
    struct __fdb_entry fdbs[chunk];

    /* from bridge-utils, brctl showmacs */
    const char *brforward_file_path = "/sys/class/net/" CONFIG_TARGET_LAN_BRIDGE_NAME "/brforward";
    LOGT("probe_eth_clients: reading fdb entries from %s", brforward_file_path);
    f = fopen(brforward_file_path, "r");
    if (f == NULL) return;

    fseek(f, 0, SEEK_SET);
    n = fread(fdbs, sizeof(struct __fdb_entry), chunk, f);
    fclose(f);
    LOGT("probe_eth_clients: found %d fdb entries", n);

    int i;
    for (i = 0; i < n; i++)
    {
        if (fdbs[i].is_local == 0)
        {
            char hwaddr[32] = {0};
            os_macaddr_t hwaddr_os = {0};
            memcpy(hwaddr_os.addr, fdbs[i].mac_addr, ETH_ALEN);
            os_nif_macaddr_to_str(&hwaddr_os, hwaddr, PRI_os_macaddr_lower_t);
            LOGT("probe_eth_clients: non-local fdb_entry mac_addr %s, ageing_timer_value %d",
                 hwaddr,
                 fdbs[i].ageing_timer_value);

            struct eth_client *ipv4ethclient = ds_tree_find(&g_ipv4_eth_clients, hwaddr);
            if (ipv4ethclient)
            {
                ipv4ethclient->ageing_time = fdbs[i].ageing_timer_value / 100;
                LOGT("probe_eth_clients: updated ipv4ethclient %s, ageing time %d", hwaddr, ipv4ethclient->ageing_time);
            }
            struct eth_client *ipv6ethclient = ds_tree_find(&g_ipv6_eth_clients, hwaddr);
            if (ipv6ethclient)
            {
                ipv6ethclient->ageing_time = fdbs[i].ageing_timer_value / 100;
                LOGT("probe_eth_clients: updated ipv6ethclient %s, ageing time %d", hwaddr, ipv6ethclient->ageing_time);
            }
        }
    }
}

static void probe_eth_clients(void)
{
    update_ageing_times();
    probe_ipv4();
    probe_ipv6();
}

static void probe_eth_clients_timer_fn(struct ev_loop *loop, ev_timer *w, int revents)
{
    g_probe_eth_clients_timer.repeat = probe_period_get();
    ev_timer_again(EV_DEFAULT, &g_probe_eth_clients_timer);
    probe_eth_clients();
}

static void find_ipv4address_by_hwaddr(char hwaddr[HWADDR_LEN], char ipv4address[ADDRESS_LEN])
{
    memset(ipv4address, 0, ADDRESS_LEN);
    char myhwaddr[HWADDR_LEN] = {0};
    STRSCPY(myhwaddr, hwaddr);
    str_toupper(myhwaddr);
    struct schema_IPv4_Neighbors *ipv4n = ovsdb_cache_find_by_key(&table_IPv4_Neighbors, myhwaddr);
    if (ipv4n == NULL)
    {
        str_tolower(myhwaddr);
        ipv4n = ovsdb_cache_find_by_key(&table_IPv4_Neighbors, myhwaddr);
    }
    if (ipv4n != NULL)
    {
        strncpy(ipv4address, ipv4n->address, ADDRESS_LEN);
    }
}

static void find_ipv6address_by_hwaddr(char hwaddr[HWADDR_LEN], char ipv6address[ADDRESS_LEN])
{
    memset(ipv6address, 0, ADDRESS_LEN);
    char myhwaddr[HWADDR_LEN] = {0};
    STRSCPY(myhwaddr, hwaddr);
    str_tolower(myhwaddr);
    struct schema_IPv6_Neighbors *ipv6n = ovsdb_cache_find_by_key(&table_IPv6_Neighbors, myhwaddr);
    if (ipv6n == NULL)
    {
        str_toupper(myhwaddr);
        ipv6n = ovsdb_cache_find_by_key(&table_IPv6_Neighbors, myhwaddr);
    }
    if (ipv6n != NULL)
    {
        strncpy(ipv6address, ipv6n->address, ADDRESS_LEN);
    }
}

static void oml_new_modify_handle_ipv4(struct schema_OVS_MAC_Learning *rec)
{
    char ipv4address[ADDRESS_LEN] = {0};
    find_ipv4address_by_hwaddr(rec->hwaddr, ipv4address);
    struct eth_client *ipv4ethclient = ds_tree_find(&g_ipv4_eth_clients, rec->hwaddr);
    if (ipv4ethclient == NULL)
    {
        LOGT("probe_eth_clients: inserting into ipv4 list: [%s, %s]", rec->hwaddr, ipv4address);
        struct eth_client *nc4 = new_client(rec->hwaddr, ipv4address);
        ds_tree_insert(&g_ipv4_eth_clients, nc4, nc4->hwaddr);
    }
    else
    {
        LOGT("probe_eth_clients: updating ipv4 list: [%s, %s]", ipv4ethclient->hwaddr, ipv4address);
        STRSCPY(ipv4ethclient->address, ipv4address);
    }
}

static void oml_new_modify_handle_ipv6(struct schema_OVS_MAC_Learning *rec)
{
    char ipv6address[ADDRESS_LEN] = {0};
    find_ipv6address_by_hwaddr(rec->hwaddr, ipv6address);
    struct eth_client *ipv6ethclient = ds_tree_find(&g_ipv6_eth_clients, rec->hwaddr);
    if (ipv6ethclient == NULL)
    {
        LOGT("probe_eth_clients: inserting into ipv6 list: [%s, %s]", rec->hwaddr, ipv6address);
        struct eth_client *nc6 = new_client(rec->hwaddr, ipv6address);
        ds_tree_insert(&g_ipv6_eth_clients, nc6, nc6->hwaddr);
    }
    else
    {
        LOGT("probe_eth_clients: updating ipv6 list: [%s, %s]", ipv6ethclient->hwaddr, ipv6address);
        STRSCPY(ipv6ethclient->address, ipv6address);
    }
}

static void callback_OVS_MAC_Learning(
        ovsdb_update_monitor_t *mon,
        struct schema_OVS_MAC_Learning *old_rec,
        struct schema_OVS_MAC_Learning *rec,
        ovsdb_cache_row_t *row)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW || mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        LOGT("probe_eth_clients: OVS_MAC_Learning new/modify entry hwaddr %s", rec->hwaddr);
        oml_new_modify_handle_ipv4(rec);
        oml_new_modify_handle_ipv6(rec);
        return;
    }
    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        LOGT("probe_eth_clients: OVS_MAC_Learning delete entry hwaddr %s", rec->hwaddr);
        struct eth_client *ipv4ethclient = ds_tree_find(&g_ipv4_eth_clients, rec->hwaddr);
        if (ipv4ethclient != NULL)
        {
            ds_tree_remove(&g_ipv4_eth_clients, ipv4ethclient);
            FREE(ipv4ethclient);
        }
        struct eth_client *ipv6ethclient = ds_tree_find(&g_ipv6_eth_clients, rec->hwaddr);
        if (ipv6ethclient != NULL)
        {
            ds_tree_remove(&g_ipv6_eth_clients, ipv6ethclient);
            FREE(ipv6ethclient);
        }
        return;
    }
    return;
}

static void update_ipv4_eth_client_address(struct schema_IPv4_Neighbors *rec, bool update_del)
{
    char myhwaddr[HWADDR_LEN] = {0};
    STRSCPY(myhwaddr, rec->hwaddr);
    str_tolower(myhwaddr);
    struct eth_client *ethclient = ds_tree_find(&g_ipv4_eth_clients, myhwaddr);
    if (ethclient == NULL)
    {
        str_toupper(myhwaddr);
        ethclient = ds_tree_find(&g_ipv4_eth_clients, myhwaddr);
    }
    if (ethclient != NULL)
    {
        if (!update_del)
        {
            LOGT("probe_eth_clients: IPv4_Neighbors new/modify, hwaddr %s found, address %s",
                 rec->hwaddr,
                 rec->address);
            STRSCPY(ethclient->address, rec->address);
        }
        else
        {
            LOGT("probe_eth_clients: IPv4_Neighbors delete, hwaddr %s", rec->hwaddr);
            MEMZERO(ethclient->address);
        }
    }
}

static void callback_IPv4_Neighbors(
        ovsdb_update_monitor_t *mon,
        struct schema_IPv4_Neighbors *old_rec,
        struct schema_IPv4_Neighbors *rec,
        ovsdb_cache_row_t *row)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW || mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        update_ipv4_eth_client_address(rec, false);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        update_ipv4_eth_client_address(rec, true);
        return;
    }
    return;
}

static void update_ipv6_eth_client_address(struct schema_IPv6_Neighbors *rec, bool update_del)
{
    char myhwaddr[HWADDR_LEN] = {0};
    STRSCPY(myhwaddr, rec->hwaddr);
    str_tolower(myhwaddr);
    struct eth_client *ethclient = ds_tree_find(&g_ipv6_eth_clients, myhwaddr);
    if (ethclient == NULL)
    {
        str_toupper(myhwaddr);
        ethclient = ds_tree_find(&g_ipv6_eth_clients, myhwaddr);
    }
    if (ethclient != NULL)
    {
        if (!update_del)
        {
            LOGT("probe_eth_clients: IPv6_Neighbors new/modify, hwaddr %s found, address %s",
                 rec->hwaddr,
                 rec->address);
            STRSCPY(ethclient->address, rec->address);
        }
        else
        {
            LOGT("probe_eth_clients: IPv6_Neighbors delete, hwaddr %s", rec->hwaddr);
            MEMZERO(ethclient->address);
        }
    }
}

static void callback_IPv6_Neighbors(
        ovsdb_update_monitor_t *mon,
        struct schema_IPv6_Neighbors *old_rec,
        struct schema_IPv6_Neighbors *rec,
        ovsdb_cache_row_t *row)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW || mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        update_ipv6_eth_client_address(rec, false);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        update_ipv6_eth_client_address(rec, true);
        return;
    }
    return;
}

static void callback_Bridge(
        ovsdb_update_monitor_t *mon,
        struct schema_Bridge *old_rec,
        struct schema_Bridge *rec,
        ovsdb_cache_row_t *row)
{
    return;
}

bool nm2_probe_eth_clients_init(void)
{
    OVSDB_TABLE_INIT(OVS_MAC_Learning, hwaddr);
    OVSDB_TABLE_INIT(IPv4_Neighbors, hwaddr);
    OVSDB_TABLE_INIT(IPv6_Neighbors, hwaddr);
    OVSDB_TABLE_INIT(Bridge, name);

    OVSDB_CACHE_MONITOR(OVS_MAC_Learning, false);
    OVSDB_CACHE_MONITOR(IPv4_Neighbors, false);
    OVSDB_CACHE_MONITOR(IPv6_Neighbors, false);
    OVSDB_CACHE_MONITOR(Bridge, false);

    ev_init(&g_probe_eth_clients_timer, probe_eth_clients_timer_fn);
    g_probe_eth_clients_timer.repeat = probe_period_get();
    ev_timer_again(EV_DEFAULT, &g_probe_eth_clients_timer);
    return true;
}
