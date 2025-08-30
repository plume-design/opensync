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

#include <dlfcn.h>
#include <ev.h>          /* libev routines */
#include <getopt.h>      /* command line arguments */
#include <time.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#include "log.h"         /* Logging routines */
#include "json_util.h"   /* json routines */
#include "os.h"          /* OS helpers */
#include "ovsdb.h"       /* ovsdb helpers */
#include "target.h"      /* target API */
#include "schema.h"
#include "ovsdb_utils.h"
#include "qm_conn.h"
#include "dppline.h"
#include "network_metadata.h" /* Network metadataAPI */
#include "fcm.h"         /* our api */
#include "fcm_priv.h"
#include "fcm_mgr.h"
#include "fcm_gatekeeper.h"
#include "fcm_filter.h"
#include "fcm_stats.h"
#include "memutil.h"
#include "data_report_tags.h"
#include "kconfig.h"
#include "nf_utils.h"

#if defined(CONFIG_FCM_NO_DSO)
#include "ct_stats.h"
#include "lan_stats.h"
#include "intf_stats.h"
#endif

static fcm_mgr_t fcm_mgr;

static int fcm_tree_node_cmp(const void *a, const void *b)
{
    const char *name_a = a;
    const char *name_b = b;
    return (strcmp(name_a, name_b));
}

static char * fcm_get_other_config_val(ds_tree_t *other_config, char *key)
{
    struct str_pair *pair;

    if ((other_config == NULL) || (key == NULL))
        return NULL;

    pair = ds_tree_find(other_config, key);
    if (pair == NULL) return NULL;

    LOGD("%s: other_config key : %s val : %s\n", __func__, key, pair->value);
    return pair->value;
}

static void fcm_get_plugin_configs(fcm_collector_t *collector,
                                   struct schema_FCM_Collector_Config *conf)
{
    char plugin_path[2048] = {'\0'};
    char *init_fn;
    char *path;
    char *dso;
    path = fcm_get_other_config_val(collector->collect_conf.other_config,
                                    FCM_DSO_PATH);
    if (path != NULL) STRSCPY(plugin_path, path);
    else STRSCPY(plugin_path, FCM_DSO_DFLT_PATH);

    dso = fcm_get_other_config_val(collector->collect_conf.other_config,
                                   FCM_DSO);
    if (dso != NULL)
    {
        snprintf(collector->dso_path, sizeof(collector->dso_path), "%s", dso);
    }
    else
    {
        snprintf(collector->dso_path, sizeof(collector->dso_path),
                 "%s%s%s%s", plugin_path, FCM_DSO_PREFIX,
                 collector->collect_conf.name, FCM_DSO_TYPE);
    }

    init_fn = fcm_get_other_config_val(collector->collect_conf.other_config,
                                       FCM_DSO_INIT);
    if (init_fn != NULL) STRSCPY(collector->dso_init, init_fn);
    else snprintf(collector->dso_init, sizeof(collector->dso_init),
                  "%s_plugin_init", collector->collect_conf.name);
    LOGD("%s: Plugin name: %s\n", __func__, collector->dso_path);
    LOGD("%s: Plugin init function: %s\n", __func__, collector->dso_init);
}

static fcm_report_conf_t * fcm_get_report_config(char *name)
{
    fcm_mgr_t *mgr = NULL;
    fcm_report_conf_t *conf = NULL;
    ds_tree_t *report_tree = NULL;

    mgr = fcm_get_mgr();
    report_tree = &mgr->report_conf_tree;
    conf = ds_tree_find(report_tree, name);
    return conf;
}

static void fcm_set_plugin_params(fcm_collect_plugin_t *plugin,
                                  fcm_collect_conf_t *collect_conf,
                                  fcm_report_conf_t *report_conf)
{
    if (collect_conf->filter_name[0] != '\0')
        plugin->filters.collect = collect_conf->filter_name;
    if (report_conf->hist_filter[0]  != '\0')
        plugin->filters.hist = report_conf->hist_filter;
    if (report_conf->report_filter[0] != '\0')
        plugin->filters.report = report_conf->report_filter;
    plugin->sample_interval = collect_conf->sample_time;
    plugin->report_interval = report_conf->report_time;
    plugin->mqtt_topic = report_conf->mqtt_topic;
    plugin->fmt = report_conf->fmt;
}

static void fcm_clear_plugin_params(fcm_collect_plugin_t *plugin)
{
    plugin->filters.collect = NULL;
    plugin->filters.hist = NULL;
    plugin->report_interval = 0;
    plugin->filters.report = NULL;
    plugin->mqtt_topic = NULL;
    plugin->fmt = FCM_RPT_NO_FMT;
}

static void fcm_clear_report_ticks(fcm_collector_t *collector)
{
    collector->report.ticks = 0;
    collector->report.curr_ticks = 0;
    collector->report.report_time = 0;
}

static void fcm_set_report_params(fcm_collector_t *collector,
                                  fcm_report_conf_t *report_conf)
{
    if ((collector->collect_conf.sample_time == 0) ||
        (report_conf->report_time == 0))
    {
        fcm_clear_report_ticks(collector);
        return;
    }
    // if there is change in report_time update the ticks
    if (collector->report.report_time != report_conf->report_time)
    {
        // collector->report.curr_ticks = 0;
        collector->report.ticks =  report_conf->report_time /
                                   collector->collect_conf.sample_time;
        collector->report.report_time = report_conf->report_time;
    }
}

static unsigned int fcm_return_min_timer(fcm_mgr_t *mgr)
{
    fcm_collector_t *collector = NULL;
    fcm_collect_conf_t *collect_conf = NULL;
    ds_tree_t *collectors = NULL;
    unsigned int min_timer = INT_MAX;

    collectors = &mgr->collect_tree;
    collector = ds_tree_head(collectors);
    while (collector != NULL)
    {
        collect_conf = &collector->collect_conf;
        min_timer = MIN(min_timer, collect_conf->sample_time);
        collector = ds_tree_next(collectors, collector);
    }
    return min_timer;
}

static void fcm_reset_collect_interval(ev_timer *timer, unsigned int val)
{
    fcm_mgr_t *mgr = NULL;
    mgr = fcm_get_mgr();
    timer->repeat = val;
    ev_timer_again(mgr->loop, timer);
}

/**
 * @brief Callback function for libev timer to purge aggregated data.
 *
 * This function is triggered periodically by a libev timer to purge
 * the aggregated data stored in the dummy aggregator.
 * @param loop   Pointer to the libev event loop.
 * @param w      Pointer to the ev_timer watcher.
 * @param revents Event flags (unused).
 */
static void fcm_purge_aggr_cb(struct ev_loop *loop, ev_timer *w, int revents)
{
    fcm_mgr_t *mgr;

    mgr = fcm_get_mgr();
    if (mgr == NULL) return;

    if (mgr->dummy_aggr == NULL) return;

    net_md_purge_aggr(mgr->dummy_aggr);
}

/**
 * @brief Restart the aggregator purge timer using the configured interval.
 */
static void fcm_restart_purge_timer(void)
{
    fcm_mgr_t *mgr;

    mgr = fcm_get_mgr();
    if (mgr == NULL) return;

    if (mgr->aggr_purge_interval == 0) return;

    mgr->aggr_purge_timer.repeat = mgr->aggr_purge_interval;
    ev_timer_again(mgr->loop, &mgr->aggr_purge_timer);
}

static void fcm_set_aggr_purge_interval(void)
{
    fcm_report_conf_t *report_conf;
    unsigned int max_interval = 0;
    ds_tree_t *report_tree;
    fcm_mgr_t *mgr;

    mgr = fcm_get_mgr();
    if (mgr == NULL) return;

    report_tree = &mgr->report_conf_tree;
    if (report_tree == NULL) return;

    // Find maximum report time across all configurations
    report_conf = ds_tree_head(report_tree);
    while (report_conf != NULL)
    {
        if (report_conf->report_time > max_interval)
            max_interval = report_conf->report_time;
        report_conf = ds_tree_next(report_tree, report_conf);
    }

    LOGD("%s: Maximum report interval found: %u seconds", __func__, max_interval);
    // Only update if interval changed
    if (mgr->aggr_purge_interval != max_interval)
    {
        mgr->aggr_purge_interval = max_interval;
        LOGT("%s: Aggregator purge interval set to %u seconds\n", __func__, mgr->aggr_purge_interval);

        fcm_restart_purge_timer();
    }
}

static bool fcm_apply_report_config_changes(fcm_collector_t *collector)
{
    fcm_collect_conf_t *collect_conf =  NULL;
    fcm_report_conf_t *report_conf = NULL;
    fcm_collect_plugin_t *plugin = NULL;
    bool ret = false;

    collect_conf = &collector->collect_conf;
    plugin = &collector->plugin;
    report_conf =  fcm_get_report_config(collect_conf->report_name);
    if (report_conf)
    {
         fcm_set_plugin_params(plugin, collect_conf, report_conf);
         // update sample interval & report ticks
         fcm_set_report_params(collector, report_conf);
         ret = true;
    }
    else
    {
        // No report_config found may be deleted. Reset to zero
        fcm_clear_plugin_params(plugin);
        fcm_clear_report_ticks(collector);
        LOGD("%s: report config not found for collector : %s", \
              __func__, collector->collect_conf.name);
        ret = false;
    }

    fcm_set_aggr_purge_interval();
    return ret;
}

static void fcm_sample_timer_cb(struct ev_loop *loop, ev_timer *w, int revents)
{
    fcm_collect_plugin_t *plugin = NULL;
    fcm_collector_t *collector = NULL;
    fcm_mgr_t *mgr = NULL;
    bool rc;

    collector = w->data;
    if (!collector) return;
    /*
     * Accept the report_config changes for each sample_timeout
     * to get the latest report_configs
     */
    fcm_apply_report_config_changes(collector);

    mgr = fcm_get_mgr();
    if (mgr == NULL) return;

    rc = fcm_stats_get_flows(mgr);
    if (rc == false)
    {
        LOGD("%s: Failed to get flows", __func__);
        return;
    }

    plugin = &collector->plugin;
    if (plugin->collect_periodic) plugin->collect_periodic(plugin);


    if (collector->report.ticks == 0)
    {
       LOGD("%s: No reporting as report time: %d\n",
              __func__, collector->report.ticks);
       collector->report.curr_ticks = 0;
       return;
    }

    collector->report.curr_ticks++;
    // report tick count reached
    if (collector->report.curr_ticks >= collector->report.ticks)
    {
        if (plugin->send_report)
        {
            LOGD("%s: Send mqtt collector: %s report ticks: %d\n",
                 __func__, collector->collect_conf.name,
                 collector->report.curr_ticks);
          plugin->send_report(plugin);
          collector->report.count++;
        }
        collector->report.curr_ticks = 0;
    }
}

void fcm_init_purge_timer(void)
{
    fcm_mgr_t *mgr;

    mgr = fcm_get_mgr();
    if (mgr == NULL) return;

    /* Return if the purge timer is already active */
    if (ev_is_active(&mgr->aggr_purge_timer)) return;

    LOGD("%s: Initializing aggregator purge event", __func__);
    ev_init(&mgr->aggr_purge_timer, fcm_purge_aggr_cb);
    mgr->aggr_purge_timer.data = NULL;
}


static void collector_evinit(fcm_collector_t *collector,
                             fcm_collect_conf_t *collect_conf)
{
    ev_init(&collector->sample_timer, fcm_sample_timer_cb);
    collector->sample_timer.data = collector;
}

void fcm_stop_purge_timer(void)
{
    fcm_mgr_t *mgr;

    mgr = fcm_get_mgr();
    if (mgr == NULL) return;

    if (!ev_is_active(&mgr->aggr_purge_timer)) return;

    ev_timer_stop(mgr->loop, &mgr->aggr_purge_timer);
}

void init_collector_plugin(fcm_collector_t *collector)
{
    void (*plugin_init)(fcm_collect_plugin_t *collector_plugin);
    struct net_md_aggregator *aggr = NULL;
    fcm_collect_plugin_t *plugin = NULL;
    struct fcm_filter_client *c_client = NULL;
    struct fcm_filter_client *r_client = NULL;
    struct fcm_session *session;
    fcm_mgr_t *mgr;

    mgr = fcm_get_mgr();
    if (collector->plugin_init == NULL) return;

    aggr = mgr->dummy_aggr;
    *(void **)(&plugin_init) = collector->plugin_init;

    plugin = &collector->plugin;

    plugin->fcm = collector;
    plugin->loop = mgr->loop;
    plugin->get_mqtt_hdr_node_id = fcm_get_mqtt_hdr_node_id;
    plugin->get_mqtt_hdr_loc_id = fcm_get_mqtt_hdr_loc_id;
    plugin->get_other_config = fcm_plugin_get_other_config;
    plugin->name = collector->collect_conf.name;
    plugin->fcm_gk_request = fcm_gk_lookup;
    plugin->aggr = aggr;

    session = CALLOC(1, sizeof(*session));
    if (session == NULL) return;

    /** collect client */
    if (plugin->filters.collect != NULL)
    {
        c_client = CALLOC(1, sizeof(*c_client));
        if (c_client == NULL)
        {
            FREE(session);
            return;
        }

        c_client->session = session;
        c_client->name = STRDUP(plugin->filters.collect);
        fcm_filter_register_client(c_client);
        plugin->collect_client = c_client;
    }

    /** report client */
    if (plugin->filters.report != NULL)
    {
        r_client = CALLOC(1, sizeof(*r_client));
        if (r_client == NULL)
        {
            fcm_filter_deregister_client(c_client);
            FREE(c_client->name);
            FREE(c_client);
            FREE(session);
            return;
        }

        r_client->session = session;
        r_client->name = STRDUP(plugin->filters.report);
        fcm_filter_register_client(r_client);
        plugin->report_client = r_client;
    }

    plugin->session = session;
    /* call the init function of plugin */
    plugin_init(plugin);

    fcm_reset_collect_interval(&collector->sample_timer,
                            fcm_return_min_timer(mgr));
    collector->initialized = true;

    /* initialize curl */
    LOGT("%s(): initializing curl", __func__);
    gk_curl_easy_init(&mgr->ecurl);
}


#if defined(CONFIG_FCM_NO_DSO)
static struct plugin_init_table plugin_init_table[] =
{
    {
        .name = "ct_stats",
        .init = ct_stats_plugin_init,
    },
    {
        .name = "intfstats",
        .init = intf_stats_plugin_init,
    },
    {
        .name = "lanstats",
        .init = lan_stats_plugin_init,
    }
};

static bool
fcm_match_init(struct schema_FCM_Collector_Config *conf, fcm_collector_t *collector)
{
    char *prefix;
    size_t i;
    int ret;

    if (conf == NULL) return false;

    if (!strlen(conf->name)) return false;

    for (i = 0; i < ARRAY_SIZE(plugin_init_table); i++)
    {
        prefix = plugin_init_table[i].name;
        ret = strncmp(conf->name, prefix, strlen(prefix));
        if (ret != 0) continue;

        collector->plugin_init = plugin_init_table[i].init;
        if (fcm_apply_report_config_changes(collector))
            init_collector_plugin(collector);
        else
            LOGD("%s: Report config not available at plugin_init time: %s",
                __func__, collector->collect_conf.name);
    }

    return true;
}
#else
static bool
fcm_match_init(struct schema_FCM_Collector_Config *conf, fcm_collector_t *collector)
{
    return false;
}
#endif

void init_pending_collector_plugin(ds_tree_t *collect_tree)
{
    fcm_collector_t *collector = NULL;

    ds_tree_foreach(collect_tree, collector)
    {
        if (collector->initialized) continue;
        //get the report config for collector
        if (fcm_apply_report_config_changes(collector) == false) continue;
        // report config configured for the collector
        init_collector_plugin(collector);
    }
}
static void init_collect_conf_node(fcm_collect_conf_t *collect_conf,
                              struct schema_FCM_Collector_Config *schema_conf)
{
    ds_tree_t *other_config = NULL;

    if (schema_conf->interval_present)
        collect_conf->sample_time = schema_conf->interval;
    if (schema_conf->filter_name_present)
    {
        STRSCPY(collect_conf->filter_name, schema_conf->filter_name);
    }
    if (schema_conf->report_name_present)
    {
        STRSCPY(collect_conf->report_name, schema_conf->report_name);
    }
    if (schema_conf->other_config_present)
    {
        other_config = schema2tree(sizeof(schema_conf->other_config_keys[0]),
                                   sizeof(schema_conf->other_config[0]),
                                   schema_conf->other_config_len,
                                   schema_conf->other_config_keys,
                                   schema_conf->other_config);
        collect_conf->other_config = other_config;
    }
}

static void update_collect_conf_node(fcm_collect_conf_t *collect_conf,
                       struct schema_FCM_Collector_Config *schema_conf)
{
    ds_tree_t *other_config = NULL;

    if (schema_conf->interval_changed)
        collect_conf->sample_time = schema_conf->interval;
    if (schema_conf->filter_name_changed)
    {
        STRSCPY(collect_conf->filter_name, schema_conf->filter_name);
    }
    if (schema_conf->report_name_changed)
    {
        STRSCPY(collect_conf->report_name, schema_conf->report_name);
    }
    if (schema_conf->other_config_changed)
    {
        free_str_tree(collect_conf->other_config);
        other_config = schema2tree(sizeof(schema_conf->other_config_keys[0]),
                                   sizeof(schema_conf->other_config[0]),
                                   schema_conf->other_config_len,
                                   schema_conf->other_config_keys,
                                   schema_conf->other_config);
        collect_conf->other_config = other_config;
    }
}

static fcm_collector_t *lookup_collect_config(ds_tree_t *collect_tree,
                                              char *name)
{
    fcm_collector_t *collector = NULL;

    collector = ds_tree_find(collect_tree, name);
    if (collector) return collector;

    collector = CALLOC(1, sizeof(*collector));
    STRSCPY(collector->collect_conf.name, name);
    ds_tree_insert(collect_tree, collector, collector->collect_conf.name);
    LOGD("%s: New collector plugin added %s", __func__, collector->collect_conf.name);
    return collector;
}

static fcm_report_conf_t *lookup_report_config(ds_tree_t *conf_tree, char *name)
{
    fcm_report_conf_t *conf = NULL;

    conf = fcm_get_report_config(name);
    if (conf) return conf;

    conf = CALLOC(1, sizeof(*conf));
    STRSCPY(conf->name, name);
    ds_tree_insert(conf_tree, conf, conf->name);
    LOGD("%s: New report config added %s", __func__, conf->name);
    return conf;
}

static fcm_rpt_fmt_t fmt_string_to_enum (char *format)
{
    fcm_rpt_fmt_t fmt = FCM_RPT_NO_FMT;
    if (strcasecmp(format, "cumulative") == 0)
        fmt = FCM_RPT_FMT_CUMUL;
    else if (strcasecmp(format, "delta") == 0)
        fmt = FCM_RPT_FMT_DELTA;
    else if (strcasecmp(format, "raw")  == 0)
        fmt = FCM_RPT_FMT_RAW;
    return fmt;
}


static void init_report_conf_node(fcm_report_conf_t *report_conf,
                                  struct schema_FCM_Report_Config *schema_conf)
{
    ds_tree_t *other_config = NULL;

    if (schema_conf->interval_present)
        report_conf->report_time = schema_conf->interval;
    if (schema_conf->format_present)
        report_conf->fmt = fmt_string_to_enum(schema_conf->format);
    if (schema_conf->hist_interval_present)
       report_conf->hist_time = schema_conf->hist_interval;
    if (schema_conf->hist_filter_present)
    {
        STRSCPY(report_conf->hist_filter, schema_conf->hist_filter);
    }
    if (schema_conf->report_filter_present)
    {
        STRSCPY(report_conf->report_filter, schema_conf->report_filter);
    }
    if (schema_conf->mqtt_topic_present)
    {
        STRSCPY(report_conf->mqtt_topic, schema_conf->mqtt_topic);
    }
    if (schema_conf->other_config_present)
    {
        other_config = schema2tree(sizeof(schema_conf->other_config_keys[0]),
                                   sizeof(schema_conf->other_config[0]),
                                   schema_conf->other_config_len,
                                   schema_conf->other_config_keys,
                                   schema_conf->other_config);
        report_conf->other_config = other_config;
    }
}

static void update_report_conf_node(fcm_report_conf_t *report_conf,
                                    struct schema_FCM_Report_Config *schema_conf)
{
    ds_tree_t *other_config = NULL;

    if (schema_conf->interval_changed)
        report_conf->report_time = schema_conf->interval;
    if (schema_conf->format_changed)
        report_conf->fmt = fmt_string_to_enum(schema_conf->format);
    if (schema_conf->hist_interval_changed)
       report_conf->hist_time = schema_conf->hist_interval;
    if (schema_conf->hist_filter_changed)
    {
        STRSCPY(report_conf->hist_filter, schema_conf->hist_filter);
    }
    if (schema_conf->report_filter_changed)
    {
        STRSCPY(report_conf->report_filter, schema_conf->report_filter);
    }
    if (schema_conf->mqtt_topic_changed)
    {
        STRSCPY(report_conf->mqtt_topic, schema_conf->mqtt_topic);
    }
    if (schema_conf->other_config_changed)
    {
        free_str_tree(report_conf->other_config);
        other_config = schema2tree(sizeof(schema_conf->other_config_keys[0]),
                                   sizeof(schema_conf->other_config[0]),
                                   schema_conf->other_config_len,
                                   schema_conf->other_config_keys,
                                   schema_conf->other_config);
        report_conf->other_config = other_config;
    }
}

void init_report_config(struct schema_FCM_Report_Config *conf)
{
    fcm_mgr_t *mgr = NULL;
    ds_tree_t *report_conf_tree = NULL;
    fcm_report_conf_t *report_conf_node = NULL;

    fcm_init_purge_timer();

    mgr = fcm_get_mgr();
    report_conf_tree = &mgr->report_conf_tree;
    report_conf_node = lookup_report_config(report_conf_tree, conf->name);
    if (report_conf_node == NULL) return;
    init_report_conf_node(report_conf_node, conf);
    // call any pending collector_plugin init waiting for report_config
    init_pending_collector_plugin(&mgr->collect_tree);

    //Set the purge interval taking the max report value
    fcm_set_aggr_purge_interval();
}


void update_report_config(struct schema_FCM_Report_Config *conf)
{
    fcm_mgr_t *mgr = NULL;
    ds_tree_t *report_conf_tree = NULL;
    fcm_report_conf_t *report_conf_node = NULL;

    mgr = fcm_get_mgr();
    report_conf_tree = &mgr->report_conf_tree;
    report_conf_node = lookup_report_config(report_conf_tree, conf->name);

    if (report_conf_node == NULL) return;

    update_report_conf_node(report_conf_node, conf);
    /* Update the purge interval taking the max report value */
    fcm_set_aggr_purge_interval();
}

void delete_report_config(struct schema_FCM_Report_Config *conf)
{
    fcm_mgr_t *mgr = NULL;
    ds_tree_t *report_conf_tree = NULL;
    fcm_report_conf_t *report_conf_node = NULL;

    mgr = fcm_get_mgr();
    report_conf_tree = &mgr->report_conf_tree;
    report_conf_node = ds_tree_find(report_conf_tree, conf->name);
    if (report_conf_node == NULL) return;
    free_str_tree(report_conf_node->other_config);
    ds_tree_remove(report_conf_tree, report_conf_node);
    FREE(report_conf_node);
}

bool init_collect_config(struct schema_FCM_Collector_Config *conf)
{
    void (*plugin_init)(fcm_collect_plugin_t *collector_plugin);
    char *error = NULL;
    fcm_mgr_t *mgr = NULL;
    fcm_collector_t *collector = NULL;
    fcm_collect_conf_t *collect_conf = NULL;
    ds_tree_t *collect_tree = NULL;

    mgr = fcm_get_mgr();
    collect_tree = &mgr->collect_tree;
    collector = lookup_collect_config(collect_tree, conf->name);
    if (collector == NULL) return false;

    collect_conf = &collector->collect_conf;
    init_collect_conf_node(collect_conf, conf);
    fcm_get_plugin_configs(collector, conf);
    collector_evinit(collector, collect_conf);

    if (kconfig_enabled(CONFIG_FCM_NO_DSO))
    {
        return fcm_match_init(conf, collector);
    }

    dlerror();
    collector->handle = dlopen(collector->dso_path, RTLD_NOW);
    if (collector->handle == NULL)
    {
        LOGE("%s: dlopen %s failed: %s", __func__,
              collector->dso_path, dlerror());
        return false;
    }
    dlerror();
    *(void **)(&plugin_init) = dlsym(collector->handle, collector->dso_init);
    error = dlerror();
    if (error != NULL)
    {
        LOGE("%s: could not get init symbol %s: %s",
             __func__, collector->dso_init, error);
        dlclose(collector->handle);
        return false;
    }
    collector->plugin_init = plugin_init;
    if (fcm_apply_report_config_changes(collector))
        init_collector_plugin(collector);
    else
        LOGD("%s: Report config not available at plugin_init time: %s",
              __func__, collector->collect_conf.name);
    return true;
}

void update_collect_config(struct schema_FCM_Collector_Config *conf)
{
    fcm_mgr_t *mgr = NULL;
    fcm_collector_t *collector = NULL;
    fcm_collect_conf_t *collect_conf = NULL;
    ds_tree_t *collect_tree = NULL;

    mgr = fcm_get_mgr();
    collect_tree = &mgr->collect_tree;
    collector = lookup_collect_config(collect_tree, conf->name);
    if (collector == NULL) return;

    collect_conf = &collector->collect_conf;
    update_collect_conf_node(collect_conf, conf);
    // <TBD>: For config specific changes
    fcm_apply_report_config_changes(collector);
    fcm_reset_collect_interval(&collector->sample_timer,
                            fcm_return_min_timer(mgr));
}

void delete_collect_config(struct schema_FCM_Collector_Config *conf)
{
    struct fcm_filter_client *c_client, *r_client;
    fcm_collector_t *collector = NULL;
    fcm_collect_plugin_t *plugin = NULL;
    ds_tree_t *collect_tree = NULL;
    struct fcm_session *session;
    fcm_mgr_t *mgr = NULL;


    mgr = fcm_get_mgr();
    collect_tree = &mgr->collect_tree;
    collector = lookup_collect_config(collect_tree, conf->name);
    if (collector == NULL) return;

    plugin = &collector->plugin;
    // stop the sample timer
    fcm_reset_collect_interval(&collector->sample_timer, 0);
    ev_timer_stop(mgr->loop, &collector->sample_timer);
    if (plugin->close_plugin)
    {
        plugin->close_plugin(plugin);
        LOGD("%s: Plugin %s is closed\n", __func__, conf->name);
    }
    if (!kconfig_enabled(CONFIG_FCM_NO_DSO)) dlclose(collector->handle);

    session = plugin->session;
    c_client = plugin->collect_client;
    if (c_client != NULL)
    {
        fcm_filter_deregister_client(c_client);
        FREE(c_client->name);
        FREE(c_client);
    }

    r_client = plugin->report_client;
    if (r_client != NULL)
    {
        fcm_filter_deregister_client(r_client);
        FREE(r_client->name);
        FREE(r_client);
    }

    FREE(session);

    free_str_tree(collector->collect_conf.other_config);
    ds_tree_remove(collect_tree, collector);
    FREE(collector);
}


bool fcm_init_mgr(struct ev_loop *loop)
{
    struct net_md_aggregator_set aggr_set;
    struct net_md_aggregator *aggr;
    fcm_mgr_t *mgr;

    mgr = fcm_get_mgr();
    memset(mgr, 0, sizeof(fcm_mgr_t));
    mgr->loop = loop;
    mgr->cb_ovsdb_table_upsert_where = ovsdb_table_upsert_where;

    snprintf(mgr->pid, sizeof(mgr->pid), "%d", (int)getpid());
    ds_tree_init(&fcm_mgr.collect_tree, fcm_tree_node_cmp,
                 fcm_collector_t, node);
    ds_tree_init(&fcm_mgr.report_conf_tree, fcm_tree_node_cmp,
                 fcm_report_conf_t, node);

    /* Set the initial max memory threshold */
    fcm_set_max_mem();

    LOGT("%s(): initializing curl handler", __func__);
    gk_curl_easy_init(&mgr->ecurl);

    /* Set the default timer for neigh_table entries*/
    mgr->neigh_cache_ttl = FCM_NEIGH_SYS_ENTRY_TTL;

    memset(&aggr_set, 0, sizeof(aggr_set));
    aggr_set.num_windows = 1;
    aggr_set.acc_ttl = 120;
    aggr_set.report_type = NET_MD_REPORT_ABSOLUTE;
    aggr = net_md_allocate_aggregator(&aggr_set);
    if (aggr == NULL) return false;
    mgr->dummy_aggr = aggr;

    if (nf_ct_init(loop, mgr->dummy_aggr) < 0)
    {
        LOGE("Error initializing conntrack");
        return -1;
    }

    LOGI("FCM Manager Initialized\n");
    return true;
}

fcm_mgr_t * fcm_get_mgr(void)
{
    return &fcm_mgr;
}

char * fcm_get_mqtt_hdr_node_id(void)
{
    fcm_mgr_t *mgr = NULL;

    mgr = fcm_get_mgr();
    return mgr->mqtt_headers[FCM_HEADER_NODE_ID];
}


char * fcm_get_mqtt_hdr_loc_id(void)
{
    fcm_mgr_t *mgr = NULL;

    mgr = fcm_get_mgr();
    return mgr->mqtt_headers[FCM_HEADER_LOCATION_ID];
}


char * fcm_plugin_get_other_config(fcm_collect_plugin_t *plugin, char *key)
{
    fcm_collector_t *collector = NULL;
    char *ret = NULL;

    collector = plugin->fcm;
    ret = fcm_get_other_config_val(collector->collect_conf.other_config, key);
    LOGD("%s: key : %s val : %s\n", __func__, key, ret);
    return ret;
}
