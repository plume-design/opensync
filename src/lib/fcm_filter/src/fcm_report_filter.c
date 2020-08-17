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

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include "os.h"
#include "util.h"
#include "log.h"
#include "fcm_filter.h"
#include "network_metadata_report.h"
#include "fcm_report_filter.h"

static fcm_plugin_filter_t filtername;


/**
 * @brief initialization function filter name.
 *
 * @param valid pointer to net_md_stats_accumulator.
 * @return void.
 */
void fcm_filter_context_init(fcm_collect_plugin_t *collector)
{
    filtername.collect = collector->filters.collect;
    filtername.hist = collector->filters.hist;
    filtername.report = collector->filters.report;
}

/**
 * @brief print function net_md_stats_accumulator paramter.
 *
 * @param valid pointer to net_md_stats_accumulator.
 * @return void.
 */
static void print_md_acc_key(struct net_md_stats_accumulator *md_acc)
{

    LOGT("net_md_stats_accumulator=%p key=%p fkey=%p",
            md_acc, md_acc->key, md_acc->fkey);

    net_md_log_acc(md_acc);
}


/**
 * @brief call 7 tuple filter.
 *
 * receive net_md_stats_accumulator and fill the structure for filter.
 *
 * @param valid pointer to net_md_stats_accumulator.
 * @return true for include, false for exclude.
 */
static bool apply_filter(struct net_md_flow_key *key,
                         fcm_filter_stats_t *pkt,
                         struct flow_key *fkey,
                         char *filter_name)
{
    bool seven_tuple_action;
    fcm_filter_l2_info_t mac_filter;
    fcm_filter_l3_info_t filter;
    os_macaddr_t null_mac;
    os_macaddr_t *smac;
    os_macaddr_t *dmac;

    memset(&null_mac, 0, sizeof(null_mac));
    if (filter_name == NULL)
    {
        /* no filter name default included */
        return true;
    }

    smac = (key->smac != NULL ? key->smac : &null_mac);
    dmac = (key->dmac != NULL ? key->dmac : &null_mac);

    snprintf(mac_filter.src_mac, sizeof(mac_filter.src_mac),
             PRI_os_macaddr_lower_t,
             FMT_os_macaddr_pt(smac));
    snprintf(mac_filter.dst_mac, sizeof(mac_filter.dst_mac),
             PRI_os_macaddr_lower_t,
             FMT_os_macaddr_pt(dmac));

    mac_filter.vlan_id = key->vlan_id;
    mac_filter.eth_type = key->ethertype;

    if (inet_ntop(AF_INET, key->src_ip, filter.src_ip,
        INET6_ADDRSTRLEN) == NULL)
    {
        LOGE("%s: inet_ntop src_ip %s error: %s", __func__,
             key->src_ip, strerror(errno));
        return false;
    }
    if (inet_ntop(AF_INET, key->dst_ip, filter.dst_ip,
        INET6_ADDRSTRLEN) == NULL)
    {
        LOGE("%s: inet_ntop dst_ip %s error: %s", __func__,
             key->dst_ip, strerror(errno));
        return false;
    }

    filter.sport = ntohs(key->sport);
    filter.dport = ntohs(key->dport);
    filter.l4_proto = key->ipprotocol;

    /* key->ip_version No ip (0), ipv4 (4), ipv6 (6) */
    filter.ip_type = (key->ip_version <= 4)? AF_INET: AF_INET6;

    fcm_filter_7tuple_apply(filter_name,
                             &mac_filter,
                             &filter,
                             pkt,
                             fkey,
                             &seven_tuple_action);
    return seven_tuple_action;
}


/**
 * @brief call 7 tuple filter for collect filter.
 *
 * receive net_md_stats_accumulator and fill the structure for report filter.
 *
 * @param valid pointer to net_md_stats_accumulator.
 * @return true for include, false for exclude.
 */
static bool apply_collect_filter(struct net_md_flow_key *key)
{
    return apply_filter(key, NULL, NULL, filtername.collect);
}


/**
 * @brief call 7 tuple filter for report filter.
 *
 * receive net_md_stats_accumulator and fill the structure for report filter.
 *
 * @param valid pointer to net_md_stats_accumulator.
 * @return true for include, false for exclude.
 */
static bool apply_report_filter(struct net_md_stats_accumulator *md_acc)
{
    struct net_md_flow_key *key;
    fcm_filter_stats_t pkt;

    if (md_acc == NULL) return true;

    key = md_acc->key;
    if (key == NULL) return true;

    pkt.pkt_cnt = md_acc->report_counters.packets_count;
    pkt.bytes = md_acc->report_counters.bytes_count;

    return apply_filter(key, &pkt, md_acc->fkey, filtername.report);
}


/**
 * @brief callback function for network metadata.
 *
 * receive net_md_stats_accumulator and call the collector filter.
 *
 * @param pointer to the flow key.
 * @return true for include, false for exclude.
 */
bool fcm_collect_filter_nmd_callback(struct net_md_flow_key *key)
{
    if (key == NULL) return true;

    return apply_collect_filter(key);
}


/**
 * @brief callback function for network metadata.
 *
 * receive net_md_stats_accumulator and call report filter.
 *
 * @param valid pointer to net_md_stats_accumulator.
 * @return true for include, false for exclude.
 */
bool fcm_report_filter_nmd_callback(struct net_md_stats_accumulator *md_acc)
{
    if ((md_acc == NULL) || (md_acc->key == NULL))
    {
        return true;
    }
    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
    {
        print_md_acc_key(md_acc);
    }
    return apply_report_filter(md_acc);
}
