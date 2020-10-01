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
#include <jansson.h>
#include <pcap.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <time.h>

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
#include "fsm_dpi_utils.h"
#include "imc.h"
#include "qm_conn.h"

static struct imc_context g_imc_client =
{
    .initialized = false,
    .endpoint = "ipc:///tmp/imc_fsm2fcm",
};

static struct imc_dso g_imc_context = { 0 };

static bool
fsm_dpi_load_imc(void)
{
    char *dso = CONFIG_INSTALL_PREFIX"/lib/libimc.so";
    char *init = "imc_init_dso";
    struct stat st;
    char *error;
    int rc;

    rc = stat(dso, &st);
    if (rc != 0) return true; /* All ops will be void */

    dlerror();
    g_imc_context.handle = dlopen(dso, RTLD_NOW);
    if (g_imc_context.handle == NULL)
    {
        LOGE("%s: dlopen %s failed: %s", __func__, dso, dlerror());
        return false;
    }

    dlerror();
    *(void **)(&g_imc_context.init) = dlsym(g_imc_context.handle, init);
    error = dlerror();
    if (error != NULL) {
        LOGE("%s: could not get symbol %s: %s",
             __func__, init, error);
        dlclose(g_imc_context.handle);
        return false;
    }

    g_imc_context.init(&g_imc_context);

    return true;
}


static int
fsm_dpi_init_client(struct imc_context *client, imc_free_sndmsg free_cb,
                    void *hint)
{
    struct imc_sockoption opt;
    int opt_value;
    int ret;

    if (g_imc_context.init_client == NULL)
    {
        client->imc_free_sndmsg = free_cb;
        return 0;
    }

    /* initialize context */
    g_imc_context.init_context(client);

    /* Set the send threshold option */
    opt.option_name = IMC_SNDHWM;
    opt_value = 10; /* Allow 10 pending messages */
    opt.value = &opt_value;
    opt.len = sizeof(opt_value);
    ret = g_imc_context.add_sockopt(client, &opt);
    if (ret)
    {
        LOGI("%s: setting ICM_SNDHWM option failed", __func__);
        return -1;
    }

    /* Set the linger option */
    opt.option_name = IMC_LINGER;
    opt_value = 0; /* Free all pending messages immediately on close */
    opt.value = &opt_value;
    opt.len = sizeof(opt_value);
    ret = g_imc_context.add_sockopt(client, &opt);
    if (ret)
    {
        LOGD("%s: setting ICM_LINGER option failed", __func__);
        return -1;
    }

    /* Start the client */
    ret = g_imc_context.init_client(client, free_cb, hint);
    if (ret)
    {
        LOGD("%s: init_client() failed", __func__);
    }

    return ret;
}


static void
fsm_dpi_terminate_client(struct imc_context *client)
{
    if (g_imc_context.terminate_client == NULL) return;
    g_imc_context.terminate_client(client);
    g_imc_context.reset_context(client);
}


static int
fsm_dpi_client_send(struct imc_context *client, void *data,
                    size_t len, int flags)
{
    int rc;

    if (g_imc_context.client_send == NULL)
    {
        client->imc_free_sndmsg(data, client->free_msg_hint);

        return 0;
    }

    rc = g_imc_context.client_send(client, data, len, flags);

    return rc;
}


static void
free_send_msg(void *data, void *hint)
{
    free(data);
}


static int
fsm_dpi_send_report(struct fsm_session *session)
{
    struct fsm_dpi_dispatcher *dispatch;
    union fsm_dpi_context *dpi_context;
    struct net_md_aggregator *aggr;
    struct imc_context *client;
    struct packed_buffer *pb;
    char *mqtt_topic;
    int rc;

    dpi_context = session->dpi;
    if (dpi_context == NULL) return -1;

    client = &g_imc_client;
    if (!client->initialized) return -1;

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

    /* Beware, sending the pb through imc will schedule its freeing */
    rc = fsm_dpi_client_send(client, pb->buf, pb->len, IMC_DONTWAIT);
    if (rc != 0)
    {
        LOGD("%s: could not send message", __func__);
        client->io_failure_cnt++;
        rc = -1;
        goto err_send;
    }
    client->io_success_cnt++;

err_send:
    free(pb);

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
    bool ret;

    dpi_context = session->dpi;
    if (dpi_context == NULL) return false;

    dpi_plugin = &dpi_context->plugin;
    dpi_plugin->session = session;
    dpi_plugin->bound = false;

    dpi_plugin->targets = fsm_get_other_config_val(session, "targeted_devices");
    dpi_plugin->excluded_targets = fsm_get_other_config_val(session,
                                                            "excluded_devices");

    ret = fsm_dpi_add_plugin_to_dispatcher(session);
    if (!ret) return ret;

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

    /* Check if the dpi plugin is alread bound to a dispatcher */
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

        dpi_flow_info = calloc(1, sizeof(*dpi_flow_info));
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
    pair = ds_tree_head(&aggr->eth_pairs);
    while (pair != NULL)
    {
        fsm_dpi_add_plugin_to_tree(session, &pair->five_tuple_flows);
        pair = ds_tree_next(&aggr->eth_pairs, pair);
    }
    fsm_dpi_add_plugin_to_tree(session, &aggr->five_tuple_flows);
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
            ds_tree_remove(acc->dpi_plugins, dpi_flow_info);
            free(dpi_flow_info);
        }
        flow = ds_tree_next(tree, flow);
    }
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
    struct net_md_eth_pair *pair;
    pair = ds_tree_head(&aggr->eth_pairs);
    while (pair != NULL)
    {
        fsm_dpi_del_plugin_from_tree(session, &pair->five_tuple_flows);
        pair = ds_tree_next(&aggr->eth_pairs, pair);
    }
    fsm_dpi_del_plugin_from_tree(session, &aggr->five_tuple_flows);
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
    return;
}


/**
 * @brief compare 2 dpi sessions names
 *
 * @param a the first session's name
 * @param a the second session's name
 * @return 0 if names match
 */
static int
fsm_dpi_sessions_cmp(void *a, void *b)
{
    return strcmp(a, b);
}

/**
 * @brief report filter routine
 *
 * @param acc the accumulater checked to be reported
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
    /* Place holder */
}


/**
 * @brief check for UDP flow with no data
 *
 * @param acc the accumulator to check
 */
static void
fsm_dpi_check_udp_no_data(struct net_md_stats_accumulator *acc)
{
    struct net_md_flow_key *key;

    if (acc->counters.payload_bytes_count != 0) return;

    key = acc->key;
    if (key == NULL) return;

    if (key->ipprotocol != IPPROTO_UDP) return;
    LOGI("%s: destroying UDP flow with no payload", __func__);

    net_md_log_acc(acc);
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
    fsm_dpi_check_udp_no_data(acc);
}


/**
 * @brief initializes the dpi resources of a dispatcher session
 *
 * @param session the dispatcher session
 * @return true if the initialization succeeeded, false otherwise
 */
bool
fsm_init_dpi_dispatcher(struct fsm_session *session)
{
    struct net_md_aggregator_set aggr_set;
    struct fsm_dpi_dispatcher *dispatch;
    union fsm_dpi_context *dpi_context;
    struct net_md_aggregator *aggr;
    struct imc_context *client;
    struct node_info node_info;
    ds_tree_t *dpi_sessions;
    struct fsm_mgr *mgr;
    bool ret;
    int rc;

    dpi_context = session->dpi;
    if (dpi_context == NULL) return false;

    dispatch = &dpi_context->dispatch;
    dispatch->periodic_ts = time(NULL);

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

    dispatch->session = session;
    dpi_sessions = &dispatch->plugin_sessions;
    ds_tree_init(dpi_sessions, fsm_dpi_sessions_cmp,
                 struct fsm_dpi_plugin, dpi_node);

    fsm_dispatch_set_ops(session);
    fsm_dpi_bind_plugins(session);

    ret = fsm_dpi_load_imc();
    if (!ret) goto error;

    client = &g_imc_client;
    client->ztype = IMC_PUSH;
    rc = fsm_dpi_init_client(client, free_send_msg, NULL);
    if (rc)
    {
        LOGD("%s: could not initiate client", __func__);
        goto error;
    }

    ret = net_md_activate_window(dispatch->aggr);
    if (!ret)
    {
        LOGE("%s: failed to activate aggregator", __func__);
        goto error;
    }

    return true;

error:
    net_md_free_aggregator(dispatch->aggr);
    return false;
}


/**
 * @brief free the dpi resources of a dispatcher plugin
 *
 * @param session the session to free
 */
void
fsm_free_dpi_dispatcher(struct fsm_session *session)
{
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
    net_md_free_aggregator(dispatch->aggr);

    fsm_dpi_terminate_client(&g_imc_client);
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
        return true;
    }

    /* Allocate a new context */
    dpi_context = calloc(1, sizeof(*dpi_context));
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
       fsm_free_dpi_dispatcher(session);
    }
    else if (session->type == FSM_DPI_PLUGIN)
    {
        fsm_free_dpi_plugin(session);
    }

    free(session->dpi);
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
fsm_dpi_session_cmp(void *a, void *b)
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


/**
 * @brief retrieves the flow accumulator from a parsed packet
 *
 * @param net_parser the parsing info
 * @param aggr the dispatcher's aggregator
 * @return the flow accumulator
 */
struct net_md_stats_accumulator *
fsm_net_parser_to_acc(struct net_header_parser *net_parser,
                      struct net_md_aggregator *aggr)
{
    struct net_md_stats_accumulator *acc;
    struct eth_header *eth_hdr;
    struct net_md_flow_key key;
    bool is_fragment;

    eth_hdr = &net_parser->eth_header;

    memset(&key, 0, sizeof(key));
    key.smac = eth_hdr->srcmac;
    key.dmac = eth_hdr->dstmac;
    key.vlan_id = eth_hdr->vlan_id;
    key.ethertype = eth_hdr->ethertype;

    key.ip_version = net_parser->ip_version;
    if (net_parser->ip_version == 4)
    {
        struct iphdr * iphdr;

        iphdr = net_header_get_ipv4_hdr(net_parser);
        key.src_ip = (uint8_t *)(&iphdr->saddr);
        key.dst_ip = (uint8_t *)(&iphdr->daddr);

        is_fragment = fsm_dpi_is_ip_fragment(net_parser);
        if (is_fragment) return NULL;
    }
    else if (net_parser->ip_version == 6)
    {
        struct ip6_hdr *ip6hdr;

        ip6hdr = net_header_get_ipv6_hdr(net_parser);
        key.src_ip = (uint8_t *)(&ip6hdr->ip6_src.s6_addr);
        key.dst_ip = (uint8_t *)(&ip6hdr->ip6_dst.s6_addr);
    }

    key.ipprotocol = net_parser->ip_protocol;
    if (key.ipprotocol == IPPROTO_UDP)
    {
        struct udphdr *udphdr;

        udphdr = net_parser->ip_pld.udphdr;
        key.sport = udphdr->source;
        key.dport = udphdr->dest;
    }
    else if (key.ipprotocol == IPPROTO_TCP)
    {
        struct tcphdr *tcphdr;

        tcphdr = net_parser->ip_pld.tcphdr;
        key.sport = tcphdr->source;
        key.dport = tcphdr->dest;
    }

    acc = net_md_lookup_acc(aggr, &key);

    return acc;
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
    if (acc->report) return;

    /* Mark the accumulator for report */
    acc->report = true;

    /* Provision space for reporting */
    if (acc->state != ACC_STATE_WINDOW_ACTIVE) acc->aggr->active_accs++;

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

    snprintf(mac_s, sizeof(mac_s), PRI_os_macaddr_lower_t,
             FMT_os_macaddr_pt(mac));

    rc = om_tag_in(mac_s, val);
    if (rc) return true;

    ret = strncmp(mac_s, val, strlen(mac_s));
    return (ret == 0);
}


/**
 * @brief check if any mac of a ethernet header matches a given tag
 *
 * @param the mac address to check
 * @param val an opensync tag name or the string representation of a mac address
 * @return true if the mac matches the value, false otherwise
 */
static bool
fsm_dpi_find_macs_in_val(struct eth_header *eth_hdr, char *val)
{
    bool rc;

    if (val == NULL) return false;

    rc = fsm_dpi_find_mac_in_val(eth_hdr->srcmac, val);
    rc |= fsm_dpi_find_mac_in_val(eth_hdr->dstmac, val);

    return rc;
}


/**
 * @brief dipatches a received packet to the dpi plugin handlers
 *
 * Check the overall state of the flow:
 * - if marked as passthrough or blocker, do not dispatch the packet
 * - otherwise, dispatch the packet to plgins willing to inspect it.
 * @param net_parser the parsed info for the current packet
 */
static void
fsm_dispatch_pkt(struct net_header_parser *net_parser)
{
    union fsm_dpi_context *plugin_dpi_context;
    struct net_md_stats_accumulator *acc;
    struct fsm_parser_ops *parser_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dpi_plugin;
    struct fsm_dpi_plugin *plugin;
    struct eth_header *eth_hdr;
    struct fsm_mgr *mgr;
    ds_tree_t *tree;
    bool excluded;
    bool included;
    bool drop;
    bool pass;

    eth_hdr = &net_parser->eth_header;

    acc = net_parser->acc;

    if (acc == NULL) return;

    if (acc->dpi_done == 1) return;

    tree = acc->dpi_plugins;
    if (tree == NULL) return;

    mgr = fsm_get_mgr();

    info = ds_tree_head(tree);
    if (info == NULL) return;

    drop = false;
    pass = true;

    while (info != NULL && !drop)
    {
        dpi_plugin = info->session;
        plugin_dpi_context = dpi_plugin->dpi;
        plugin = &plugin_dpi_context->plugin;

        /* Check if the source or dest device is an excluded target */
        if (plugin->excluded_targets == NULL)
        {
            excluded = false;
        }
        else
        {
            excluded = fsm_dpi_find_macs_in_val(eth_hdr,
                                                plugin->excluded_targets);
        }
        if (excluded)
        {
            info = ds_tree_next(tree, info);
            continue;
        }

        /* Check if the source or dest device is a target */
        /* No explicit target means include */
        if (plugin->targets == NULL)
        {
            included = true;
        }
        else
        {
            included = fsm_dpi_find_macs_in_val(eth_hdr, plugin->targets);
        }
        if (!included)
        {
            info = ds_tree_next(tree, info);
            continue;
        }

        if (info->decision == FSM_DPI_CLEAR)
        {
            info->decision = FSM_DPI_INSPECT;
        }

        if (info->decision == FSM_DPI_INSPECT)
        {
            if (dpi_plugin->p_ops == NULL)
            {
                info = ds_tree_next(tree, info);
                continue;
            }

            parser_ops = &dpi_plugin->p_ops->parser_ops;
            if (parser_ops->handler == NULL)
            {
                info = ds_tree_next(tree, info);
                continue;
            }
            parser_ops->handler(dpi_plugin, net_parser);
        }

        drop = (info->decision == FSM_DPI_DROP);
        pass &= (info->decision == FSM_DPI_PASSTHRU);

        info = ds_tree_next(tree, info);
    }

    if (drop) mgr->set_dpi_state(net_parser, FSM_DPI_DROP);
    if (pass) mgr->set_dpi_state(net_parser, FSM_DPI_PASSTHRU);

    if (drop || pass) acc->dpi_done = 1;
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
    size_t payload_len;
    bool filter;

    dpi_context = session->dpi;
    if (dpi_context == NULL) return;

    dispatch = &dpi_context->dispatch;

    acc = fsm_net_parser_to_acc(net_parser, dispatch->aggr);
    if (acc == NULL) return;

    counters.packets_count = acc->counters.packets_count + 1;
    counters.bytes_count = acc->counters.bytes_count + net_parser->packet_len;
    payload_len = net_parser->packet_len - net_parser->parsed;
    counters.payload_bytes_count = acc->counters.payload_bytes_count + payload_len;
    net_md_set_counters(dispatch->aggr, acc, &counters);

    fsm_dpi_alloc_flow_context(session, acc);
    net_parser->acc = acc;

    filter = fsm_dpi_filter_packet(net_parser);
    if (!filter) return;

    fsm_dispatch_pkt(net_parser);
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
    ds_tree_t *dpi_sessions;

    dpi_sessions = acc->dpi_plugins;
    if (dpi_sessions == NULL) return;

    dpi_flow_info = ds_tree_head(dpi_sessions);
    while (dpi_flow_info != NULL)
    {
        remove = dpi_flow_info;
        dpi_flow_info = ds_tree_next(dpi_sessions, dpi_flow_info);
        ds_tree_remove(dpi_sessions, remove);
        free(remove);
    }
    free(dpi_sessions);
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
    acc->dpi_plugins = calloc(1, sizeof(ds_tree_t));
    if (acc->dpi_plugins == NULL) return;

    ds_tree_init(acc->dpi_plugins, fsm_dpi_session_cmp,
                 struct fsm_dpi_flow_info, dpi_node);

    dpi_context = session->dpi;
    dispatch = &dpi_context->dispatch;
    dpi_sessions = &dispatch->plugin_sessions;
    dpi_plugin = ds_tree_head(dpi_sessions);
    while (dpi_plugin != NULL)
    {
        dpi_flow_info = calloc(1, sizeof(*dpi_flow_info));
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


#define FSM_DPI_INTERVAL 120
/**
 * @brief routine periodically called
 *
 * Periodically walks the ggregator and removes the outdated flows
 * @param session the dpi dispatcher session
 */
void
fsm_dpi_periodic(struct fsm_session *session)
{
    struct fsm_dpi_dispatcher *dispatch;
    union fsm_dpi_context *dpi_context;
    struct net_md_aggregator *aggr;
    struct flow_window **windows;
    struct flow_window *window;
    struct flow_report *report;
    time_t now;
    int rc;

    dpi_context = session->dpi;
    if (dpi_context == NULL) return;

    dispatch = &dpi_context->dispatch;
    aggr = dispatch->aggr;
    if (aggr == NULL) return;

    report = aggr->report;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    now = time(NULL);
    if ((now - dispatch->periodic_ts) >= FSM_DPI_INTERVAL)
    {
        windows = report->flow_windows;
        window = *windows;

        LOGI("%s: %s: total flows: %zu held flows: %zu, reported flows: %zu",
             __func__, session->name, aggr->total_flows, aggr->held_flows,
             window->num_stats);
        LOGI("%s: imc: io successes: %" PRIu64
             ", io failures: %" PRIu64, __func__,
             g_imc_client.io_success_cnt, g_imc_client.io_failure_cnt);
        dispatch->periodic_ts = now;
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
