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

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <ev.h>
#include <errno.h>

#include "os_types.h"
#include "os.h"
#include "log.h"
#include "network_metadata_report.h"
#include "network_metadata.h"
#include "fcm_report_filter.h"
#include "fcm.h"
#include "fcm_filter.h"
#include "lan_stats.h"
#include "util.h"
#include "policy_tags.h"
#include "memutil.h"
#include "kconfig.h"
#include "ovsdb.h"
#include "ovsdb_table.h"
#include "ovsdb_utils.h"
#include "data_report_tags.h"
#include "nf_utils.h"

ovsdb_table_t table_Connection_Manager_Uplink;

/**
 * Singleton tracking the plugin state
 */
static lan_stats_mgr_t g_lan_stats =
{
    .initialized = false,
};

/*
 * @brief interface type will be converted from enum to
 * string
 */
char *
uplink_iface_type_to_str(uplink_iface_type val)
{
    const struct iface_type *map;
    size_t array_size;
    int i;

    array_size = ARRAY_SIZE(iface_type_map);
    map = iface_type_map;
    for (i = 0; i < array_size; i++)
    {
        if (val == map->value) return map->iface_type;
        map++;
    }

    map = iface_type_map;
    return map->iface_type;
}

/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int
lan_stats_session_cmp(const void *a, const void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a ==  p_b) return 0;
    if (p_a < p_b) return -1;

    return 1;
}

lan_stats_instance_t *
lan_stats_lookup_session(fcm_collect_plugin_t *collector)
{
    lan_stats_instance_t *lan_stats_instance;
    lan_stats_mgr_t *mgr;
    ds_tree_t *sessions;

    mgr = lan_stats_get_mgr();
    if (!mgr->initialized) return NULL;

    sessions = &mgr->lan_stats_sessions;
    lan_stats_instance = ds_tree_find(sessions, collector);
    return lan_stats_instance;
}

/**
 * @brief look up or allocate a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
lan_stats_instance_t *
lan_stats_get_session(fcm_collect_plugin_t *collector)
{
    lan_stats_instance_t *lan_stats_instance;
    lan_stats_mgr_t *mgr;
    ds_tree_t *sessions;

    mgr = lan_stats_get_mgr();
    sessions = &mgr->lan_stats_sessions;

    lan_stats_instance = ds_tree_find(sessions, collector);
    if (lan_stats_instance != NULL) return lan_stats_instance;

    LOGD("%s: Adding a new session", __func__);
    lan_stats_instance = CALLOC(1, sizeof(*lan_stats_instance));
    if (lan_stats_instance == NULL) return NULL;

    lan_stats_instance->initialized = false;

    ds_tree_insert(sessions, lan_stats_instance, collector);
    return lan_stats_instance;
}

/**
 * @brief returns the pointer to the plugin's global state tracker
 */
lan_stats_mgr_t *
lan_stats_get_mgr(void)
{
    return &g_lan_stats;
}

/**
 * @brief returns the pointer to the active plugin
 */
lan_stats_instance_t *
lan_stats_get_active_instance(void)
{
    lan_stats_mgr_t *mgr;

    mgr = lan_stats_get_mgr();
    if (mgr == NULL) return NULL;

    return mgr->active;
}

static void
lan_stats_set_data_report_tag(struct net_md_stats_accumulator *acc)
{
    struct data_report_tags **report_tags_array;
    struct data_report_tags *report_tags;
    struct str_set *smac_report_set;
    struct str_set *dmac_report_set;
    struct net_md_flow_key *key;
    struct str_set *report_tag;
    struct flow_key *fkey;
    char smac[32];
    char dmac[32];
    size_t idx;
    size_t i;

    key = acc->key;
    if (key == NULL) return;

    fkey = acc->fkey;
    if (fkey == NULL) return;

    fkey->num_data_report = 0;
    smac_report_set = NULL;
    dmac_report_set = NULL;

    if (key->smac != NULL)
    {
        MEMZERO(smac);
        snprintf(smac, sizeof(smac), PRI_os_macaddr_lower_t, FMT_os_macaddr_pt(key->smac));
        smac_report_set = data_report_tags_get_tags(key->smac);
        if (smac_report_set != NULL)
        {
            fkey->num_data_report++;
        }
        else
        {
            LOGD("%s(): report details are empty for smac %s", __func__,
                 smac);
        }
    }

    if (key->dmac != NULL)
    {
        MEMZERO(dmac);
        snprintf(dmac, sizeof(dmac), PRI_os_macaddr_lower_t, FMT_os_macaddr_pt(key->dmac));
        dmac_report_set = data_report_tags_get_tags(key->dmac);
        if (dmac_report_set != NULL)
        {
            fkey->num_data_report++;
        }
        else
        {
            LOGD("%s(): report details are empty for dmac %s", __func__,
                 dmac);
        }
    }

    if (fkey->num_data_report == 0) return;

    report_tags_array = CALLOC(fkey->num_data_report, sizeof(*report_tags_array));
    idx = 0;

    if (smac_report_set != NULL)
    {
        report_tags = CALLOC(1, sizeof(*report_tags));
        report_tags_array[idx] = report_tags;

        report_tag = CALLOC(smac_report_set->nelems, sizeof(*report_tag));
        report_tags->data_report = report_tag;

        report_tag->nelems = smac_report_set->nelems;
        report_tag->array = CALLOC(report_tag->nelems, sizeof(*report_tag->array));
        for (i = 0; i < report_tag->nelems; i++)
        {
            report_tag->array[i] = STRDUP(smac_report_set->array[i]);
        }
        report_tags->id = STRDUP(smac);
        idx++;
    }

    if (dmac_report_set != NULL)
    {
        report_tags = CALLOC(1, sizeof(*report_tags));
        report_tags_array[idx] = report_tags;

        report_tag = CALLOC(dmac_report_set->nelems, sizeof(*report_tag));
        report_tags->data_report = report_tag;

        report_tag->nelems = dmac_report_set->nelems;
        report_tag->array = CALLOC(report_tag->nelems, sizeof(*report_tag->array));
        for (i = 0; i < report_tag->nelems; i++)
        {
            report_tag->array[i] = STRDUP(dmac_report_set->array[i]);
        }
        report_tags->id = STRDUP(dmac);
    }

    fkey->data_report = report_tags_array;

    return;
}


/**
 * @brief callback from the accumulator reporting
 *
 * Called on the reporting of an accumulator
 * @param aggr the lan_stats aggregator
 * @param the accumulator being reported
 */
static void
lan_stats_on_acc_report(struct net_md_aggregator *aggr,
                        struct net_md_stats_accumulator *acc)
{
    if (aggr == NULL) return;
    if (acc == NULL) return;

    /* Add data report tags */
    lan_stats_set_data_report_tag(acc);
}


static int
lan_stats_alloc_aggr(lan_stats_instance_t *lan_stats_instance)
{
    struct net_md_aggregator_set aggr_set;
    fcm_collect_plugin_t *collector;
    struct net_md_aggregator *aggr;
    struct node_info node_info;
    int report_type = 0;

    collector = lan_stats_instance->collector;
    memset(&aggr_set, 0, sizeof(aggr_set));
    node_info.node_id = collector->get_mqtt_hdr_node_id();
    node_info.location_id = collector->get_mqtt_hdr_loc_id();
    aggr_set.info = &node_info;
    if (collector->fmt == FCM_RPT_FMT_CUMUL)
        report_type = NET_MD_REPORT_ABSOLUTE;
    else if (collector->fmt == FCM_RPT_FMT_DELTA)
        report_type = NET_MD_REPORT_RELATIVE;
    aggr_set.report_type = report_type;
    aggr_set.num_windows = MAX_HISTOGRAMS;
    aggr_set.acc_ttl = (2 * collector->report_interval);
    aggr_set.send_report = net_md_send_report;
    aggr_set.on_acc_report = lan_stats_on_acc_report;
    aggr_set.report_stats_type = NET_MD_LAN_FLOWS;
    aggr = net_md_allocate_aggregator(&aggr_set);
    if (aggr == NULL)
    {
        LOGD("Aggregator allocation failed\n");
        return -1;
    }
    lan_stats_instance->aggr = aggr;
    collector->plugin_ctx = lan_stats_instance;

   return 0;
}

static void
lan_stats_activate_window(fcm_collect_plugin_t *collector)
{
    lan_stats_instance_t *lan_stats_instance;
    struct net_md_aggregator *aggr;
    bool ret = false;

    lan_stats_instance = collector->plugin_ctx;
    aggr = lan_stats_instance->aggr;
    if (aggr == NULL)
    {
        LOGD("%s: Aggergator is empty", __func__);
        return;
    }

    ret = net_md_activate_window(aggr);
    if (!ret)
    {
        LOGD("%s: Aggregator window activation failed", __func__);
        return;
    }
}

static void
lan_stats_close_window(fcm_collect_plugin_t *collector)
{
    lan_stats_instance_t *lan_stats_instance;
    struct flow_uplink uplink = {0};
    struct net_md_aggregator *aggr;
    struct flow_window **windows;
    struct flow_window *window;
    struct flow_report *report;
    char *iface_type_holder;
    lan_stats_mgr_t *mgr;
    bool ret;

    mgr = lan_stats_get_mgr();
    if (!mgr->initialized) return;

    if (collector == NULL) return;

    lan_stats_instance = collector->plugin_ctx;
    if (lan_stats_instance == NULL) return;

    aggr = lan_stats_instance->aggr;
    if (aggr == NULL) return;

    if (mgr->uplink_if_type)
    {
        iface_type_holder = uplink_iface_type_to_str(mgr->uplink_if_type);
        uplink.uplink_if_type = iface_type_holder;
        uplink.uplink_changed = mgr->curr_uplinked_changed;
    }

    ret = net_md_add_uplink(aggr, &uplink);
    if (!ret)
    {
        LOGT("%s: Failed to add uplink to window", __func__);
    }
    ret = net_md_close_active_window(aggr);
    if (!ret)
    {
        LOGD("%s: Aggregator close window failed", __func__);
        return;
    }

    report = aggr->report;
    if (report == NULL) return;

    windows = report->flow_windows;
    window = *windows;

    LOGI("%s: %s: total flows: %zu, held flows: %zu, reported flows: %zu, eth pairs %zu",
         __func__, collector->name, aggr->total_flows, aggr->held_flows,
         window->num_stats, aggr->total_eth_pairs);
}

static void
lan_stats_send_aggr_report(lan_stats_instance_t *lan_stats_instance)
{
    fcm_collect_plugin_t *collector;
    struct net_md_aggregator *aggr;
    bool ret = false;

    collector = lan_stats_instance->collector;
    aggr = lan_stats_instance->aggr;
    if (aggr == NULL)
    {
        LOGD("Aggergator is empty\n");
        return;
    }

    if (net_md_get_total_flows(aggr) <= 0)
    {
        net_md_reset_aggregator(aggr);
        return;
    }

    LOGT("%s(): sending the report", __func__);
    ret = aggr->send_report(aggr, collector->mqtt_topic);
    if (ret == false)
    {
        LOGD("%s: Aggregator send report failed", __func__);
        return;
    }

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
    {
        net_md_log_aggr(aggr);
    }
}

bool
lan_stats_is_mac_in_tag(char *tag, os_macaddr_t *mac)
{
    char mac_s[32] = { 0 };

    snprintf(mac_s, sizeof(mac_s), PRI_os_macaddr_lower_t,
             FMT_os_macaddr_pt(mac));

    return om_tag_in(mac_s, tag);

}

static void
lan_stats_send_report_cb(fcm_collect_plugin_t *collector)
{
    lan_stats_instance_t *lan_stats_instance;
    lan_stats_mgr_t *mgr;
    char *ct_zone;
    uint16_t tmp_zone;

    if (collector == NULL) return;

    if (collector->mqtt_topic == NULL) return;

    mgr = lan_stats_get_mgr();
    lan_stats_instance = lan_stats_get_active_instance();
    if (lan_stats_instance != collector->plugin_ctx)
    {
      LOGT("%s(): Not an active instance, not sending report", __func__);
      return;
    }

    /* Accept zone change after reporting */
    ct_zone = collector->get_other_config(collector, "ct_zone");
    tmp_zone = 0;
    if (ct_zone) tmp_zone = atoi(ct_zone);

    if (lan_stats_instance->ct_zone != tmp_zone)
    {
        lan_stats_instance->ct_zone = tmp_zone;
        LOGD("%s: updated zone: %d", __func__, lan_stats_instance->ct_zone);
    }

    mgr->report = true;
    lan_stats_close_window(collector);
    lan_stats_send_aggr_report(lan_stats_instance);
    lan_stats_activate_window(collector);
}


void
lan_stats_collect_cb(fcm_collect_plugin_t *collector)
{
    lan_stats_instance_t *lan_stats_instance;
    lan_stats_mgr_t *mgr;

    if (collector == NULL) return;

    mgr = lan_stats_get_mgr();
    lan_stats_instance = collector->plugin_ctx;

    /* collect stats only for active instance. */
    if (lan_stats_instance != mgr->active) return;

    lan_stats_process_aggr(lan_stats_instance->aggr, collector->aggr);
}


void lan_stats_plugin_exit(fcm_collect_plugin_t *collector)
{
    lan_stats_instance_t *lan_stats_instance;
    struct net_md_aggregator *aggr;
    lan_stats_mgr_t *mgr;

    mgr = lan_stats_get_mgr();
    if (!mgr->initialized) return;

    if (mgr->num_sessions == 0) return;
    mgr->num_sessions--;

    lan_stats_instance = lan_stats_lookup_session(collector);
    if (lan_stats_instance == NULL)
    {
        LOGI("%s(): could not find instance", __func__);
        return;
    }

    /* free the parent tag */
    FREE(lan_stats_instance->parent_tag);

    /* free the aggregator */
    aggr = lan_stats_instance->aggr;
    if (aggr == NULL)
    {
        LOGD("%s(): Aggregator is empty", __func__);
        return;
    }
    net_md_close_active_window(aggr);
    aggr->five_tuple_flows = NULL;
    aggr->eth_pairs = NULL;
    net_md_free_aggregator(aggr);
    FREE(aggr);

    /* delete the session */
    ds_tree_remove(&mgr->lan_stats_sessions, lan_stats_instance);
    FREE(lan_stats_instance);

    /* mark the remaining session as active if any */
    lan_stats_instance = ds_tree_head(&mgr->lan_stats_sessions);
    if (lan_stats_instance != NULL) mgr->active = lan_stats_instance;

    if (mgr->num_sessions == 0) lan_stats_exit_mgr();
    return;
}

void
lan_stats_plugin_close_cb(fcm_collect_plugin_t *collector)
{
    LOGD("%s: lan stats stats plugin stopped", __func__);
    lan_stats_plugin_exit(collector);
}

/**
 * @brief maintaining uplink info in "lan_stats_mgr_t"
 */
void
link_stats_collect_cb(uplink_iface_type uplink_if_type)
{
    lan_stats_mgr_t *mgr;

    if (uplink_if_type == IFTYPE_NONE) return;

    mgr = lan_stats_get_mgr();
    if (uplink_if_type != mgr->uplink_if_type)
    {
        mgr->report = false;
    }

    mgr->uplink_changed = true;
    mgr->uplink_if_type = uplink_if_type;
}

/* Entry function for plugin */
int lan_stats_plugin_init(fcm_collect_plugin_t *collector)
{
    lan_stats_instance_t *lan_stats_instance;
    lan_stats_mgr_t *mgr;
    char *parent_tag;
    char *active;
    char *name;
    char *ct_zone;
    int rc;

    mgr = lan_stats_get_mgr();
    if (!mgr->initialized)
    {
        LOGI("%s(): initializing lan stats manager", __func__);
        lan_stats_init_mgr(collector->loop);
    }

    /* adding ovsdb registration*/
    mgr->ovsdb_init();

    if (mgr->num_sessions == mgr->max_sessions)
    {
        LOGI("%s: max lan stats session %d reached. Exiting", __func__,
             mgr->max_sessions);
        return -1;
    }

    lan_stats_instance = lan_stats_get_session(collector);
    if (lan_stats_instance == NULL)
    {
        LOGD("%s: could not add lan stats instance", __func__);
        return -1;
    }

    if (lan_stats_instance->initialized) return 0;
    lan_stats_instance->collector = collector;
    lan_stats_instance->name = collector->name;

    lan_stats_instance->session = collector->session;

    if (collector->collect_client != NULL)
    {
        lan_stats_instance->c_client = collector->collect_client;
    }

    if (collector->report_client != NULL)
    {
        lan_stats_instance->r_client = collector->report_client;
    }

    collector->collect_periodic = lan_stats_collect_cb;
    collector->send_report = lan_stats_send_report_cb;
    collector->close_plugin = lan_stats_plugin_close_cb;

    ct_zone = collector->get_other_config(collector, "ct_zone");
    if (ct_zone) lan_stats_instance->ct_zone = atoi(ct_zone);


    rc = lan_stats_alloc_aggr(lan_stats_instance);
    if (rc != 0)
    {
        LOGI("%s(): failed to allocate flow aggregator", __func__);
        return -1;
    }

    lan_stats_activate_window(collector);

    ds_dlist_init(&lan_stats_instance->ct_list, ctflow_info_t, ct_node);

    lan_stats_instance->initialized = true;

    /* Check if the session has a name */
    name = collector->name;
    mgr->num_sessions++;

    /* Check if the session has the active key set */
    parent_tag = collector->get_other_config(collector,
                                             "parent_tag");

    if (parent_tag != NULL)
    {
        lan_stats_instance->parent_tag = STRDUP(parent_tag);
    }

    if (mgr->num_sessions == 1)
    {
        LOGI("%s: %s is the active session", __func__,
             name ? name : "default");
        mgr->active = lan_stats_instance;
        return 0;
    }

    /* Check if the session has the active key set */
    active = collector->get_other_config(collector,
                                         "active");

    if (active != NULL)
    {
        LOGI("%s: %s is now the active session", __func__,
             name ? name : "default");
        mgr->active = lan_stats_instance;
    }

    return 0;
}

/*
 * @brief interface type will be converted from string to
 * enum
 */
uplink_iface_type iftype_string_to_enum(char *format)
{
    const struct iface_type *map;
    size_t array_size;
    size_t ret;
    size_t i;

    array_size = ARRAY_SIZE(iface_type_map);
    map = iface_type_map;

    for (i = 0; i < array_size; i++)
    {
        ret = strcmp(format, map->iface_type);
        if (ret == 0) return map->value;
        map++;
    }

    map = iface_type_map;
    return map->value;
}

/**
 * @brief registered callback for Connection_Manager_Uplink events
 */
void
callback_Connection_Manager_Uplink(ovsdb_update_monitor_t *mon,
                                   struct schema_Connection_Manager_Uplink *old_uplink,
                                   struct schema_Connection_Manager_Uplink *uplink)
{

    uplink_iface_type fmt;

    if (!uplink->is_used) return;

    fmt = iftype_string_to_enum(uplink->if_type);
    if (fmt == IFTYPE_ETH || fmt == IFTYPE_LTE)
    {
        link_stats_collect_cb(fmt);
    }
}

/**
 * @brief register's ovsdb initialization
 */
void
lan_stats_ovsdb_init(void)
{
    LOGI("%s(): monitoring CMU table", __func__);

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT(Connection_Manager_Uplink, is_used);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(Connection_Manager_Uplink, false);
}

/**
 * @brief deregister's ovsdb initialization
 */
void
lan_stats_ovsdb_exit(void)
{
    LOGI("%s(): unmonitoring CMU table", __func__);

    /* Deregister monitor events */
    ovsdb_table_fini(&table_Connection_Manager_Uplink);
}

void
lan_stats_init_mgr(struct ev_loop *loop)
{
    lan_stats_mgr_t *mgr;

    mgr = lan_stats_get_mgr();
    memset(mgr, 0, sizeof(*mgr));
    mgr->loop = loop;
    mgr->max_sessions = 2;
    ds_tree_init(&mgr->lan_stats_sessions, lan_stats_session_cmp,
                 lan_stats_instance_t, lan_stats_node);

    if (mgr->ovsdb_init == NULL) mgr->ovsdb_init = lan_stats_ovsdb_init;
    if (mgr->ovsdb_exit == NULL) mgr->ovsdb_exit = lan_stats_ovsdb_exit;

    mgr->debug = false;
    mgr->initialized = true;
    mgr->report = false;

    return;
}

void
lan_stats_exit_mgr(void)
{
    lan_stats_mgr_t *mgr;

    mgr = lan_stats_get_mgr();

    /* Removing ovsdb registration*/
    if (mgr->ovsdb_exit != NULL) mgr->ovsdb_exit();

    memset(mgr, 0, sizeof(*mgr));
    mgr->initialized = false;
}
