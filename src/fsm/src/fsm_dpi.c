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
#include <string.h>
#include <errno.h>
#include <endian.h>
#include <jansson.h>
#include <pcap.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <net/if.h>
#include <netinet/in.h>

#include "os.h"
#include "util.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "policy_tags.h"
#include "schema.h"
#include "log.h"
#include "ds.h"
#include "json_util.h"
#include "target.h"
#include "target_common.h"
#include "fsm.h"
#include "network_metadata_report.h"
#include "sockaddr_storage.h"
#include "fsm_dpi_utils.h"
#include "fsm_internal.h"
#include "fsm_packet_reinject_utils.h"
#include "qm_conn.h"
#include "nf_utils.h"
#include "kconfig.h"
#include "memutil.h"
#include "hw_acc.h"
#include "dpi_stats.h"
#include "dpi_intf.h"
#include "fsm_ipc.h"
#include "fsm_fn_trace.h"

#include "accel_evict_msg.h"
#include "nfe.h"


#define MAX_RESERVED_PORT_NUM 1023
#define NON_RESERVED_PORT_START_NUM MAX_RESERVED_PORT_NUM + 1
#define DHCP_SERVER_PORT_NUM 67
#define DHCP_CLIENT_PORT_NUM 68
#define MAX_CONNTRACK_ENTRIES 8192

extern int nfe_conntrack_tcp_timeout_est;
nfe_conntrack_t nfe_ct;
extern nfe_conn_t nfe_conn_lookup_by_tuple(nfe_conntrack_t conntrack,
                                           const struct nfe_tuple *tuple,
                                           uint64_t timestamp, int *dir);
bool
fsm_is_dns(struct net_header_parser *net_parser)
{
    struct udphdr *udph;

    if (net_parser == NULL) return false;
    if (net_parser->ip_protocol != IPPROTO_UDP) return false;

    udph = net_parser->ip_pld.udphdr;
    return ((ntohs(udph->source) == 53) || (ntohs(udph->dest) == 53));
}


#define FSM_TRACK_DNS(net_parser, plugin)                           \
    {                                                               \
        uint8_t qr;                                                 \
        bool is_dns = fsm_is_dns(net_parser);                       \
        if (is_dns)                                                 \
        {                                                           \
            qr = *(uint8_t *)(net_parser->data + 2);                \
            qr = (qr & (1 << 7)) >> 7;                              \
            LOGT("%s:%d: %s DNS %s id 0x%x", __func__, __LINE__,    \
                 plugin, (qr ? "reply" : "request"),                \
                 ntohs(*(uint16_t *)(net_parser->data)));           \
        }                                                           \
    }


static int
fsm_dpi_send_report(struct fsm_session *session)
{
    struct fsm_dpi_dispatcher *dispatch;
    union fsm_dpi_context *dpi_context;
    struct net_md_aggregator *aggr;
    struct packed_buffer *pb;
    struct fsm_mgr *mgr;
    char *mqtt_topic;
    int rc;

    dpi_context = session->dpi;
    if (dpi_context == NULL) return -1;

    dispatch = &dpi_context->dispatch;
    aggr = dispatch->aggr;

    /* Don't bother sending an empty report */
    if (aggr->active_accs == 0) return 0;

    /*
     * Reset the counter indicating the # of inactive flows with
     * a reference count. It will be updated while building the report.
     */
    aggr->held_flows = 0;

    pb = serialize_flow_report(aggr->report);
    if (pb == NULL) return -1;

    if (pb->buf == NULL) return 0; /* Nothing to send */

    /* If a topic is provided, also send the report to that topic */
    mqtt_topic = session->ops.get_config(session, "mqtt_v");
    session->ops.send_pb_report(session, mqtt_topic, pb->buf, pb->len);

    /* Check if the connection to FCM is established */
    mgr = fsm_get_mgr();
    if (!(mgr->osbus_flags & FSM_OSBUS_FCM))
    {
        FREE(pb->buf);
        FREE(pb);
        return 0;
    }

    /* Beware, sending the pb->buf will schedule its freeing */
    rc = fsm_ipc_client_send(pb->buf, pb->len);
    FREE(pb);

    return rc;
}


/**
 * @brief check if a fsm session is a dpi session
 *
 * @param session the session to check
 * @return true is the session is either a dpi dispatcher of a dpi plugin,
 *         false otherwise.
 */
bool
fsm_is_dpi(struct fsm_session *session)
{
    if (session->type == FSM_DPI_DISPATCH) return true;
    if (session->type == FSM_DPI_PLUGIN) return true;

    return false;
}


static int
fsm_get_max_conntrack_entries(struct fsm_session *session)
{
    char *max_entries;
    int max;

    max_entries = session->ops.get_config(session, "max_conntrack_entries");
    if (max_entries == NULL) return MAX_CONNTRACK_ENTRIES;

    max = strtoul(max_entries, NULL, 10);
    if (max <= 0) return MAX_CONNTRACK_ENTRIES;

    return max;
}


/**
 * @brief initiates a dpi plugin session
 *
 * @param session a dpi plugin session to initialize
 * @return true if the initialization succeeded, false otherwise
 */
bool
fsm_init_dpi_plugin(struct fsm_session *session)
{
    union fsm_dpi_context *dpi_context;
    struct fsm_dpi_plugin *dpi_plugin;
    struct fsm_dpi_plugin_ops *ops;
    bool ret;

    dpi_context = session->dpi;
    if (dpi_context == NULL) return false;

    dpi_plugin = &dpi_context->plugin;
    dpi_plugin->session = session;
    dpi_plugin->bound = false;
    dpi_plugin->clients_init = false;

    dpi_plugin->targets = fsm_get_other_config_val(session, "targeted_devices");
    dpi_plugin->excluded_targets = fsm_get_other_config_val(session,
                                                            "excluded_devices");

    ret = fsm_dpi_add_plugin_to_dispatcher(session);
    if (!ret) return ret;

    ops = &session->p_ops->dpi_plugin_ops;
    ops->notify_client = fsm_dpi_call_client;
    ops->register_clients = fsm_dpi_register_clients;
    ops->unregister_clients = fsm_dpi_unregister_clients;
    ops->mark_flow = fsm_dpi_mark_for_report;

    return true;
}


/**
 * @brief Add a dpi plugin to a dispatcher session
 *
 * @param session a dpi plugin session to add to its dispatcher
 * @return true if the addition succeeded, false otherwise
 */
bool
fsm_dpi_add_plugin_to_dispatcher(struct fsm_session *session)
{
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *dpi_plugin;
    struct fsm_session *dispatcher;
    ds_tree_t *dpi_sessions;
    char *dispatcher_name;
    ds_tree_t *sessions;

    /* Basic checks on the dpi plugin session */
    if (session->type != FSM_DPI_PLUGIN) return false;
    if (session->dpi == NULL) return false;

    /* Get the dpi plugin context */
    dpi_plugin = &session->dpi->plugin;

    /* Check if the dpi plugin is already bound to a dispatcher */
    if (dpi_plugin->bound) return false;

    /* Retrieve the dispatcher session */
    dispatcher_name = session->ops.get_config(session, "dpi_dispatcher");
    if (dispatcher_name == NULL) return false;

    sessions = fsm_get_sessions();
    if (sessions == NULL) return false;

    dispatcher = ds_tree_find(sessions, dispatcher_name);

    /* The dispatcher might not yet have been registered. Allow it */
    if (dispatcher == NULL) return true;

    if (dispatcher->type != FSM_DPI_DISPATCH) return false;
    if (dispatcher->dpi == NULL) return false;

    LOGI("%s: adding dpi plugin %s to %s",
         __func__, session->name, dispatcher->name);

    /* Add the dpi plugin to the dispatcher */
    dpi_dispatcher = &dispatcher->dpi->dispatch;
    dpi_sessions = &dpi_dispatcher->plugin_sessions;
    ds_tree_insert(dpi_sessions, dpi_plugin, session->name);
    dpi_plugin->bound = true;

    return true;
}


/**
 * @brief retrieve the fsm dispatcher session for the given dpi plugin
 *
 * Looks up the dispatcher session as named in the other_config table.
 * @param session the dpi plugin session
 * @return the dispatcher session
 */
struct fsm_session *
fsm_dpi_find_dispatcher(struct fsm_session *session)
{
    struct fsm_session *dispatcher;
    char *dispatcher_name;
    ds_tree_t *sessions;

    /* Retrieve the dispatcher session */
    dispatcher_name = session->ops.get_config(session, "dpi_dispatcher");
    if (dispatcher_name == NULL) return NULL;

    sessions = fsm_get_sessions();
    if (sessions == NULL) return NULL;

    dispatcher = ds_tree_find(sessions, dispatcher_name);

    return dispatcher;
}


/**
 * @brief add a plugin's pointer to nodes of a tree
 *
 * @param session the session to add
 * @param the tree of nodes to be updated
 */
void
fsm_dpi_add_plugin_to_tree(struct fsm_session *session,
                           ds_tree_t *tree)
{
    struct fsm_dpi_flow_info *dpi_flow_info;
    struct net_md_stats_accumulator *acc;
    struct net_md_flow *flow;

    if (tree == NULL) return;

    flow = ds_tree_head(tree);
    while (flow != NULL)
    {
        acc = flow->tuple_stats;
        dpi_flow_info = ds_tree_find(acc->dpi_plugins, session);
        if (dpi_flow_info != NULL)
        {
            flow = ds_tree_next(tree, flow);
            continue;
        }

        dpi_flow_info = CALLOC(1, sizeof(*dpi_flow_info));
        if (dpi_flow_info == NULL)
        {
            flow = ds_tree_next(tree, flow);
            continue;
        }
        dpi_flow_info->session = session;

        ds_tree_insert(acc->dpi_plugins, dpi_flow_info, session);
        flow = ds_tree_next(tree, flow);
    }
}


/**
 * @brief add a plugin's pointer to current flows
 *
 * @param session the session to add
 * @param aggr the flow aggregator
 */
void
fsm_dpi_add_plugin_to_flows(struct fsm_session *session,
                            struct net_md_aggregator *aggr)
{
    struct net_md_eth_pair *pair;

    pair = ds_tree_head(aggr->eth_pairs);
    while (pair != NULL)
    {
        fsm_dpi_add_plugin_to_tree(session, &pair->five_tuple_flows);
        pair = ds_tree_next(aggr->eth_pairs, pair);
    }
    fsm_dpi_add_plugin_to_tree(session, aggr->five_tuple_flows);
}



static void
fsm_dpi_del_plugin_from_acc(struct fsm_session *session,
                            struct net_md_stats_accumulator *acc)
{
    struct fsm_dpi_flow_info *dpi_flow_info;

    if (acc == NULL)
    {
        return;
    }

    if (acc->dpi_plugins == NULL)
    {
        return;
    }

    dpi_flow_info = ds_tree_find(acc->dpi_plugins, session);
    if (dpi_flow_info == NULL)
    {
        return;
    }

    ds_tree_remove(acc->dpi_plugins, dpi_flow_info);
    FREE(dpi_flow_info);
}


/**
 * @brief delete a plugin's pointer from nodes of a tree
 *
 * @param session the session to delete
 * @param the tree of nodes to be updated
 */
void
fsm_dpi_del_plugin_from_tree(struct fsm_session *session,
                             ds_tree_t *tree)
{
    struct net_md_stats_accumulator *acc;
    struct net_md_flow *flow;

    if (tree == NULL) return;

    flow = ds_tree_head(tree);
    while (flow != NULL)
    {
        acc = flow->tuple_stats;
        fsm_dpi_del_plugin_from_acc(session, acc);
        flow = ds_tree_next(tree, flow);
    }
}


void fsm_dpi_cleanup_acc(nfe_conn_t conn, void *data)
{
    struct net_md_stats_accumulator *acc;
    struct fsm_session *session;

    session = (struct fsm_session *)data;
    acc = container_of(conn, struct net_md_stats_accumulator, priv);
    if (acc == NULL) return;

    fsm_dpi_del_plugin_from_acc(session, acc);
    return;
}
/**
 * @brief delete a plugin's pointer from current flows
 *
 * @param session the session to delete
 * @param aggr the flow aggregator
 */
void
fsm_dpi_del_plugin_from_flows(struct fsm_session *session,
                              struct net_md_aggregator *aggr)
{
    struct net_md_stats_accumulator *acc;
    struct net_md_eth_pair *pair;

    if (aggr->nfe_ct)
    {
        nfe_conntrack_dump(aggr->nfe_ct, fsm_dpi_cleanup_acc, session);
        return;
    }

    pair = ds_tree_head(aggr->eth_pairs);
    while (pair != NULL)
    {
        fsm_dpi_del_plugin_from_tree(session, &pair->ethertype_flows);
        fsm_dpi_del_plugin_from_tree(session, &pair->five_tuple_flows);
        acc = pair->mac_stats;
        fsm_dpi_del_plugin_from_acc(session, acc);

        pair = ds_tree_next(aggr->eth_pairs, pair);
    }
    fsm_dpi_del_plugin_from_tree(session, aggr->five_tuple_flows);
}


/**
 * @brief free the dpi resources of a dpi plugin
 *
 * @param session the session to free
 */
void
fsm_free_dpi_plugin(struct fsm_session *session)
{
    struct fsm_dpi_dispatcher *dispatch;
    union fsm_dpi_context *dpi_context;
    struct fsm_dpi_plugin *dpi_plugin;
    struct net_md_aggregator *aggr;
    struct fsm_session *dispatcher;

    /* Retrieve the dispatcher */
    dispatcher = fsm_dpi_find_dispatcher(session);
    if (dispatcher == NULL) return;

    dpi_context = dispatcher->dpi;
    if (dpi_context == NULL) return;

    dispatch = &dpi_context->dispatch;
    dpi_plugin = ds_tree_find(&dispatch->plugin_sessions, session->name);
    if (dpi_plugin != NULL)
    {
        ds_tree_remove(&dispatch->plugin_sessions, dpi_plugin);
    }

    LOGT("%s: removed dpi plugin %s from %s",
         __func__, session->name, dispatcher->name);

    aggr = dispatch->aggr;
    if (aggr == NULL) return;

    fsm_dpi_del_plugin_from_flows(session, aggr);

    fsm_dpi_unregister_clients(session);

    return;
}


/**
 * @brief check if a mac address belongs to a given tag or matches a value
 *
 * @param the mac address to check
 * @param val an opensync tag name or the string representation of a mac address
 * @return true if the mac matches the value, false otherwise
 */
static bool
fsm_dpi_find_mac_in_val(os_macaddr_t *mac, char *val)
{
    char mac_s[32] = { 0 };
    bool rc;
    int ret;

    if (val == NULL) return false;

    /* In case of NFQUEUE mac address may be null, hence the condition */
    if (mac == NULL) return false;
    snprintf(mac_s, sizeof(mac_s), PRI_os_macaddr_lower_t,
             FMT_os_macaddr_pt(mac));

    rc = om_tag_in(mac_s, val);
    if (rc) return true;

    ret = strncmp(mac_s, val, strlen(mac_s));
    return (ret == 0);
}


/**
 * @brief check if mac matches a given tag
 *
 * @param the mac address to check
 * @param dispatch the core dispatcher to check in included & exclude devices
 * @return true if the mac matches the value, false otherwise
 */
static bool
fsm_dpi_find_mac(os_macaddr_t *mac, struct fsm_dpi_dispatcher *dispatch)
{
    bool excluded_devices;
    bool included_devices;
    bool rc;

    if (mac == NULL) return false;
    if (dispatch == NULL) return false;

    if (dispatch->excluded_devices == NULL) excluded_devices = false;
    else excluded_devices = true;
    if (dispatch->included_devices == NULL) included_devices = false;
    else included_devices = true;

    if (!excluded_devices && !included_devices) return false;
    if (!excluded_devices && included_devices)
    {
        rc = fsm_dpi_find_mac_in_val(mac, dispatch->included_devices);
        return rc;
    }
    if (excluded_devices && !included_devices)
    {
        rc = fsm_dpi_find_mac_in_val(mac, dispatch->excluded_devices);
        return (!rc);
    }
    if (excluded_devices && included_devices)
    {
        rc = fsm_dpi_find_mac_in_val(mac, dispatch->excluded_devices);
        return (!rc);
    }

    return true;
}


/**
 * @brief check if the multicast bit is set in a mac address
 *        (bit 0 of byte 0 is set to 1)
 *
 * @param mac a pointer to the MAC address to check
 * @return true if we have a broadcast/multicast MAC address
 */
static bool
fsm_dpi_is_mac_bcast(os_macaddr_t *mac)
{
    bool rc;

    if (mac == NULL) return false;

    rc = (mac->addr[0] & 0x1) == 0x1;
    return rc;
}

/**
 * @brief compare 2 dpi sessions names
 *
 * @param a the first session's name
 * @param a the second session's name
 * @return 0 if names match
 */
static int
fsm_dpi_sessions_cmp(const void *a, const void *b)
{
    return strcmp(a, b);
}

/**
 * @brief report filter routine
 *
 * @param acc the accumulator checked to be reported
 * @return true if the flow should be reported, false otherwise
 */
static bool
fsm_dpi_report_filter(struct net_md_stats_accumulator *acc)
{
    if (acc == NULL) return false;

    if (!acc->report) return false;

    /* Make sure to report the flow only once */
    acc->report = false;

    return true;
}


static void
fsm_dpi_update_acc_key(struct net_md_stats_accumulator *acc)
{
    struct net_md_flow_key *key;

    if (acc == NULL) return;

    key = acc->key;
    if (key == NULL) return;

    key->direction = acc->direction;
    key->originator = acc->originator;
}

void
fsm_dpi_set_lan2lan_originator(struct net_md_stats_accumulator *acc)
{
    struct net_md_flow_key *key;
    bool smac_bcast;
    bool dmac_bcast;

    if (acc == NULL) return;

    key = acc->key;
    if (key == NULL) return;

    smac_bcast = fsm_dpi_is_mac_bcast(key->smac);
    dmac_bcast = fsm_dpi_is_mac_bcast(key->dmac);

    if (acc->direction == NET_MD_ACC_LAN2LAN_DIR)
    {
        if (smac_bcast && !dmac_bcast)
            acc->originator = NET_MD_ACC_ORIGINATOR_DST;
        else if (!smac_bcast && dmac_bcast)
            acc->originator = NET_MD_ACC_ORIGINATOR_SRC;
        else
            acc->originator = NET_MD_ACC_ORIGINATOR_SRC;
    }
}

/**
 * @brief set accumulator flow direction based ports
 *
 * Sets the accumulator direction (outbound, inbound, undetermined)
 * @param dispatch the core dispatcher
 * @param the accumulator to be tagged with a direction
 */
void
fsm_dpi_set_acc_direction_on_port(struct fsm_dpi_dispatcher *dispatch,
                                  struct net_md_stats_accumulator *acc)
{
    struct net_md_flow_key *key;
    bool smac_found;
    bool smac_bcast;
    bool dmac_found;
    bool dmac_bcast;
    uint16_t sport;
    uint16_t dport;

    if (dispatch == NULL) return;
    if (acc == NULL) return;

    key = acc->key;
    if (key == NULL) return;

    smac_bcast = fsm_dpi_is_mac_bcast(key->smac);
    dmac_bcast = fsm_dpi_is_mac_bcast(key->dmac);
    smac_found = fsm_dpi_find_mac(key->smac, dispatch) || smac_bcast;
    dmac_found = fsm_dpi_find_mac(key->dmac, dispatch) || dmac_bcast;
    sport = htons(key->sport);
    dport = htons(key->dport);

    if (!(smac_found || dmac_found))
    {
        LOGD("%s: no MAC found", __func__);
        return;
    }

    if (smac_found && dmac_found)
    {
        acc->direction = NET_MD_ACC_LAN2LAN_DIR;
        fsm_dpi_set_lan2lan_originator(acc);
    }
    else if ((sport > MAX_RESERVED_PORT_NUM) &&
             (dport < NON_RESERVED_PORT_START_NUM))
    {
        acc->direction = (smac_found ? NET_MD_ACC_OUTBOUND_DIR :
                          NET_MD_ACC_INBOUND_DIR);
    }
    else if ((sport < NON_RESERVED_PORT_START_NUM) &&
             (dport > MAX_RESERVED_PORT_NUM))
    {
        acc->direction = (smac_found ? NET_MD_ACC_INBOUND_DIR :
                          NET_MD_ACC_OUTBOUND_DIR);
    }
    else if ((sport < NON_RESERVED_PORT_START_NUM) &&
             (dport < NON_RESERVED_PORT_START_NUM))
    {
        /* DHCP flow direction */
        if ((sport == DHCP_SERVER_PORT_NUM && dport == DHCP_CLIENT_PORT_NUM) ||
            (sport == DHCP_CLIENT_PORT_NUM && dport == DHCP_SERVER_PORT_NUM))
        {
            acc->direction = NET_MD_ACC_OUTBOUND_DIR;
        }
        else
        {
            /* Ports are reserved, set direction based on smac */
            acc->direction = (smac_found ? NET_MD_ACC_OUTBOUND_DIR :
                              NET_MD_ACC_INBOUND_DIR);
        }
    }
    else
    {
        /* Ports are non reserved, set direction based on smac */
        acc->direction = (smac_found ? NET_MD_ACC_OUTBOUND_DIR :
                          NET_MD_ACC_INBOUND_DIR);
    }

    if (acc->direction == NET_MD_ACC_INBOUND_DIR)
    {
        acc->originator = (smac_found ? NET_MD_ACC_ORIGINATOR_DST :
                           NET_MD_ACC_ORIGINATOR_SRC);
    }
    else if (acc->direction == NET_MD_ACC_OUTBOUND_DIR)
    {
        acc->originator = (smac_found ? NET_MD_ACC_ORIGINATOR_SRC :
                           NET_MD_ACC_ORIGINATOR_DST);
    }
}


/**
 * @brief set TCP accumulator flow direction
 *
 * Sets the accumulator direction (outbound, inbound, undetermined)
 * @param dispatch the core dispatcher
 * @param the accumulator to be tagged with a direction
 */
bool
fsm_dpi_set_tcp_acc_direction(struct fsm_dpi_dispatcher *dispatch,
                              struct net_md_stats_accumulator *acc)
{
    struct net_md_flow_key *key;
    bool smac_found;
    bool dmac_found;

    if (dispatch == NULL) return false;
    if (acc == NULL) return false;

    key = acc->key;
    if (key == NULL) return false;


    smac_found = fsm_dpi_find_mac(key->smac, dispatch);
    dmac_found = fsm_dpi_find_mac(key->dmac, dispatch);

    if (key->tcp_flags == FSM_TCP_SYN)
    {
        acc->originator = NET_MD_ACC_ORIGINATOR_SRC;
        if (smac_found && dmac_found)
        {
            acc->direction = NET_MD_ACC_LAN2LAN_DIR;
        }
        else if (smac_found && !dmac_found)
        {
            acc->direction = NET_MD_ACC_OUTBOUND_DIR;
        }
        else if (!smac_found && dmac_found)
        {
            acc->direction = NET_MD_ACC_INBOUND_DIR;
        }
        else
        {
            acc->direction = NET_MD_ACC_UNSET_DIR;
        }
    }
    else if (key->tcp_flags == (FSM_TCP_SYN | FSM_TCP_ACK))
    {
        acc->originator = NET_MD_ACC_ORIGINATOR_DST;
        if (smac_found && dmac_found)
        {
            acc->direction = NET_MD_ACC_LAN2LAN_DIR;
        }
        else if (smac_found && !dmac_found)
        {
            acc->direction = NET_MD_ACC_INBOUND_DIR;
        }
        else if (!smac_found && dmac_found)
        {
            acc->direction = NET_MD_ACC_OUTBOUND_DIR;
        }
        else
        {
            acc->direction = NET_MD_ACC_UNSET_DIR;
        }
    }
    else
    {
        fsm_dpi_set_acc_direction_on_port(dispatch, acc);
    }

    fsm_dpi_update_acc_key(acc);

    return (acc->direction != NET_MD_ACC_UNSET_DIR);
}


/**
 * @brief set UDP accumulator flow direction
 *
 * Sets the accumulator direction (outbound, inbound, undetermined)
 * @param aggr the aggregator the accumulator belongs
 * @param the accumulator to be tagged with a direction
 */
bool
fsm_dpi_set_udp_acc_direction(struct fsm_dpi_dispatcher *dispatch,
                              struct net_md_stats_accumulator *acc)
{
    struct net_md_flow_key *key;

    if (dispatch == NULL) return false;
    if (acc == NULL) return false;

    key = acc->key;
    if (key == NULL) return false;


    fsm_dpi_set_acc_direction_on_port(dispatch, acc);
    fsm_dpi_update_acc_key(acc);

    return (acc->direction != NET_MD_ACC_UNSET_DIR);
}

bool
fsm_dpi_is_multicast_ip(struct net_md_flow_key *key)
{
    struct sockaddr_storage ip;
    int family;

    if (key->ip_version == 0) return false;

    family = ((key->ip_version == 4) ? AF_INET : AF_INET6);
    sockaddr_storage_populate(family, key->dst_ip, &ip);

    if (key->ip_version == 4)
    {
        struct sockaddr_in   *in4;
        in4 = (struct sockaddr_in *)&ip;

        if (((in4->sin_addr.s_addr & htonl(0xE0000000)) == htonl(0xE0000000) ||
            (in4->sin_addr.s_addr & htonl(0xF0000000)) == htonl(0xF0000000)))
        {
            /* broadcast or multicast */
            return true;
        }
    }
    else if (key->ip_version == 6)
    {
        struct sockaddr_in6 *in6;

        in6 = (struct sockaddr_in6 *)&ip;
        return IN6_IS_ADDR_MULTICAST(&in6->sin6_addr);
    }

    return false;
}
/**
 * @brief set ICMP accumulator flow direction
 *
 * Sets the accumulator direction (outbound, inbound, undetermined)
 * @param aggr the aggregator the accumulator belongs
 * @param the accumulator to be tagged with a direction
 */
bool
fsm_dpi_set_icmp_acc_direction(struct fsm_dpi_dispatcher *dispatch,
                               struct net_md_stats_accumulator *acc)
{
    struct net_md_flow_key *key;
    bool smac_found;
    bool dmac_found;
    uint8_t ipproto;
    bool mcast_ip = false;

    if (dispatch == NULL) return false;
    if (acc == NULL) return false;

    key = acc->key;
    if (key == NULL) return false;

    ipproto = key->ipprotocol;

    smac_found = fsm_dpi_find_mac(key->smac, dispatch);
    dmac_found = fsm_dpi_find_mac(key->dmac, dispatch);

    /* if dmac is multicast or broadcast, then direction will be LAN2LAN */
    dmac_found |= fsm_dpi_is_mac_bcast(key->dmac);
    if (!(smac_found || dmac_found)) return false;

    /* for some multicast pkts, dst mac is not populated,
     * check if the pkt is multicast by checking its dest IP */
    if (!dmac_found) mcast_ip = fsm_dpi_is_multicast_ip(key);

    if ((smac_found && dmac_found) || mcast_ip)
    {
        acc->direction = NET_MD_ACC_LAN2LAN_DIR;
        fsm_dpi_set_lan2lan_originator(acc);
    }
    else if (smac_found)
    {
        acc->direction = NET_MD_ACC_OUTBOUND_DIR;
        acc->originator = NET_MD_ACC_ORIGINATOR_SRC;
        if (ipproto == IPPROTO_ICMP && key->icmp_type == ICMP_ECHOREPLY)
        {
            acc->direction = NET_MD_ACC_INBOUND_DIR;
            acc->originator = NET_MD_ACC_ORIGINATOR_DST;
        }

        if (ipproto == IPPROTO_ICMPV6 && key->icmp_type == ICMP6_ECHO_REPLY)
        {
            acc->direction = NET_MD_ACC_INBOUND_DIR;
            acc->originator = NET_MD_ACC_ORIGINATOR_DST;
        }
    }
    else if (dmac_found)
    {
        acc->direction = NET_MD_ACC_OUTBOUND_DIR;
        acc->originator = NET_MD_ACC_ORIGINATOR_DST;
        if (ipproto == IPPROTO_ICMP && key->icmp_type == ICMP_ECHO)
        {
            acc->direction = NET_MD_ACC_INBOUND_DIR;
            acc->originator = NET_MD_ACC_ORIGINATOR_SRC;
        }

        if (ipproto == IPPROTO_ICMPV6 && key->icmp_type == ICMP6_ECHO_REQUEST)
        {
            acc->direction = NET_MD_ACC_INBOUND_DIR;
            acc->originator = NET_MD_ACC_ORIGINATOR_SRC;
        }
    }

    fsm_dpi_update_acc_key(acc);

    return (acc->direction != NET_MD_ACC_UNSET_DIR);
}


/**
 * @brief set ARP accumulator flow direction
 *
 * Sets the accumulator direction (outbound, inbound, undetermined)
 * @param aggr the aggregator the accumulator belongs
 * @param the accumulator to be tagged with a direction
 */
bool
fsm_dpi_set_arp_acc_direction(struct fsm_dpi_dispatcher *dispatch,
                               struct net_md_stats_accumulator *acc)
{
    if (dispatch == NULL) return false;
    if (acc == NULL) return false;

    acc->direction = NET_MD_ACC_LAN2LAN_DIR;
    fsm_dpi_set_lan2lan_originator(acc);
    return (acc->direction != NET_MD_ACC_UNSET_DIR);
}



/**
 * @brief set the accumulator flow direction
 *
 * Sets the accumulator direction (outbound, inbound, undetermined)
 * @param dispatch the core dispatcher
 * @param the accumulator to be tagged with a direction
 * @return true if the direction was detected, false otherwise
 */
bool
fsm_dpi_set_acc_direction(struct fsm_dpi_dispatcher *dispatch,
                          struct net_md_stats_accumulator *acc)
{
    struct net_md_flow_key *key;

    if (acc == NULL) return false;

    key = acc->key;
    if (key == NULL) return false;


    if (key->ipprotocol == IPPROTO_TCP) return fsm_dpi_set_tcp_acc_direction(dispatch, acc);

    if (key->ipprotocol == IPPROTO_UDP) return fsm_dpi_set_udp_acc_direction(dispatch, acc);

    if (key->ipprotocol == IPPROTO_ICMP ||
        key->ipprotocol == IPPROTO_ICMPV6)
    {
        return fsm_dpi_set_icmp_acc_direction(dispatch, acc);
    }

    if (key->ethertype == ETH_P_ARP)
    {
        return fsm_dpi_set_arp_acc_direction(dispatch, acc);
    }

    return false;
}


/**
 * @brief mark an accumulator for report
 *
 * @param acc the accumulator to mark for report
 */
static void
fsm_dpi_mark_acc_for_report(struct net_md_aggregator *aggr,
                            struct net_md_stats_accumulator *acc)
{
    if (acc->report) return;

    /* Mark the accumulator for report */
    acc->report = true;

    /* Provision space for reporting */
    if (acc->state != ACC_STATE_WINDOW_ACTIVE) aggr->active_accs++;

    return;
}


void
fsm_dpi_set_network_id(struct net_md_stats_accumulator *acc)
{
    struct net_md_flow_key *key;
    struct flow_key *fkey;

    os_macaddr_t *dev_mac;
    char *netid;

    if (acc == NULL) return;

    key = acc->key;
    if (key == NULL) return;

    fkey = acc->fkey;
    if (fkey == NULL) return;

    /* get the device mac */
    if ((acc->originator == NET_MD_ACC_ORIGINATOR_SRC &&
         acc->direction == NET_MD_ACC_OUTBOUND_DIR) ||
        (acc->originator == NET_MD_ACC_ORIGINATOR_DST &&
         acc->direction == NET_MD_ACC_INBOUND_DIR))
    {
        dev_mac = key->smac;
    }
    else if ((acc->originator == NET_MD_ACC_ORIGINATOR_SRC &&
              acc->direction == NET_MD_ACC_INBOUND_DIR) ||
             (acc->originator == NET_MD_ACC_ORIGINATOR_DST &&
              acc->direction == NET_MD_ACC_OUTBOUND_DIR))
    {
        dev_mac = key->dmac;
    }
    else
    {
        LOGD("%s: Unknown direction packets, cannot determine network id", __func__);
        return;
    }

    netid = fsm_get_network_id(dev_mac);
    fkey->networkid = (netid) ? STRDUP(netid) : STRDUP("unknown");
}

void
fsm_dpi_set_flow_marker(struct net_md_stats_accumulator *acc)
{
    struct flow_key *fkey;
    if (acc == NULL) return;

    fkey = acc->fkey;
    fkey->flowmarker = acc->flow_marker;
}

/**
 * @brief set uplinkname for the acc.
 *
 * Sets the uplink name for the flow (eth0, eth1, tunnel)
 * @param the accumulator to be tagged with an uplinkname.
 */
void
fsm_dpi_set_uplink_name(struct net_md_stats_accumulator *acc)
{
    struct net_md_flow_key *key;
    struct flow_key *fkey;
    char ifname[IFNAMSIZ] = {0};

    if (acc == NULL) return;

    key = acc->key;
    fkey = acc->fkey;

    if ((acc->originator == NET_MD_ACC_ORIGINATOR_SRC &&
         acc->direction == NET_MD_ACC_OUTBOUND_DIR) ||
        (acc->originator == NET_MD_ACC_ORIGINATOR_DST &&
         acc->direction == NET_MD_ACC_INBOUND_DIR))
    {
        if (key->tx_idx) if_indextoname(key->tx_idx, ifname);
    }
    else if ((acc->originator == NET_MD_ACC_ORIGINATOR_SRC &&
              acc->direction == NET_MD_ACC_INBOUND_DIR) ||
             (acc->originator == NET_MD_ACC_ORIGINATOR_DST &&
              acc->direction == NET_MD_ACC_OUTBOUND_DIR))
    {
        if (key->rx_idx) if_indextoname(key->rx_idx, ifname);
    }
    else
    {
        LOGD("%s: Unknown direction packets, cannot determine network uplinkname", __func__);
        return;
    }

    if (key->tx_idx || key->rx_idx) fkey->uplinkname = STRDUP(ifname);
    return;
}


/**
 * @brief callback from the accumulator creation
 *
 * Called on the creation of an accumulator
 * @param aggr the core dpi aggregator
 * @param the accumulator being created
 */
void
fsm_dpi_on_acc_creation(struct net_md_aggregator *aggr,
                        struct net_md_stats_accumulator *acc)
{
    struct fsm_dpi_dispatcher *dispatch;

    bool rc;

    if (aggr == NULL) return;

    dispatch = aggr->context;
    if (acc->direction != NET_MD_ACC_UNSET_DIR) return;

    acc->flags |= NET_MD_ACC_FIRST_LEG;
    acc->dpi_always = false;

    rc = fsm_dpi_set_acc_direction(dispatch, acc);
    if (!rc) return;

    fsm_dpi_set_network_id(acc);

    fsm_dpi_set_uplink_name(acc);

    fsm_dpi_mark_acc_for_report(aggr, acc);
}


/**
 * @brief callback to the accumulaor destruction
 *
 * Called on the destruction of an accumulator
 * @param aggr the core dpi aggregator
 * @param the accumulator being destroyed
 */
void
fsm_dpi_on_acc_destruction(struct net_md_aggregator *aggr,
                           struct net_md_stats_accumulator *acc)
{
    struct net_md_stats_accumulator *rev_acc;
    int mark;

    mark = fsm_dpi_get_mark(acc->flow_marker, acc->dpi_done);
    if ((mark > 2) && (acc->mark_done != mark))
    {
        flush_accel_flows(acc);
    }

    rev_acc = acc->rev_acc;

    /* Clean up rev_acc's dereference */
    if (rev_acc != NULL)
    {
        rev_acc->rev_acc = NULL;
    }
    acc->rev_acc = NULL;
}


static void
fsm_dispatcher_init_tap_intfs(struct fsm_session *dispatcher)
{
    struct dpi_intf_registration registrar;
    struct fsm_mgr *mgr;

    mgr = fsm_get_mgr();

    dpi_intf_init();

    MEMZERO(registrar);
    registrar.loop = mgr->loop;
    registrar.context = dispatcher;
    registrar.id = dispatcher->name;
    registrar.handler = fsm_pcap_dispatcher_handler;

    dpi_intf_register_context(&registrar);
}


/**
 * @brief initializes the dpi resources of a dispatcher session
 *
 * @param session the dispatcher session
 * @return true if the initialization succeeded, false otherwise
 */
bool
fsm_init_dpi_dispatcher(struct fsm_session *session)
{
    struct net_md_aggregator_set aggr_set;
    struct fsm_dpi_dispatcher *dispatch;
    union fsm_dpi_context *dpi_context;
    struct net_md_aggregator *aggr;
    struct node_info node_info;
    ds_tree_t *dpi_sessions;
    char *recv_str;
    struct fsm_mgr *mgr;
    bool ret;
    int rc;
    int res;
    int max_conntrack_entries = 0;

    dpi_context = session->dpi;
    if (dpi_context == NULL) return false;

    dispatch = &dpi_context->dispatch;
    dispatch->periodic_report_ts = time(NULL);
    dispatch->periodic_backoff_ts = time(NULL);

    dispatch->included_devices = fsm_get_other_config_val(session,
                                                          "included_devices");
    dispatch->excluded_devices = fsm_get_other_config_val(session,
                                                          "excluded_devices");

    dispatch->listening_ip = fsm_get_other_config_val(session,
                                                      "listening_ip");
    dispatch->listening_port = fsm_get_other_config_val(session,
                                                      "listening_port");
    recv_str = fsm_get_other_config_val(session, "recv_method");

    dispatch->recv_method = VECTOR_IO;
    if (recv_str && strcmp(recv_str, "buffer") == 0) dispatch->recv_method = BUFFER;

    memset(&aggr_set, 0, sizeof(aggr_set));
    mgr = fsm_get_mgr();
    node_info.location_id = mgr->location_id;
    node_info.node_id = mgr->node_id;
    aggr_set.info = &node_info;
    aggr_set.num_windows = 1;
    aggr_set.acc_ttl = 120;
    aggr_set.report_type = NET_MD_REPORT_ABSOLUTE;
    aggr_set.report_filter = fsm_dpi_report_filter;
    aggr_set.send_report = net_md_send_report;
    aggr_set.on_acc_create = fsm_dpi_on_acc_creation;
    aggr_set.on_acc_destroy = fsm_dpi_on_acc_destruction;
    aggr = net_md_allocate_aggregator(&aggr_set);
    if (aggr == NULL) return false;

    dispatch->aggr = aggr;
    aggr->context = dispatch;

    dispatch->session = session;
    dpi_sessions = &dispatch->plugin_sessions;
    ds_tree_init(dpi_sessions, fsm_dpi_sessions_cmp,
                 struct fsm_dpi_plugin, dpi_node);

    fsm_dispatch_set_ops(session);
    fsm_dpi_bind_plugins(session);

    fsm_dispatcher_init_tap_intfs(session);

    ret = net_md_activate_window(dispatch->aggr);
    if (!ret)
    {
        LOGE("%s: failed to activate aggregator", __func__);
        return false;
    }

    rc = accel_evict_msg_socket_init(CONFIG_TARGET_LAN_BRIDGE_NAME);
    if (rc < 0)
    {
        LOGE("%s: failed to initialize flow cache context", __func__);
        return false;
    }

    fsm_set_dpi_health_stats_cfg(session);

    /* NFE Initialization */
    nfe_conntrack_tcp_timeout_est = 300;

    max_conntrack_entries = fsm_get_max_conntrack_entries(session);

    if ((res = nfe_conntrack_create(&nfe_ct, max_conntrack_entries)) != 0)
    {
        LOGE("%s: failed to allocate conntrack: %d\n", __func__, res);
        return false;
    }
    aggr->nfe_ct = nfe_ct;
    return true;
}


/**
 * @brief free the dpi resources of every plugin
 *
 * @param session the session to free
 */
void
fsm_free_dpi_plugins_resources(struct fsm_session *session)
{
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    struct fsm_dpi_dispatcher *dispatch;
    union fsm_dpi_context *dpi_context;
    struct fsm_dpi_plugin *dpi_plugin;
    struct fsm_dpi_plugin *next;
    ds_tree_t *dpi_sessions;

    dpi_context = session->dpi;
    if (dpi_context == NULL) return;

    dispatch = &dpi_context->dispatch;
    dpi_sessions = &dispatch->plugin_sessions;
    dpi_plugin = ds_tree_head(dpi_sessions);
    while (dpi_plugin != NULL)
    {
        dpi_plugin_ops = &dpi_plugin->session->p_ops->dpi_plugin_ops;
        if (dpi_plugin_ops->dpi_free_resources != NULL)
        {
            dpi_plugin_ops->dpi_free_resources(dpi_plugin->session);
        }
        next = ds_tree_next(dpi_sessions, dpi_plugin);
        dpi_plugin = next;
    }
}


/**
 * @brief free the dpi resources of a dispatcher plugin
 *
 * @param session the session to free
 */
void
fsm_free_dpi_dispatcher(struct fsm_session *session)
{
    struct net_md_aggregator *aggr;
    struct fsm_dpi_dispatcher *dispatch;
    union fsm_dpi_context *dpi_context;
    struct dpi_node *dpi_plugin;
    struct dpi_node *remove;
    ds_tree_t *dpi_sessions;
    struct dpi_node *next;

    dpi_context = session->dpi;
    if (dpi_context == NULL) return;

    dispatch = &dpi_context->dispatch;
    dpi_sessions = &dispatch->plugin_sessions;
    dpi_plugin = ds_tree_head(dpi_sessions);
    while (dpi_plugin != NULL)
    {
        next = ds_tree_next(dpi_sessions, dpi_plugin);
        remove = dpi_plugin;
        ds_tree_remove(dpi_sessions, remove);
        dpi_plugin = next;
    }

    aggr = dispatch->aggr;
    nfe_conntrack_destroy(aggr->nfe_ct);
    net_md_free_aggregator(aggr);
    FREE(aggr);

    accel_evict_msg_socket_exit();

    fsm_ipc_terminate_client();
}


/**
 * @brief binds existing dpi plugins to to a dispatcher session
 *
 * Upon the creation of a dpi dispatcher plugin, walk through
 * the existing plugins and bind the relevant dpi plugins
 * @param session the dispatcher plugin session
 */
void
fsm_dpi_bind_plugins(struct fsm_session *session)
{
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dispatch;
    struct fsm_dpi_plugin *dpi_plugin;
    struct fsm_session *plugin;
    ds_tree_t *dpi_sessions;
    char *dispatcher_name;
    ds_tree_t *sessions;
    int cmp;

    if (session->type != FSM_DPI_DISPATCH) return;

    dispatcher_dpi_context = session->dpi;
    if (dispatcher_dpi_context == NULL) return;

    dispatch = &dispatcher_dpi_context->dispatch;
    dpi_sessions = &dispatch->plugin_sessions;

    sessions = fsm_get_sessions();
    if (sessions == NULL) return;

    plugin = ds_tree_head(sessions);
    while (plugin != NULL)
    {
        if (plugin->type != FSM_DPI_PLUGIN)
        {
            plugin = ds_tree_next(sessions, plugin);
            continue;
        }

        plugin_dpi_context = plugin->dpi;
        if (plugin_dpi_context == NULL)
        {
            plugin = ds_tree_next(sessions, plugin);
            continue;
        }

        dpi_plugin = &plugin_dpi_context->plugin;
        if (dpi_plugin->bound)
        {
            plugin = ds_tree_next(sessions, plugin);
            continue;
        }

        dispatcher_name = plugin->ops.get_config(plugin, "dpi_dispatcher");
        if (dispatcher_name == NULL)
        {
            plugin = ds_tree_next(sessions, plugin);
            continue;
        }

        cmp = strcmp(dispatcher_name, session->name);
        if (cmp)
        {
            plugin = ds_tree_next(sessions, plugin);
            continue;
        }
        ds_tree_insert(dpi_sessions, dpi_plugin, plugin->name);
        dpi_plugin->bound = true;

        plugin = ds_tree_next(sessions, plugin);
    }
}


static void
fsm_update_dpi_plugin(struct fsm_session *session)
{
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_plugin *plugin;

    plugin_dpi_context = session->dpi;
    plugin = &plugin_dpi_context->plugin;

    plugin->targets = fsm_get_other_config_val(session,
                                               "targeted_devices");
    plugin->excluded_targets = fsm_get_other_config_val(session,
                                                        "excluded_devices");
    LOGD("%s: %s: targeted_devices: %s", __func__, session->name,
         plugin->targets ? plugin->targets : "None");
    LOGD("%s: %s: excluded_devices: %s", __func__, session->name,
         plugin->excluded_targets ? plugin->excluded_targets : "None") ;
}


/**
 * @brief update the configuration of dpi dispatcher
 *
 * Updates other_config info to dpi dispatcher
 * @param session updated session
 */
static void
fsm_update_dpi_dispatcher(struct fsm_session *session)
{
    struct fsm_dpi_dispatcher *dispatch;
    union fsm_dpi_context *dpi_context;
    char *mem_monitor_restart;
    char *recv_str;

    dpi_context = session->dpi;
    if (dpi_context == NULL) return;

    dispatch = &dpi_context->dispatch;
    dispatch->included_devices = fsm_get_other_config_val(session,
                                                          "included_devices");
    dispatch->excluded_devices = fsm_get_other_config_val(session,
                                                          "excluded_devices");
    dispatch->listening_ip = fsm_get_other_config_val(session,
                                                      "listening_ip");
    dispatch->listening_port = fsm_get_other_config_val(session, "listening_port");

    dispatch->recv_method = VECTOR_IO;
    recv_str = fsm_get_other_config_val(session, "recv_method");
    if (recv_str && strcmp(recv_str, "buffer") == 0) dispatch->recv_method = BUFFER;

    mem_monitor_restart = fsm_get_other_config_val(session, "mem_monitor_restart");
    if (mem_monitor_restart && strcmp(mem_monitor_restart, "true") == 0)
    {
        fsm_reinit_mem_monitor();
    }
}


/**
 * @brief initializes the dpi reources of a dpi session
 *
 * Calls either the dispatcher or the dpi init plugin routine for the session
 * based on its type
 * @param session the session to initialize
 * @return true if the initialization succeeded, false otherwise
 */
bool
fsm_update_dpi_context(struct fsm_session *session)
{
    union fsm_dpi_context *dpi_context;
    bool ret;

    if (!fsm_is_dpi(session)) return true;

    /* Update if already initialized */
    if (session->dpi != NULL)
    {
        if (session->type == FSM_DPI_PLUGIN)
        {
            fsm_update_dpi_plugin(session);
        }
        else if (session->type == FSM_DPI_DISPATCH)
        {
            fsm_update_dpi_dispatcher(session);
        }
        return true;
    }

    /* Allocate a new context */
    dpi_context = CALLOC(1, sizeof(*dpi_context));
    if (dpi_context == NULL) return false;

    session->dpi = dpi_context;

    ret = false;
    if (session->type == FSM_DPI_DISPATCH)
    {
        ret = fsm_init_dpi_dispatcher(session);
    }
    else if (session->type == FSM_DPI_PLUGIN)
    {
        ret = fsm_init_dpi_plugin(session);
    }

    if (!ret) goto err_free_dpi_context;

    return true;

err_free_dpi_context:
    fsm_free_dpi_context(session);

    return false;
}



/**
 * @brief released the dpi reources of a dpi session
 *
 * Calls either the dispatcher or the dpi plugin release routine for the session
 * based on its type
 * @param session the session to release
 */
void
fsm_free_dpi_context(struct fsm_session *session)
{
    if (!fsm_is_dpi(session)) return;

    if (session->type == FSM_DPI_DISPATCH)
    {
        fsm_free_dpi_plugins_resources(session);
        fsm_free_dpi_dispatcher(session);
    }
    else if (session->type == FSM_DPI_PLUGIN)
    {
        fsm_free_dpi_plugin(session);
    }

    FREE(session->dpi);
    session->dpi = NULL;
}


/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int
fsm_dpi_session_cmp(const void *a, const void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a ==  p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}


/**
 * @brief check if the current parsed packet is an ip fragment
 *
 * @param net_parser the parsing info
 * @return true if the packet is an ip fragment, false otherwise
 */
bool
fsm_dpi_is_ip_fragment(struct net_header_parser *net_parser)
{
    struct iphdr * iphdr;
    uint16_t offset;

    if (net_parser->ip_version != 4) return false;

    iphdr = net_header_get_ipv4_hdr(net_parser);
    offset = ntohs(iphdr->frag_off);
    if (offset & 0x2000) return true;
    if (offset & 0x1fff) return true;

    return false;
}


static bool
fsm_set_ip_tuple_key(struct net_header_parser *net_parser,
                     struct net_md_flow_key *key)
{
    bool is_fragment;

    key->ip_version = net_parser->ip_version;
    if (net_parser->ip_version == 4)
    {
        struct iphdr * iphdr;

        iphdr = net_header_get_ipv4_hdr(net_parser);
        key->src_ip = (uint8_t *)(&iphdr->saddr);
        key->dst_ip = (uint8_t *)(&iphdr->daddr);

        is_fragment = fsm_dpi_is_ip_fragment(net_parser);
        if (is_fragment) return false;
    }
    else if (net_parser->ip_version == 6)
    {
        struct ip6_hdr *ip6hdr;

        ip6hdr = net_header_get_ipv6_hdr(net_parser);
        key->src_ip = (uint8_t *)(&ip6hdr->ip6_src.s6_addr);
        key->dst_ip = (uint8_t *)(&ip6hdr->ip6_dst.s6_addr);
    }

    key->ipprotocol = net_parser->ip_protocol;
    if (key->ipprotocol == IPPROTO_UDP)
    {
        struct udphdr *udphdr;

        udphdr = net_parser->ip_pld.udphdr;
        key->sport = udphdr->source;
        key->dport = udphdr->dest;
    }
    else if (key->ipprotocol == IPPROTO_TCP)
    {
        struct tcphdr *tcphdr;

        tcphdr = net_parser->ip_pld.tcphdr;
        key->sport = tcphdr->source;
        key->dport = tcphdr->dest;
        key->tcp_flags |= (tcphdr->syn ? FSM_TCP_SYN : 0);
        key->tcp_flags |= (tcphdr->ack ? FSM_TCP_ACK : 0);
    }
    else if (key->ipprotocol == IPPROTO_ICMP)
    {
        struct icmphdr *icmphdr;

        icmphdr = net_parser->ip_pld.icmphdr;
        key->icmp_type = icmphdr->type;
        key->icmp_idt = icmphdr->un.echo.id;
    }
    else if (key->ipprotocol == IPPROTO_ICMPV6)
    {
        struct icmp6_hdr *icmp6hdr;

        icmp6hdr = net_parser->ip_pld.icmp6hdr;
        key->icmp_type = icmp6hdr->icmp6_type;
        key->icmp_idt = icmp6hdr->icmp6_dataun.icmp6_un_data16[0];
    }

    key->rx_idx = net_parser->rx_vidx;
    key->tx_idx = net_parser->tx_vidx;
    key->flags |= NET_MD_ACC_FIVE_TUPLE;

    return true;
}


/**
 * @brief retrieves the nfe conn from the packet
 *
 * @param net_parser the parsing info
 * @param aggr the dispatcher's aggregator
 * @return the flow accumulator
 */
nfe_conn_t
fsm_net_parser_to_conn(struct net_header_parser *net_parser)
{

    struct net_md_stats_accumulator *acc;
    struct eth_header *eth_hdr;
    uint16_t ethertype;


    nfe_conn_t conn;
    struct nfe_packet packet;
    struct timespec now;
    uint64_t timestamp;
    int res;

    ethertype = 0;

    eth_hdr = net_header_get_eth(net_parser);

    if (net_parser->source == PKT_SOURCE_SOCKET)
    {
        ethertype = eth_hdr->ethertype;
    }


    clock_gettime(CLOCK_MONOTONIC, &now);
    timestamp = ((uint64_t)now.tv_sec * 1000) + (now.tv_nsec / 1000000);

    res = nfe_packet_hash(&packet, ethertype, net_parser->start,
                          net_parser->caplen, timestamp);
    if (res)
    {
        LOGE("%s: failed to hash packet\n", __func__);
        return NULL;
    }

    conn = nfe_conn_lookup(nfe_ct, &packet);
    if (!conn) return NULL;
    acc = container_of(conn, struct net_md_stats_accumulator, priv);
    acc->packet = CALLOC(1, sizeof(struct nfe_packet));
    if (!acc->packet) return NULL;
    memcpy(acc->packet, &packet, sizeof(struct nfe_packet));
    return conn;
}


/**
 * @brief mark the flow for report
 *
 * @param session the dpi plugin session marking the flow
 * @param acc the accumulator to mark for report
 */
void
fsm_dpi_mark_for_report(struct fsm_session *session,
                        struct net_md_stats_accumulator *acc)
{
    fsm_dpi_mark_acc_for_report(acc->aggr, acc);
}


/**
 * @brief check if a mac should be processed
 *
 * @param the mac to check
 * @param included_targets tag representing the included targets
 * @param excluded_targets tag representing the excluded targets
 *
 * check if a packet should be processed based on its the source and destination
 */
bool
fsm_dpi_should_process_mac(os_macaddr_t *mac,
                           char *included_targets,
                           char *excluded_targets)
{
    bool is_unicast;
    bool excluded;
    bool included;

    /* Sanity check */
    if (mac == NULL) return false;

    /* Do not process broadcast/multicast addresses */
    is_unicast = ((mac->addr[0] & 0x1) == 0x0);
    if (!is_unicast) return false;

    /* Check if the mac is explicitly included */
    if (included_targets != NULL)
    {
        included = fsm_dpi_find_mac_in_val(mac, included_targets);
        return included;
    }

    /* The mac is not explicitly included, check if it is explicitly excluded */
    if (excluded_targets != NULL)
    {
        excluded = !fsm_dpi_find_mac_in_val(mac, excluded_targets);
        return excluded;
    }

    /* The mac is not explicitly included nor explicitly excluded, process it */
    return true;
}


/**
 * @brief check if a packet should be processed
 *
 * @param the parsed info for the current packet
 * @param included_targets tag representing the included targets
 * @param excluded_targets tag representing the excluded targets
 *
 * check if a packet should be procesed based on its the source and destination
 * Both source and destination macs are checked for processing.
 * If one of the mac is to be processed, the packet is processed regardless of
 * the processing status of the second mac.
 * For example, a DNS reply may come from a source to be excluded, to a destination
 * to be included. Such a packet needs to be processed.
 */
bool
fsm_dpi_should_process(struct net_header_parser *net_parser,
                       char *included_targets,
                       char *excluded_targets)
{
    struct eth_header *eth_hdr;
    bool process;

    /* safety check */
    if (net_parser == NULL) return false;

    /* Access the ethernet header */
    eth_hdr = &net_parser->eth_header;

    /* Check the source mac */
    process = fsm_dpi_should_process_mac(eth_hdr->srcmac,
                                         included_targets, excluded_targets);

    /* Check the destination mac */
    process |= fsm_dpi_should_process_mac(eth_hdr->dstmac,
                                          included_targets, excluded_targets);

    return process;
}


#define FLUSH_COOKIE 0xDA2C5588
int
flush_accel_flows(struct net_md_stats_accumulator *acc)
{
#if defined(CONFIG_ACCEL_FLOW_EVICT_MESSAGE)
    int32_t data_i32, ct_mark, ip_len, index, err;
    uint8_t fush_msg[48];
    struct net_md_flow_info info;
    os_macaddr_t client;
    bool rc;

    MEMZERO(info);
    MEMZERO(fush_msg);
    MEMZERO(client);

    if (acc == NULL)
    {
        LOGD("%s: no stats_accumulator data", __func__);
        return -1;
    }

    rc = net_md_get_flow_info(acc, &info);
    if ((rc == false) || (info.local_mac == NULL))
    {
        LOGD("%s: no flow_info data, rc: %d or missing srcmac: %p", __func__, rc, info.local_mac);
        return -1;
    }

    if (!info.local_ip || !info.remote_ip)
    {
        LOGD("%s: src or dst ip NULL %p/%p", __func__, info.local_ip, info.remote_ip);
        return -1;
    }

    ct_mark = fsm_dpi_get_mark(acc->flow_marker, acc->dpi_done);

    index = 0;
    ip_len = (info.ip_version == 4) ? 4 : 16;

    //cookie: ensure `value'
    data_i32 = htole32(FLUSH_COOKIE);
    memcpy(&fush_msg[index], &data_i32, 4);           index +=4;
    memcpy(&fush_msg[index], &info.ip_version, 1);    index +=1;
    memcpy(&fush_msg[index], &info.ipprotocol, 1);    index +=1;
    memcpy(&fush_msg[index], info.local_ip, ip_len);  index +=ip_len;
    memcpy(&fush_msg[index], info.remote_ip, ip_len); index +=ip_len;

    //ports: ensure endian (ports are in `be` format)
    memcpy(&fush_msg[index], &info.local_port, 2);    index +=2;
    memcpy(&fush_msg[index], &info.remote_port, 2);   index +=2;

    //ct_mark: ensure `value'
    data_i32 = htole32(ct_mark);
    memcpy(&fush_msg[index], &data_i32, 4);           index +=4;

    /* get the device mac */
    if (memcmp(&client, info.local_mac, sizeof(os_macaddr_t)) == 0)
    {
        LOGW("%s: unknown client (no mac)? Broadcasting the flush message to all nodes", __func__);
        memset(&client, 0xff, sizeof(os_macaddr_t));
    }
    else
    {
        memcpy(&client, info.local_mac, sizeof(os_macaddr_t));
    }

    err = accel_evict_msg_socket_send(&client, fush_msg, index);
    if (err != 0)
    {
        LOGW("%s: evict message transmit failed: %d", __func__, err);
        return -1;
    }

    LOGD("%s: done setting new mark to CT: %d", __func__, ct_mark);
#endif /* CONFIG_ACCEL_FLOW_EVICT_MESSAGE */
    return 0;
}


/**
 * @brief dispatches a received packet to the dpi plugin handlers
 *
 * Check the overall state of the flow:
 * - if marked as passthrough or blocker, do not dispatch the packet
 * - otherwise, dispatch the packet to plugins willing to inspect it.
 * @param net_parser the parsed info for the current packet
 */
static void
fsm_dispatch_pkt(struct fsm_session *session,
                 struct net_header_parser *net_parser)
{
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    struct net_md_stats_accumulator *acc;
    struct dpi_mark_policy mark_policy;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dpi_plugin;
    struct fsm_dpi_plugin *plugin;
    int state = FSM_DPI_CLEAR;
    ds_tree_t *tree;
    bool process;
    bool drop;
    bool pass;
    int mark;
    int err;

    acc = net_parser->acc;
    if (acc == NULL) return;

    if (acc->dpi_done != 0)
    {
        bool ct_mark_flow;

        ct_mark_flow = true;

        memset(&mark_policy, 0, sizeof(mark_policy));
        FSM_TRACK_DNS(net_parser, session->name);
        mark = fsm_dpi_get_mark(net_parser->acc->flow_marker, acc->dpi_done);

        /*
         * Ideally, we would like to set the ct_mark (and flush accelerated data-path)
         * only once per flow/ct_mark change.
         * In testing we noticed that sometimes ct_mark won't get set on 1st try
         * (API only reports if command was sent to ct) successfully, not if ct_mark was actually set).
         * So until we figure out why and fix that, we still need to push the ct_mark multiple times.
         *
         * It is important we fix this eventually. This commented code is left as a reminder of that.
         */
         // ct_mark_flow = (mark_policy.flow_mark != mark);

        mark_policy.flow_mark = mark;
        if (!ct_mark_flow)
        {
            mark_policy.mark_policy = PKT_VERDICT_ONLY;
        }

        err = session->set_dpi_mark(net_parser, &mark_policy);
        if (err != 0)
        {
            LOGD("%s: Setting ct_mark failed (1)", __func__);
            return;
        }

        return;
    }

    tree = acc->dpi_plugins;
    if (tree == NULL) return;

    info = ds_tree_head(tree);
    if (info == NULL) return;

    drop = false;
    pass = true;

    while (info != NULL && !drop)
    {
        dpi_plugin = info->session;
        plugin_dpi_context = dpi_plugin->dpi;
        plugin = &plugin_dpi_context->plugin;

        process = fsm_dpi_should_process(net_parser,
                                         plugin->targets,
                                         plugin->excluded_targets);
        if (!process)
        {
            FSM_TRACK_DNS(net_parser, dpi_plugin->name);

            info = ds_tree_next(tree, info);
            continue;
        }

        if (info->decision == FSM_DPI_CLEAR)
        {
            info->decision = FSM_DPI_INSPECT;
        }

        if (info->decision == FSM_DPI_INSPECT || acc->dpi_always)
        {
            if (dpi_plugin->p_ops == NULL)
            {
                info = ds_tree_next(tree, info);
                continue;
            }

            dpi_plugin_ops = &dpi_plugin->p_ops->dpi_plugin_ops;
            if (dpi_plugin_ops->handler == NULL)
            {
                info = ds_tree_next(tree, info);
                continue;
            }
            FSM_TRACK_DNS(net_parser, dpi_plugin->name);

            fsm_fn_trace(dpi_plugin_ops->handler, FSM_FN_ENTER);
            dpi_plugin_ops->handler(dpi_plugin, net_parser);
            fsm_fn_trace(dpi_plugin_ops->handler, FSM_FN_EXIT);
        }

        drop = (info->decision == FSM_DPI_DROP);
        pass &= (info->decision == FSM_DPI_PASSTHRU);

        info = ds_tree_next(tree, info);
    }

    if (net_parser->payload_updated)
    {
        FSM_TRACK_DNS(net_parser, session->name);
        fsm_forward_pkt(session, net_parser);
    }

    if (drop) state = FSM_DPI_DROP;
    if (pass) state = FSM_DPI_PASSTHRU;

    acc->dpi_done = state;

    if (acc->dpi_always) acc->dpi_done = FSM_DPI_CLEAR;

    if (pass || drop)
    {
        mark = fsm_dpi_get_mark(net_parser->acc->flow_marker, acc->dpi_done);

        /* Set the flow_marker to be used for FCM */
        acc->flow_marker = mark;
        fsm_dpi_set_flow_marker(acc);
        memset(&mark_policy, 0, sizeof(mark_policy));
        mark_policy.flow_mark = mark;
        err = session->set_dpi_mark(net_parser, &mark_policy);
        if (err != 0)
        {
            LOGD("%s: Setting ct_mark failed (2)", __func__);
        }
        else
        {
            if ((mark > 2) && (acc->mark_done != mark))
            {
                flush_accel_flows(acc);
                acc->mark_done = mark;
            }
        }
    }
    else
    {
        acc->mark_done = FSM_DPI_INSPECT;
    }
}


/**
 * @brief filter packets not worth presenting to the dpi plugins
 *
 * @param net_parser the parsed packet
 * @return true is to be presented, false otherwise
 */
static bool
fsm_dpi_filter_packet(struct net_header_parser *net_parser)
{
    bool ret = true;

    /* filter out UDP packets with no data */
    if (net_parser->ip_protocol == IPPROTO_UDP)
    {
        ret &= (net_parser->packet_len != net_parser->parsed);
    }

    return ret;
}


void
fsm_net_parser_to_key(struct net_header_parser *net_parser, struct net_md_flow_key *key) {

    struct eth_header *eth_hdr;
    uint16_t ethertype;
    bool is_ip;
    bool ret;

    eth_hdr = &net_parser->eth_header;
    memset(key, 0, sizeof(struct net_md_flow_key));
    key->smac = eth_hdr->srcmac;
    key->dmac = eth_hdr->dstmac;
    key->vlan_id = eth_hdr->vlan_id;
    ethertype = eth_hdr->ethertype;
    key->ethertype = ethertype;
    is_ip = ((ethertype == ETH_P_IP) || (ethertype == ETH_P_IPV6));
    if (!is_ip)
    {
        key->flags |= NET_MD_ACC_ETH;
    }
    else
    {
        ret = fsm_set_ip_tuple_key(net_parser, key);
        if (!ret) return;
    }
    return;
}

/**
 * @brief the dispatcher plugin's packet handler
 *
 * Retrieves the flow accumulator.
 * If the flow is new, bind it to the dpi plugins
 * Dispatch the packet to the dpi plugins
 * @param session the dispatcher session
 * @param net_parser the parsed packet
 */
static void
fsm_dpi_handler(struct fsm_session *session,
                struct net_header_parser *net_parser)
{
    struct net_md_stats_accumulator *acc;
    struct fsm_dpi_dispatcher *dispatch;
    union fsm_dpi_context *dpi_context;
    struct flow_counters counters;
    struct net_md_flow_key key;
    nfe_conn_t conn;
    size_t payload_len;
    bool process;

    dpi_context = session->dpi;
    if (dpi_context == NULL) return;

    if (kconfig_enabled(CONFIG_FSM_MAP_LEGACY_PLUGINS))
    {
        struct fsm_parser_ops *parser_ops;
        struct fsm_session *mapped;

        mapped = fsm_map_plugin_find_session(net_parser);
        if (mapped == NULL) return;

        parser_ops = &mapped->p_ops->parser_ops;
        parser_ops->handler(mapped, net_parser);

        return;
    }

    dispatch = &dpi_context->dispatch;
    process = fsm_dpi_should_process(net_parser,
                                     dispatch->included_devices,
                                     dispatch->excluded_devices);
    FSM_TRACK_DNS(net_parser, session->name);
    if (!process)
    {
        LOGT("%s: not processing the following flow: ", __func__);
        net_header_logt(net_parser);

        return;
    }

    conn = fsm_net_parser_to_conn(net_parser);
    if (conn == NULL) return;

    acc = container_of(conn, struct net_md_stats_accumulator, priv);
    if (acc == NULL) return;

    if (!acc->initialized)
    {
        fsm_net_parser_to_key(net_parser, &key);
        net_md_populate_acc(dispatch->aggr, &key, acc);
        fsm_dpi_alloc_flow_context(session, acc);
        acc->initialized = true;
        acc->aggr = dispatch->aggr;
        dispatch->aggr->nfe_ct = nfe_ct;
    }

    counters.packets_count = acc->counters.packets_count + 1;
    counters.bytes_count = acc->counters.bytes_count + net_parser->packet_len;
    payload_len = net_parser->packet_len - net_parser->parsed;
    counters.payload_bytes_count = acc->counters.payload_bytes_count + payload_len;
    net_md_set_counters(dispatch->aggr, acc, &counters);

    net_parser->acc = acc;

    process = fsm_dpi_filter_packet(net_parser);
    if (!process) return;

    net_header_logt(net_parser);
    fsm_dispatch_pkt(session, net_parser);
    FREE(acc->packet);
}

/**
 * @brief releases the dpi context of a flow accumulator
 *
 * The flow accumulator retains info about each dpi plugin.
 * Release these dpi resources.
 * @param acc the flow accumulator
 */
static void
fsm_dpi_free_flow_context(struct net_md_stats_accumulator *acc)
{
    struct fsm_dpi_flow_info *dpi_flow_info;
    struct fsm_dpi_flow_info *remove;
    struct fsm_session *session;
    ds_tree_t *dpi_sessions;

    dpi_sessions = acc->dpi_plugins;
    if (dpi_sessions == NULL) return;

    dpi_flow_info = ds_tree_head(dpi_sessions);
    while (dpi_flow_info != NULL)
    {
        remove = dpi_flow_info;
        session = remove->session;
        if (session->ops.dpi_free_conn_ctxt != NULL)
        {
            session->ops.dpi_free_conn_ctxt(acc);
        }
        dpi_flow_info = ds_tree_next(dpi_sessions, dpi_flow_info);
        ds_tree_remove(dpi_sessions, remove);
        FREE(remove);
    }
    FREE(dpi_sessions);
}


/**
 * @brief allocates the dpi context of a flow accumulator
 *
 * The flow accumulator retains info about each dpi plugin.
 * Allocates these dpi resources.
 * @param session the dpi dispatcher's session
 * @param acc the flow accumulator
 */
void
fsm_dpi_alloc_flow_context(struct fsm_session *session,
                           struct net_md_stats_accumulator *acc)
{
    struct fsm_dpi_flow_info *dpi_flow_info;
    struct fsm_dpi_dispatcher *dispatch;
    union fsm_dpi_context *dpi_context;
    struct fsm_dpi_plugin *dpi_plugin;
    ds_tree_t *dpi_sessions;

    if (acc->dpi_plugins != NULL) return;

    acc->free_plugins = fsm_dpi_free_flow_context;
    acc->dpi_plugins = CALLOC(1, sizeof(ds_tree_t));
    if (acc->dpi_plugins == NULL) return;

    ds_tree_init(acc->dpi_plugins, fsm_dpi_session_cmp,
                 struct fsm_dpi_flow_info, dpi_node);

    dpi_context = session->dpi;
    dispatch = &dpi_context->dispatch;
    dpi_sessions = &dispatch->plugin_sessions;
    dpi_plugin = ds_tree_head(dpi_sessions);
    while (dpi_plugin != NULL)
    {
        dpi_flow_info = CALLOC(1, sizeof(*dpi_flow_info));
        if (dpi_flow_info == NULL)
        {
            dpi_plugin = ds_tree_next(dpi_sessions, dpi_plugin);
            continue;
        }
        dpi_flow_info->session = dpi_plugin->session;
        ds_tree_insert(acc->dpi_plugins, dpi_flow_info,
                       dpi_flow_info->session);

        dpi_plugin = ds_tree_next(dpi_sessions, dpi_plugin);
    }
}

static void
fsm_pcap_stats(struct fsm_session *session)
{
    struct fsm_pcaps *pcaps;
    struct pcap_stat stats;
    pcap_t *pcap;
    int rc;

    if (!fsm_plugin_has_intf(session)) return;
    if (session->conf == NULL) return;

    pcaps = session->pcaps;
    if (pcaps == NULL) return;

    pcap = pcaps->pcap;
    memset(&stats, 0, sizeof(stats));

    rc = pcap_stats(pcap, &stats);
    if (rc < 0)
    {
        LOGT("%s: pcap_stats failed: %s",
             __func__, pcap_geterr(pcap));
        return;
    }

    dpi_stats_store_pcap_stats(&stats, session->conf->if_name);
    LOGI("%s: %s: packets received: %u, dropped: %u",
         __func__, session->conf->if_name, stats.ps_recv, stats.ps_drop);
}

static void
fsm_get_session_intf_pcap_stats(void)
{
    ds_tree_t *sessions = fsm_get_sessions();
    struct fsm_session *session;

    session = ds_tree_head(sessions);
    while (session != NULL)
    {
        fsm_pcap_stats(session);
        session = ds_tree_next(sessions, session);
    }
}

/**
 * @brief collect pcap stats from the interfaces configured in core_dpi_dispatch
 *  and also in Dpi_Interface_Map
 */
static void
fsm_get_intf_pcap_stats(void)
{
    /* get pcap stats from interface configured in core_dpi_dispatch */
    fsm_get_session_intf_pcap_stats();
    /* get pcap stats from interface configured in Dpi_Interface_Map */
    dpi_intf_get_pcap_stats();
}


static void nfe_log_acc_cb(nfe_conn_t conn, void *data)
{
    struct net_md_stats_accumulator *acc;

    if (!conn) return;

    acc = container_of(conn, struct net_md_stats_accumulator, priv);
    if (!acc) return;
 
    net_md_log_acc(acc, __func__);
}

void
fsm_dpi_recycle_nfe_conns(void)
{
    struct nfe_tuple tuple;
    struct timespec now;
    nfe_conn_t conn;
    int dir = 0;

    uint64_t ts;

    /* Expire idle nfe connections */

    LOGT("%s: Expiring idle nfe connections", __func__);
    clock_gettime(CLOCK_MONOTONIC, &now);
    ts = ((uint64_t)now.tv_sec * 1000) + (now.tv_nsec / 1000000);
    MEMZERO(tuple);
    tuple.proto = IPPROTO_ICMP;
    conn = nfe_conn_lookup_by_tuple(nfe_ct, &tuple, ts, &dir);
    nfe_conn_release(conn);

    tuple.proto = IPPROTO_TCP;
    conn = nfe_conn_lookup_by_tuple(nfe_ct, &tuple, ts, &dir);
    nfe_conn_release(conn);

    tuple.proto = IPPROTO_UDP;
    conn = nfe_conn_lookup_by_tuple(nfe_ct, &tuple, ts, &dir);
    nfe_conn_release(conn);

    tuple.proto = 0;
    conn = nfe_conn_lookup_by_tuple(nfe_ct, &tuple, ts, &dir);
    nfe_conn_release(conn);

    LOGT("%s: Number of active flows: %d",__func__, nfe_conntrack_dump(nfe_ct, nfe_log_acc_cb, NULL));
}


#define FSM_DPI_STATS_REPORT_INTERVAL 120
#define FSM_DPI_BACKOFF_INTERVAL 30
/**
 * @brief routine periodically called
 *
 * Periodically walks the aggregator and removes the outdated flows
 * @param session the dpi dispatcher session
 */
void
fsm_dpi_periodic(struct fsm_session *session)
{
    struct dpi_stats_packed_buffer *pb;
    struct dpi_stats_report dpi_report;
    struct fsm_dpi_dispatcher *dispatch;
    union fsm_dpi_context *dpi_context;
    struct net_md_aggregator *aggr;
    struct flow_window **windows;
    struct flow_window *window;
    struct flow_report *report;
    int long dpi_report_conf_intvl = 0;
    int long dpi_backoff_conf_intvl = 0;
    time_t now;
    int rc;
    dpi_context = session->dpi;
    if (dpi_context == NULL) return;

    dispatch = &dpi_context->dispatch;
    aggr = dispatch->aggr;
    if (aggr == NULL) return;

    MEMZERO(dpi_report);
    dpi_report.location_id = session->location_id;
    dpi_report.node_id = session->node_id;

    dpi_report_conf_intvl = session->dpi_stats_report_interval;
    if (!dpi_report_conf_intvl) dpi_report_conf_intvl = FSM_DPI_STATS_REPORT_INTERVAL;

    dpi_backoff_conf_intvl = session->dpi_backoff_interval;
    if (!dpi_backoff_conf_intvl) dpi_backoff_conf_intvl = FSM_DPI_BACKOFF_INTERVAL;

    report = aggr->report;
    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    /* collect and report function call traces */
    fsm_fn_periodic(session);

    now = time(NULL);

    if ((now - dispatch->periodic_report_ts) >= dpi_report_conf_intvl)
    {
        windows = report->flow_windows;
        window = *windows;
        /* collect and log pcap stats */
        fsm_get_intf_pcap_stats();

        /* collect and log nfqueue stats */
        fsm_get_nfqueue_stats();
        LOGI("%s: %s: total flows: %zu held flows: %zu, reported flows: %zu",
             __func__, session->name, aggr->total_flows, aggr->held_flows,
             window->num_stats);

        LOGI("%s: unix_ipc: io successes: %" PRIu64
             ", io failures: %" PRIu64, __func__,
             g_fsm_io_success_cnt, g_fsm_io_failure_cnt);

        dispatch->periodic_report_ts = now;

        pb = dpi_stats_serialize_counter_report(&dpi_report);
        if (pb == NULL) return;

        session->ops.send_pb_report(session, session->dpi_stats_report_topic, pb->buf, pb->len);
        dpi_stats_free_packed_buffer(pb);
        
        fsm_dpi_recycle_nfe_conns();
    }

    if ((now - dispatch->periodic_backoff_ts) >= dpi_backoff_conf_intvl)
    {
        nf_queue_backoff_update(false, UINT32_MAX);
        dispatch->periodic_backoff_ts = now;
    }

    rc = fsm_dpi_send_report(session);
    if (rc != 0)
    {
        LOGD("%s: report transmission failed", __func__);
    }
    net_md_reset_aggregator(aggr);

    /* Activate the observation window */
    net_md_activate_window(aggr);

    return;
}


/**
 * @brief sets up a dispatcher plugin's function pointers
 *
 * Populates the dispatcher with the various function pointers
 * (packet handler, periodic routine)
 * @param session the dpi dispatcher session
 * @return 0 if successful.
 */
int
fsm_dispatch_set_ops(struct fsm_session *session)
{
    struct fsm_session_ops *session_ops;
    struct fsm_parser_ops *dispatch_ops;

    /* Set the fsm session */
    session->handler_ctxt = session;

    /* Set the plugin specific ops */
    dispatch_ops = &session->p_ops->parser_ops;
    dispatch_ops->handler = fsm_dpi_handler;

    session_ops = &session->ops;
    session_ops->periodic = fsm_dpi_periodic;

    return 0;
}


/* nfe_ext_conn_alloc()
 *
 * libnfe provides weak symbols for connection allocation and deallocation
 * through nfe_ext_conn_alloc() and nfe_ext_conn_free() making them easy to
 * intercept. In this example, the connection is expanded to contain extra
 * stuff for dpi.
 */
void *
nfe_ext_conn_alloc(size_t size, const struct nfe_tuple *tuple)
{
    struct net_md_stats_accumulator *acc;

    if (!(acc = CALLOC(1, sizeof(*acc) + size)))
        return NULL;
    return acc->priv;
}

/* nfe_ext_conn_free()
 *
 * The counter-part to nfe_ext_conn_alloc().
 */
void
nfe_ext_conn_free(void *p, const struct nfe_tuple *tuple)
{
    struct net_md_stats_accumulator *acc;
    struct net_md_aggregator *aggr;

    acc = container_of(p, struct net_md_stats_accumulator, priv);

    aggr = acc->aggr;
    aggr->total_flows--;
    net_md_free_acc(acc);
    FREE(acc);
}