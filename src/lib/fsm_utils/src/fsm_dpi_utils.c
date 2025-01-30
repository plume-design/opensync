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

#include <string.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <errno.h>
#include "log.h"
#include "ds_tree.h"
#include "nf_utils.h"
#include "net_header_parse.h"
#include "network_metadata_report.h"
#include "fsm_dpi_utils.h"
#include "sockaddr_storage.h"
#include "osn_types.h"
#include "kconfig.h"

#define DEFAULT_ZONE (0)
#define FSM_DPI_ZONE (1)

#define NET_HDR_BUFF_SIZE 128 + (2 * INET6_ADDRSTRLEN + 128) + 256

static void copy_nf_ip_flow(
        nf_flow_t *flow,
        void *src_ip,
        void *dst_ip,
        uint16_t src_port,
        uint16_t dst_port,
        uint8_t proto,
        uint16_t family,
        uint16_t zone,
        int flow_marker
)
{
    if (family == AF_INET)
    {
        memcpy(&flow->addr.src_ip.ipv4.s_addr, src_ip, 4);
        memcpy(&flow->addr.dst_ip.ipv4.s_addr, dst_ip, 4);
    }
    else if (family == AF_INET6)
    {
        memcpy(flow->addr.src_ip.ipv6.s6_addr, src_ip, 16);
        memcpy(flow->addr.dst_ip.ipv6.s6_addr, dst_ip, 16);
    }
    flow->proto = proto;
    flow->family = family;
    flow->fields.port.src_port = src_port;
    flow->fields.port.dst_port = dst_port;
    flow->mark = flow_marker;
    flow->zone = zone;
}

static void copy_nf_icmp_flow(
        nf_flow_t *flow,
        void *src_ip,
        void *dst_ip,
        uint16_t id,
        uint8_t type,
        uint8_t code,
        uint16_t family,
        uint16_t zone,
        int flow_marker
)
{
    if (family == AF_INET)
    {
        memcpy(&flow->addr.src_ip.ipv4.s_addr, src_ip, 4);
        memcpy(&flow->addr.dst_ip.ipv4.s_addr, dst_ip, 4);
        flow->proto = 1;
    }
    else if (family == AF_INET6)
    {
        memcpy(flow->addr.src_ip.ipv6.s6_addr, src_ip, 16);
        memcpy(flow->addr.dst_ip.ipv6.s6_addr, dst_ip, 16);
        flow->proto = 58;
    }
    flow->family = family;
    flow->fields.icmp.id = id;
    flow->fields.icmp.type = type;
    flow->fields.icmp.code = code;
    flow->mark = flow_marker;
    flow->zone = zone;
}


// TODO ctx used to hold flow and its state when multiple plugins used
int fsm_set_ip_dpi_state(
        void *ctx,
        void *src_ip,
        void *dst_ip,
        uint16_t src_port,
        uint16_t dst_port,
        uint8_t proto,
        uint16_t family,
        enum fsm_dpi_state state,
        int flow_marker
)
{
    nf_flow_t flow;
    int ret;
    int mark = CT_MARK_ACCEPT;

    memset(&flow, 0, sizeof(flow));
    mark = fsm_dpi_get_mark(flow_marker, state);
    copy_nf_ip_flow(
            &flow,
            src_ip,
            dst_ip,
            src_port,
            dst_port,
            proto,
            family,
            DEFAULT_ZONE,
            mark);
    ret = nf_ct_set_mark(&flow);

    return ret;
}


int fsm_set_icmp_dpi_state(
        void *ctx,
        void *src_ip,
        void *dst_ip,
        uint16_t id,
        uint8_t type,
        uint8_t code,
        uint16_t family,
        enum fsm_dpi_state state,
        int flow_marker
)
{
    nf_flow_t flow;
    int ret;
    int mark = CT_MARK_ACCEPT;

    memset(&flow, 0, sizeof(flow));
    mark = fsm_dpi_get_mark(flow_marker, state);
    copy_nf_icmp_flow(
            &flow,
            src_ip,
            dst_ip,
            id,
            type,
            code,
            family,
            DEFAULT_ZONE,
            mark);
    ret = nf_ct_set_mark(&flow);
    return ret;
}

// APIs using net_header_parser
int fsm_set_dpi_mark(struct net_header_parser *net_hdr,
                     struct dpi_mark_policy *mark_policy)
{
    struct eth_header *eth_hdr;
    unsigned int type;
    int ret;

    if (mark_policy ==  NULL) return -1;
    if (mark_policy->mark_policy & PKT_VERDICT_ONLY) return 0;

    eth_hdr = &net_hdr->eth_header;
    type = eth_hdr->ethertype;

    if (type != ETH_P_IP && type != ETH_P_IPV6) return 0;

    ret = nf_ct_set_flow_mark(net_hdr, mark_policy->flow_mark, 0);

    return ret;
}

void fsm_dpi_set_plugin_decision(
        struct fsm_session *session,
        struct net_header_parser *net_parser,
        enum fsm_dpi_state state)
{
    struct net_md_stats_accumulator *acc;
    struct fsm_dpi_flow_info *info;

    if (session == NULL) return;

    acc = net_parser->acc;
    if (acc == NULL) return;

    if (acc->dpi_plugins == NULL) return;

    info = ds_tree_find(acc->dpi_plugins, session);
    if (info == NULL) return;

    info->decision = state;
}


/**
 * @brief determine the mark value to be used for contrack based on action.
 *
 * @param acc accumulator of the flow
 * @param action verdict received for this flow
 */
int
fsm_dpi_get_mark(int flow_marker, int action)
{
    int mark = CT_MARK_INSPECT;
    int mark_set;

    if (action == FSM_DPI_DROP) mark = CT_MARK_DROP;
    else if (action == FSM_DPI_PASSTHRU) mark = CT_MARK_ACCEPT;

    mark_set = (action == FSM_DPI_PASSTHRU);
    mark_set &= (flow_marker != 0);

    if (mark_set) mark = flow_marker;

    return mark;
}

/**
 * @brief get the user device's network id
 *
 * @param session the fsm session triggering a report
 * @param mac the uder device mac address
 */
char *
fsm_ops_get_network_id(struct fsm_session *session, os_macaddr_t *mac)
{
    char *network_id;

    if (session == NULL) return NULL;
    if (mac == NULL) return NULL;
    if (session->ops.get_network_id == NULL) return NULL;

    network_id = session->ops.get_network_id(session, mac);
    if (network_id == NULL) network_id = "unknown";

    return network_id;
}

bool
fsm_nfq_mac_same(os_macaddr_t *lkp_mac, struct nfq_pkt_info *pkt_info)
{
    bool rc = false;
    size_t i;

    if (!pkt_info->hw_addr || pkt_info->hw_addr_len != 6) return true;

    if (!memcmp(lkp_mac, pkt_info->hw_addr, sizeof(os_macaddr_t))) return true;

    if (!osn_mac_addr_cmp(pkt_info->hw_addr, &OSN_MAC_ADDR_INIT)) return true;

    LOGT("%s: pkt_mac",__func__);
    for (i = 0; i < pkt_info->hw_addr_len; i++)
    {
	    LOGT(":%02x", (unsigned char)pkt_info->hw_addr[i]);
	}

    LOGT("%s: lkp_mac: "PRI_os_macaddr_t , __func__, FMT_os_macaddr_pt(lkp_mac));

    return rc;

}


bool
fsm_update_neigh_cache(void *ipaddr, os_macaddr_t *mac, int domain, int source)
{
    struct sockaddr_storage ss_ipaddr;
    char buf[INET6_ADDRSTRLEN] = {0};
    struct neighbour_entry entry;
    time_t curr = time(NULL);
    bool rc = false;

    if (!mac || !ipaddr) return rc;

    MEMZERO(entry);
    MEMZERO(ss_ipaddr);

    LOGI("Adding neighbor entry for: %s.",inet_ntop(domain, ipaddr, buf, INET6_ADDRSTRLEN));
    entry.ipaddr = &ss_ipaddr;
    entry.mac = mac;
    entry.cache_valid_ts = curr;
    entry.source = source;
    entry.ifname = neigh_table_get_source(source);
    sockaddr_storage_populate(domain, ipaddr, entry.ipaddr);

    rc = neigh_table_add(&entry);
    if (rc == false && ((entry.flags & NEIGH_CACHED) == 0))
    {
        LOGE("%s: Failed to add neighbor entry for: %s", __func__, buf);
    }

    return rc;
}


/**
 * @brief Get the MQTT topic configured for reporting dpi
 * statistics, and the reporting interval.
 *
 * @param session the fsm session to probe
 * @return None
 */
void
fsm_set_dpi_health_stats_cfg(struct fsm_session *session)
{
    char *interval_str;
    long int interval;

    if (!session) return;

    session->dpi_stats_report_topic = session->ops.get_config(session, "dpi_health_stats_topic");

    /* read the interval time */
    interval_str = session->ops.get_config(session, "dpi_health_stats_interval_secs");
    if (interval_str != NULL)
    {
        errno = 0;
        interval = strtol(interval_str, 0, 10);
        if (errno == 0) session->dpi_stats_report_interval = (int)interval;
    }

    LOGI("%s: dpi health stats topic : %s, interval: %ld", __func__,
         session->dpi_stats_report_topic != NULL ? session->dpi_stats_report_topic : "not set",
         session->dpi_stats_report_interval);
}
