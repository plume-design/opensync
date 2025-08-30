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
#include <limits.h>

#include "os.h"
#include "util.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "nf_utils.h"
#include "schema.h"
#include "log.h"
#include "ds.h"
#include "json_util.h"
#include "target.h"
#include "target_common.h"
#include "fsm.h"
#include "fsm_oms.h"
#include "fsm_internal.h"
#include "policy_tags.h"
#include "qm_conn.h"
#include "dppline.h"
#include "fsm_dpi_utils.h"
#include "policy_tags.h"
#include "gatekeeper_cache.h"
#include "dns_cache.h"
#include "memutil.h"
#include "kconfig.h"
#include "network_zone.h"
#include "fsm_ipc.h"

#define MODULE_ID LOG_MODULE_ID_OVSDB

static ovsdb_table_t table_AWLAN_Node;
static ovsdb_table_t table_Flow_Service_Manager_Config;
static ovsdb_table_t table_Openflow_Tag;
static ovsdb_table_t table_Openflow_Local_Tag;
static ovsdb_table_t table_Openflow_Tag_Group;
static ovsdb_table_t table_Node_Config;
static ovsdb_table_t table_Node_State;
static ovsdb_table_t table_FCM_Collector_Config;

/**
 * This file manages the OVSDB updates for fsm.
 * fsm monitors two tables:
 * - AWLAN_Node
 * - Flow_Service_Manager_Config table
 * AWLAN_node stores the mqtt topics used by various handlers (http, dns, ...)
 * and mqtt invariants (location id, node id)
 * The Flow_Service_Manager table stores the configuration of each handler:
 * - handler string identifier (dns, http, ...)
 * - tapping interface
 * - optional BPF filter
 *
 * The FSM manager hosts a DS tree of sessions, each session targeting
 *  a specific type of traffic. The DS tree organizing key is the handler string
 * identifier aforementioned (dns, http, ...)
 */


/**
 * @brief fsm manager.
 *
 * Container of fsm sessions
 */
static struct fsm_mgr fsm_mgr;

/**
 * @brief fsm manager accessor
 */
struct fsm_mgr *
fsm_get_mgr(void)
{
    return &fsm_mgr;
}

/**
 * @brief fsm sessions tree accessor
 */
ds_tree_t *
fsm_get_sessions(void)
{
    struct fsm_mgr *mgr;

    mgr = fsm_get_mgr();
    return &mgr->fsm_sessions;
}

static int
fsm_sessions_cmp(const void *a, const void *b)
{
    return strcmp(a, b);
}

#define FSM_NODE_MODULE "fsm"
#define FSM_NODE_STATE_MEM_KEY "max_mem"

/**
 * @brief callback function invoked when tag value is
 *        changed.
 */
static bool
fsm_tag_update_cb(om_tag_t *tag,
                  struct ds_tree *removed,
                  struct ds_tree *added,
                  struct ds_tree *updated)
{
    network_zone_tag_update_cb(tag, removed, added, updated);
    fsm_process_tag_update(tag, removed, added, updated);
    return true;
}

/**
 * @brief register callback for receiving tag value updates
 */
bool
fsm_tag_update_init(void)
{
    struct tag_mgr fsm_tagmgr;

    memset(&fsm_tagmgr, 0, sizeof(fsm_tagmgr));
    fsm_tagmgr.service_tag_update = fsm_tag_update_cb;
    om_tag_init(&fsm_tagmgr);
    return true;
}

/**
 * @brief Set the initial memory usage threshold.
 *
 * It can be overridden through the ovsdb Node_Config entries
 */
static void
fsm_set_max_mem(void)
{
    struct fsm_mgr *mgr;
    int rc;

    mgr = fsm_get_mgr();

    /* Stash the max amount of memory available */
    rc = sysinfo(&mgr->sysinfo);
    if (rc != 0)
    {
        rc = errno;
        LOGE("%s: sysinfo failed: %s", __func__, strerror(rc));
        memset(&mgr->sysinfo, 0, sizeof(mgr->sysinfo));
    }

    mgr->max_mem = CONFIG_FSM_MEM_MAX * 1024;

    LOGI("%s: fsm default max memory usage: %" PRIu64 " kB", __func__,
         mgr->max_mem);
}


/**
 * @brief fsm manager init routine
 */
void
fsm_init_mgr(struct ev_loop *loop)
{
    struct fsm_mgr *mgr;

    mgr = fsm_get_mgr();
    memset(mgr, 0, sizeof(*mgr));
    mgr->loop = loop;
    snprintf(mgr->pid, sizeof(mgr->pid), "%d", (int)getpid());
    ds_tree_init(&mgr->fsm_sessions, fsm_sessions_cmp,
                 struct fsm_session, fsm_node);

    /* Set the initial max memory threshold */
    fsm_set_max_mem();

    /* initialize tag tree */
    ds_tree_init(&mgr->dpi_client_tags_tree, (ds_key_cmp_t *) strcmp,
                 struct fsm_dpi_client_tags, next);

    /* register for tag update callback */
    fsm_tag_update_init();
}


/**
 * @brief fsm manager reset routine
 */
void
fsm_reset_mgr(void)
{
    struct fsm_session *session;
    struct fsm_session *remove;
    struct fsm_session *next;
    ds_tree_t *sessions;
    struct fsm_mgr *mgr;

    mgr = fsm_get_mgr();
    sessions = fsm_get_sessions();
    session = ds_tree_head(sessions);
    while (session != NULL)
    {
        next = ds_tree_next(sessions, session);
        remove = session;
        ds_tree_remove(sessions, remove);
        fsm_policy_deregister_client(&remove->policy_client);
        fsm_free_session(remove);
        session = next;
    }
    free_str_tree(mgr->mqtt_headers);
}


/**
 * @brief walks the tree of sessions
 *
 * Debug function, logs each tree entry
 */
static void
fsm_walk_sessions_tree(void)
{
    ds_tree_t *sessions = fsm_get_sessions();
    struct fsm_session *session = ds_tree_head(sessions);

    LOGT("Walking sessions tree");
    while (session != NULL) {
        LOGT("  handler: %s, topic: %s",
             session->name ? session->name : "None",
             session->topic ? session->topic : "None");
        session = ds_tree_next(sessions, session);
    }
}


/**
 * @brief validate an ovsdb to private object conversion
 *
 * Expects a non NULL converted object if the number of elements
 * was not null, a null object otherwise.
 * @param converted converted object
 * @param len number of elements of the ovsdb object
 */
bool
fsm_core_check_conversion(void *converted, int len)
{
    if (len == 0) return false;
    if (converted == NULL) return false;

    return true;
}


/**
 * @brief retrieves the value from the provided key in the
 * other_config value
 *
 * @param session the fsm session owning the other_config
 * @param conf_key other_config key to look up
 */
char *
fsm_get_other_config_val(struct fsm_session *session, char *key)
{
    struct fsm_session_conf *fconf;
    struct str_pair *pair;
    ds_tree_t *tree;

    if (session == NULL) return NULL;

    fconf = session->conf;
    if (fconf == NULL) return NULL;

    tree = fconf->other_config;
    if (tree == NULL) return NULL;

    pair = ds_tree_find(tree, key);
    if (pair == NULL) return NULL;

    return pair->value;
}

static int
get_home_bridge(char *if_name, char *bridge, size_t len)
{
    char shell_cmd[1024];
    char buf[1024];

    FILE *fcmd = NULL;
    int rc = -1;

    if (if_name == NULL) return 0;
    if (bridge == NULL) return -1;

    snprintf(shell_cmd, sizeof(shell_cmd),
             "brctl show | awk 'NF>1 && NR>1 {print $1}'");

    fcmd = popen(shell_cmd, "r");
    if (fcmd == NULL)
    {
        LOGD("Error executing command.::shell_cmd=%s", shell_cmd);
        goto exit;
    }

    LOGT("Executing command.::shell_cmd=%s ", shell_cmd);

    while (fgets(buf, sizeof(buf), fcmd) != NULL)
    {
        LOGI("%s: home bridge: %s", __func__, buf);
    }

    if (ferror(fcmd))
    {
        LOGE("%s: fgets() failed", __func__);
        goto exit;
    }

    rc = pclose(fcmd);
    fcmd = NULL;

    strchomp(buf, " \t\r\n");
    strscpy(bridge, buf, len);

exit:
    if (fcmd != NULL)
    {
        pclose(fcmd);
    }

    return rc;
}

/**
 * @brief set the tx interface a plugin might use
 *
 * @param session the fsm session
 */
static void
fsm_set_tx_intf(struct fsm_session *session)
{
    const char *name;
    size_t ret;

    name = fsm_get_other_config_val(session, "tx_intf");
    if (name != NULL)
    {
        STRSCPY(session->tx_intf, name);
        return;
    }

#if defined(CONFIG_TARGET_LAN_BRIDGE_NAME)
    name = CONFIG_TARGET_LAN_BRIDGE_NAME;
#else
    name = session->bridge;
    if ((name == NULL) || (name[0] == 0)) return;
#endif

    ret = snprintf(session->tx_intf, sizeof(session->tx_intf), "%s", name);

    if (ret >= sizeof(session->tx_intf))
    {
        LOGW("%s: failed to assemble tx mirror from %s (%s), too long",
             __func__, name, session->tx_intf);
        session->tx_intf[0] = 0;
    }
}


void
fsm_process_provider(struct fsm_session *session)
{
    struct fsm_session *service;
    bool reset;

    if (session->type == FSM_WEB_CAT) return;

    /* If no provider plugin yet, attempt update */
    if (session->provider_plugin == NULL)
    {
        service = session->service;
        session->provider_plugin = service;
        if (service != NULL)
        {
            session->provider = STRDUP(service->name);
            session->provider_ops = &service->p_ops->web_cat_ops;
        }
        return;
    }

    LOGI("%s: original provider %s, new provider %s", __func__,
         session->provider, session->service ? session->service->name : "none");

    /* Provider was set, check if it changed */
    reset = (session->provider_plugin != session->service);

    if (reset)
    {
        sleep(2);
        LOGEM("%s: provider change detected. Restarting", __func__);
        exit(EXIT_SUCCESS);
    }
}


void
fsm_update_client(void *context,
                  struct policy_table *table)
{
    struct fsm_policy_client *client;
    struct fsm_session *session;

    session = (struct fsm_session *)context;
    if (session == NULL) return;

    client = &session->policy_client;
    client->table = table;
    if (session->ops.update != NULL) session->ops.update(session);
}


void
fsm_update_client_table(struct fsm_session *session, char *table_name)
{
    struct policy_table *table;
    int cmp;

    table = fsm_policy_find_table(table_name);

    /* Handle the case with no previous table set */
    if (session->policy_client.name == NULL) goto update;

    /* Handle the case with a table reset */
    if (table_name == NULL) goto update;

    /* No update */
    cmp = strcmp(session->policy_client.name, table_name);
    if (cmp == 0) return;

update:
    session->policy_client.name = IS_NULL_PTR(table_name) ? NULL : STRDUP(table_name);
    fsm_update_client(session, table);
}


static int
fsm_get_no_bridge(char *if_name, char *bridge, size_t len)
{
    if (if_name == NULL) return 0;
    if (bridge == NULL) return -1;

    strscpy(bridge, "no_bridge", len);

    return 0;
}


static int
fsm_check_config_update(struct fsm_session *session,
                        struct schema_Flow_Service_Manager_Config *conf)
{
    ds_tree_t *other_config;
    struct str_pair *pair;
    char *keep_config;
    bool check;
    int ret;

    ret = 0;

    other_config = schema2tree(sizeof(conf->other_config_keys[0]),
                               sizeof(conf->other_config[0]),
                               conf->other_config_len,
                               conf->other_config_keys,
                               conf->other_config);
    check = fsm_core_check_conversion(other_config, conf->other_config_len);
    if (!check)
    {
        ret = (session->keep_config == FSM_KEEP_CONFIG ? 0 : 1);
        goto exit;
    }

    keep_config = NULL;
    pair = ds_tree_find(other_config, "keep_config");
    if (pair != NULL) keep_config = pair->value;

    if (!keep_config)
    {
        ret = (session->keep_config == FSM_KEEP_CONFIG ? 0 : 1);
        goto exit;
    }

    /* Check if keep_config is set to true */
    ret = strncmp(keep_config, "true", strlen("true"));
    if (ret == 0)
    {
        /*
         * If the session transitions to a keep config state,
         * apply the updated config first
         */
        ret = (session->keep_config == FSM_KEEP_CONFIG ? 0 : 1);
         session->keep_config = FSM_KEEP_CONFIG;
        goto exit;
    }

    ret = strncmp(keep_config, "false", strlen("false"));
    if (ret == 0)
    {
        session->keep_config = FSM_UPDATE_CONFIG;
        ret = 1;
        goto exit;
    }

exit:
    free_str_tree(other_config);
    return ret;
}


/**
 * @brief update the ovsdb configuration of the session
 *
 * Copy ovsdb fields in their fsm_session recipient
 * @param session the fsm session to update
 * @param conf a pointer to the ovsdb provided configuration
 */
bool
fsm_session_update(struct fsm_session *session,
                   struct schema_Flow_Service_Manager_Config *conf)
{
    struct fsm_session_conf *fconf;
    ds_tree_t *other_config;
    struct fsm_mgr *mgr;
    char *policy_table;
    char *flood;
    bool check;
    bool ret;

    char *session_name = NULL;

    if (session->conf)
    {
        session_name = session->conf->handler;
    }

    ret = fsm_check_config_update(session, conf);
    if (ret == 0)
    {
        LOGI("%s: session %s: keep_config set to true, skipping update",
             __func__, session_name ? session_name : "None");

        return true;
    }
    LOGI("%s: session %s: keep_config not enabled, proceeding", __func__,
         session_name ? session_name : "None");
    mgr = fsm_get_mgr();

    fconf = session->conf;

    /* Free old conf. Could be more efficient */
    fsm_free_session_conf(fconf);
    session->conf = NULL;

    fconf = CALLOC(1, sizeof(*fconf));
    if (fconf == NULL) return false;
    session->conf = fconf;

    if (strlen(conf->handler) == 0) goto err_free_fconf;

    fconf->handler = STRDUP(conf->handler);
    if (fconf->handler == NULL) goto err_free_fconf;

    if ((strlen(conf->if_name) != 0) && (kconfig_enabled(CONFIG_FSM_TAP_INTF)))
    {
        fconf->if_name = STRDUP(conf->if_name);
        if (fconf->if_name == NULL) goto err_free_fconf;
    }

    if (strlen(conf->pkt_capt_filter) != 0)
    {
        fconf->pkt_capt_filter = STRDUP(conf->pkt_capt_filter);
        if (fconf->pkt_capt_filter == NULL) goto err_free_fconf;
    }

    if (strlen(conf->plugin) != 0)
    {
        fconf->plugin = STRDUP(conf->plugin);
        if (fconf->plugin == NULL) goto err_free_fconf;
    }

    /* Get new conf */
    other_config = schema2tree(sizeof(conf->other_config_keys[0]),
                               sizeof(conf->other_config[0]),
                               conf->other_config_len,
                               conf->other_config_keys,
                               conf->other_config);
    check = fsm_core_check_conversion(other_config, conf->other_config_len);
    if (!check) goto err_free_fconf;

    fconf->other_config = other_config;
    session->topic = fsm_get_other_config_val(session, "mqtt_v");

    if (fsm_plugin_has_intf(session))
    {
        ret = mgr->get_br(session->conf->if_name,
                          session->bridge,
                          sizeof(session->bridge));
        if (ret)
        {
            LOGE("%s: home bridge name not found for %s",
                 __func__, session->conf->if_name);
            goto err_free_fconf;
        }
        session->p_ops->parser_ops.get_service = fsm_get_web_cat_service;
    }

    flood = fsm_get_other_config_val(session, "flood");
    if (flood != NULL)
    {
        int cmp_yes, cmp_no;

        cmp_yes = strcmp(flood, "yes");
        cmp_no = strcmp(flood, "no");
        if (cmp_yes == 0) session->flood_tap = true;
        else if (cmp_no == 0) session->flood_tap = false;
        else
        {
            LOGD("%s: session %s: value %s invalid for key flood",
                 __func__, conf->handler, flood);
            session->flood_tap = false;
        }
    }

    ret = fsm_get_web_cat_service(session);
    if (!ret)
    {
        LOGE("%s: web cataegorization settings for %s failed",
             __func__, session->name);
        goto err_free_fconf;
    }

    policy_table = fsm_get_other_config_val(session, "policy_table");
    fsm_update_client_table(session, policy_table);

    fsm_process_provider(session);
    fsm_set_tx_intf(session);
    fsm_set_dpi_health_stats_cfg(session);

    ret = fsm_is_dpi(session);
    if (ret)
    {
        ret = fsm_update_dpi_context(session);
        if (!ret) goto err_free_fconf;
    }

    ret = fsm_is_dpi_client(session);
    if (ret)
    {
        ret = fsm_update_dpi_plugin_client(session);
        if (!ret) goto err_free_fconf;
    }

    ret = mgr->update_session_tap(session);
    if (!ret) goto err_free_fconf;

    return true;

err_free_fconf:
    fsm_free_session_conf(fconf);
    session->conf =  NULL;
    session->topic = NULL;

    return false;
}


/**
 * @brief free the ovsdb configuration of the session
 *
 * Frees the fsm_session ovsdb conf settings
 * @param session the fsm session to update
 */
void
fsm_free_session_conf(struct fsm_session_conf *conf)
{
    if (conf == NULL) return;

    FREE(conf->handler);
    FREE(conf->if_name);
    FREE(conf->pkt_capt_filter);
    FREE(conf->plugin);
    free_str_tree(conf->other_config);
    FREE(conf);
}


/**
 * @brief parse the session's other config map to find the
 * plugin dso
 *
 * @param session the session to parse
 */
bool
fsm_parse_dso(struct fsm_session *session)
{
    char *plugin;
    char dso[256] = { 0 };

    session->dso = NULL;

    if (session->conf && session->conf->plugin)
    {
        plugin = session->conf->plugin;
        LOGD("%s: plugin: %s", __func__, plugin);
        if (*plugin == '/')
        {
            session->dso = STRDUP(plugin);
        }
        else
        {
            snprintf(dso, sizeof(dso), CONFIG_INSTALL_PREFIX"/%s", plugin);
            session->dso = STRDUP(dso);
        }
    }
    else
    {
        LOGD("%s: plugin: No explicit plugin DSO. Inferring from name: %s",
             __func__, session->name);
        if (session->name)
        {
            snprintf(dso, sizeof(dso), CONFIG_INSTALL_PREFIX"/lib/libfsm_%s.so", session->name);
            session->dso = STRDUP(dso);
        }
    }

    LOGD("%s: session %s set dso path to %s", __func__,
         session->name, session->dso != NULL ? session->dso : "None");

    return (session->dso != NULL ? true : false);
}


#if defined(CONFIG_FSM_NO_DSO)

static struct plugin_init_table plugin_init_table[] =
{
#if defined(CONFIG_LIB_LEGACY_FSM_HTTP_PARSER)
    {
        .handler = "http",
        .init = http_plugin_init,
    },
#endif
#if defined(CONFIG_LIB_LEGACY_FSM_DNS_PARSER)
    {
        .handler = "dns",
        .init = dns_plugin_init,
    },
#endif
#if defined(CONFIG_LIB_LEGACY_FSM_MDNS_PARSER)
    {
        .handler = "mdns",
        .init = mdns_plugin_init,
    },
#endif
#if defined(CONFIG_LIB_LEGACY_FSM_UPNP_PARSER)
    {
        .handler = "upnp",
        .init = upnp_plugin_init,
    },
#endif
#if defined(CONFIG_LIB_LEGACY_FSM_NDP_PARSER)
    {
        .handler = "ndp",
        .init = ndp_plugin_init,
    },
#endif
    {
        .handler = "gatekeeper",
        .init = gatekeeper_plugin_init,
    },
    {
        .handler = "walleye_dpi",
        .init = walleye_dpi_plugin_init,
    },
    {
        .handler = "ipthreat_dpi",
        .init = ipthreat_dpi_plugin_init,
    },
    {
        .handler = "dpi_client",
        .init = fsm_dpi_client_init,
    },
    {
        .handler = "dpi_dns",
        .init = dpi_dns_plugin_init,
    },
    {
        .handler = "dpi_adt_upnp",
        .init = dpi_adt_upnp_plugin_init,
    },
    {
        .handler = "dpi_adt",
        .init = dpi_adt_plugin_init,
    },
    {
        .handler = "dpi_sni",
        .init = dpi_sni_plugin_init,
    },
    {
        .handler = "dpi_app",
        .init = dpi_sni_plugin_init,
    },
    {
        .handler = "dpi_ndp",
        .init = dpi_ndp_plugin_init,
    },
    {
        .handler = "dpi_mdns_responder",
        .init = dpi_mdns_responder_plugin_init,
    },
    {
        .handler = "dpi_dhcp_relay",
        .init = dpi_dhcp_relay_plugin_init,
    },
    {
        .handler = "wc_null",
        .init = fsm_wc_null_plugin_init,
    },
    {
        .handler = "we_dpi",
        .init = we_dpi_plugin_init,
    }
};

static bool
fsm_match_init(struct fsm_session *session)
{
    char *prefix;
    size_t i;
    int ret;


    if (session == NULL) return false;
    if (session->name == NULL) return false;

    for (i = 0; i < ARRAY_SIZE(plugin_init_table); i++)
    {
        prefix = plugin_init_table[i].handler;
        ret = strncmp(session->name, prefix, strlen(prefix));
        if (ret != 0) continue;

        ret = plugin_init_table[i].init(session);
        if (ret != 0) return false;

        /* Wrap up initialization now that the plugin itself is fully initialized */
        ret = fsm_wrap_init_plugin(session);
        return ret;
    }

    return false;
}
#else
static bool
fsm_match_init(struct fsm_session *session)
{
    return false;
}
#endif

/**
 * @brief initialize a plugin
 *
 * @param the session to initialize
 */
bool
fsm_init_plugin(struct fsm_session *session)
{
    int (*init)(struct fsm_session *session);
    char *dso_init;
    char init_fn[256];
    char *error;
    bool ret;
    int rc;

    if (session->type == FSM_DPI_DISPATCH) return true;

    if (kconfig_enabled(CONFIG_FSM_NO_DSO))
    {
        return fsm_match_init(session);
    }

    dlerror();
    session->handle = dlopen(session->dso, RTLD_NOW);
    if (session->handle == NULL)
    {
        LOGE("%s: dlopen %s failed: %s", __func__, session->dso, dlerror());
        return false;
    }
    dlerror();

    LOGI("%s: session name: %s, dso %s", __func__, session->name, session->dso);
    dso_init = fsm_get_other_config_val(session, "dso_init");
    if (dso_init == NULL)
    {
        memset(init_fn, 0, sizeof(init_fn));
        snprintf(init_fn, sizeof(init_fn), "%s_plugin_init", session->name);
        dso_init = init_fn;
    }

    if (dso_init == NULL) return false;

    *(int **)(&init) = dlsym(session->handle, dso_init);
    error = dlerror();
    if (error != NULL) {
        LOGE("%s: could not get init symbol %s: %s",
             __func__, dso_init, error);
        dlclose(session->handle);
        return false;
    }
    rc = init(session);
    if (rc != 0) return false;

    /* Wrap up initialization now that the plugin itself is fully initialized */
    ret = fsm_wrap_init_plugin(session);

    return ret;
}

#define FSM_QM_BACKOFF_INTERVAL 20

/**
 * @brief send data to QM provided no prior connect error.
 *
 * @param compression flag
 * @param topic the mqtt topic
 * @param data to send
 * @param data_size data length
 * @return true if the data was successfully sent to QM, false otherwise
 *
 * If connecting to QM fails with the specific connect error,
 * do not attempt to reconnnect for a while.
 */
static bool
fsm_send_to_qm(qm_compress_t compress, char *topic, void *data, int data_size)
{
    struct fsm_mgr *mgr;
    qm_response_t res;
    bool backoff;
    time_t now;
    bool ret;

    mgr = fsm_get_mgr();
    if (mgr->qm_backoff != 0)
    {
        now = time(NULL);
        backoff = ((now - mgr->qm_backoff) < FSM_QM_BACKOFF_INTERVAL);
        if (backoff) LOGD("%s: in back off since %ld seconds", __func__,
                          (long int)(now - mgr->qm_backoff));
        if (backoff) return false;

        /* Reflect that we are out of back off */
        LOGD("%s: out of back off since %ld seconds", __func__,
             (long int)(now - mgr->qm_backoff) - FSM_QM_BACKOFF_INTERVAL);
        mgr->qm_backoff = 0;
    }

    ret = qm_conn_send_direct(compress, topic, data, data_size, &res);
    if (ret) return true;

    LOGE("%s: error sending mqtt with topic %s: response: %u, error: %u",
         __func__, topic, res.response, res.error);

    if (res.error != QM_ERROR_CONNECT) return false;

    now = time(NULL);
    mgr->qm_backoff = now;

    return false;
}


/**
 * @brief send a json report over mqtt
 *
 * Emits and frees a json report
 * @param session the fsm session emitting the report
 * @param report the report to emit
 */
void
fsm_send_report(struct fsm_session *session, char *report)
{
    bool ret;

    LOGT("%s: msg len: %zu, msg: %s\n, topic: %s",
         __func__, report ? strlen(report) : 0,
         report ? report : "None", session->topic ? session->topic : "None");

    if (report == NULL) return;
    if (session->topic == NULL) goto free_report;

    ret = fsm_send_to_qm(QM_REQ_COMPRESS_DISABLE, session->topic,
                         report, strlen(report));
    if (ret) session->report_count++;

free_report:
    json_free(report);
    return;
}


/**
 * @brief send a protobuf report over mqtt
 *
 * Emits a protobuf report. Does not free the protobuf.
 * @param session the fsm session emitting the report
 * @param report the report to emit
 */
void
fsm_send_pb_report(struct fsm_session *session, char *topic,
                   void *pb_report, size_t pb_len)
{
    bool ret;

    LOGT("%s: msg len: %zu, topic: %s",
         __func__, pb_len, topic ? topic: "None");

    if (pb_report == NULL) return;
    if (topic == NULL) return;

    ret = fsm_send_to_qm(QM_REQ_COMPRESS_IF_CFG, topic,pb_report, pb_len);
    if (ret) session->report_count++;
}


/**
 * @brief retrieve the network ID associated to the device id
 *
 * @param session the fsm session requesting the network ID
 * @param the client device's mac address
 */
static char *
fsm_session_get_network_id(struct fsm_session *session, os_macaddr_t *mac)
{
    if (session == NULL) return NULL;

    return fsm_get_network_id(mac);
}


void
fsm_set_session_ops(struct fsm_session *session)
{

    if (session == NULL) return;

    session->ops.send_report = fsm_send_report;
    session->ops.send_pb_report = fsm_send_pb_report;
    session->ops.get_config = fsm_get_other_config_val;
    session->ops.state_cb = fsm_set_object_state;
    session->ops.latest_obj_cb = fsm_oms_get_highest_version;
    session->ops.last_active_obj_cb = fsm_oms_get_last_active_version;
    session->ops.best_obj_cb = fsm_oms_get_best_version;
    session->ops.update_client = fsm_update_client;
    session->ops.get_network_id = fsm_session_get_network_id;
    session->ops.monitor_object = fsm_register_object_to_monitor;
    session->ops.unmonitor_object = fsm_unregister_object_to_monitor;
}


/**
 * @brief allocates a FSM session
 *
 * @param conf pointer to a ovsdb record
 */
struct fsm_session *
fsm_alloc_session(struct schema_Flow_Service_Manager_Config *conf)
{
    struct fsm_session *session;
    union fsm_plugin_ops *plugin_ops;
    struct fsm_mgr *mgr;
    bool ret;

    mgr = fsm_get_mgr();

    session = CALLOC(1, sizeof(struct fsm_session));
    if (session == NULL) return NULL;

    session->name = STRDUP(conf->handler);
    if (session->name == NULL) goto err_free_session;

    session->type = fsm_service_type(conf);
    if (session->type == FSM_UNKNOWN_SERVICE) goto err_free_name;

    plugin_ops = CALLOC(1, sizeof(*plugin_ops));
    if (plugin_ops == NULL) goto err_free_name;

    session->p_ops = plugin_ops;
    session->mqtt_headers = mgr->mqtt_headers;
    session->location_id = mgr->location_id;
    session->node_id = mgr->node_id;
    session->loop = mgr->loop;
    session->flood_tap = false;
    session->keep_config = FSM_KEEP_CONFIG_NOT_SET;

    fsm_set_session_ops(session);

    ret = fsm_session_update(session, conf);
    if (!ret) goto err_free_plugin_ops;

    ret = fsm_parse_dso(session);
    if (!ret) goto err_free_dso;

    return session;

err_free_dso:
    FREE(session->dso);

err_free_plugin_ops:
    FREE(plugin_ops);

err_free_name:
    FREE(session->name);

err_free_session:
    FREE(session);

    return NULL;
}


/**
 * @brief frees a FSM session
 *
 * @param session a pointer to a FSM session to free
 */
void
fsm_free_session(struct fsm_session *session)
{
    bool ret;

    if (session == NULL) return;

    /* free fsm tap resources */
    fsm_free_tap_resources(session);

    /* Call the session exit routine */
    if (session->ops.exit != NULL) session->ops.exit(session);

    /* close forward_ctx socket*/
    if (session->forward_ctx.initialized) close(session->forward_ctx.sock_fd);

    /* Free optional dpi context */
    ret = fsm_is_dpi(session);
    if (ret) fsm_free_dpi_context(session);

    ret = fsm_is_dpi_client(session);
    if (ret) fsm_free_dpi_plugin_client(session);

    /* Close the dynamic library handler */
    if (session->handle != NULL) dlclose(session->handle);

    /* Free the config settings */
    fsm_free_session_conf(session->conf);

    /* Free the dso path string */
    FREE(session->dso);

    /* Free the plugin ops */
    FREE(session->p_ops);

    /* Free the session name */
    FREE(session->name);

    /* Free the session provider */
    FREE(session->provider);

    /* Finally free the session */
    FREE(session);
}


/**
 * @brief get session name
 *
 * return then session name
 */
char *
fsm_get_session_name(struct fsm_policy_client *client)
{
    struct fsm_session *session;

    if (client == NULL) return NULL;

    session = client->session;
    if (session == NULL) return NULL;

    return session->name;
}


/**
 * @brief add a fsm session
 *
 * @param conf the ovsdb Flow_Service_Manager_Config entry
 */
void
fsm_add_session(struct schema_Flow_Service_Manager_Config *conf)
{
    struct fsm_policy_client *client;
    struct fsm_session *session;
    struct fsm_mgr *mgr;
    ds_tree_t *sessions;
    bool ret;

    fsm_mem_monitor_plugin_init_enter(conf->handler);
    mgr = fsm_get_mgr();
    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);

    if (session != NULL)
    {
        if (session->type == FSM_WEB_CAT) fsm_modify_session(conf);
        return;
    }

    /* Allocate a new session, insert it to the sessions tree */
    session = fsm_alloc_session(conf);
    if (session == NULL)
    {
        LOGE("Could not allocate session for handler %s",
             conf->handler);
        return;
    }
    ds_tree_insert(sessions, session, session->name);

    ret = mgr->init_plugin(session);
    if (!ret)
    {
        LOGE("%s: plugin handler %s initialization failed",
             __func__, session->name);
        goto err_free_session;

    }

    if (session->type == FSM_WEB_CAT)
    {
        fsm_web_cat_service_update(session, FSM_SERVICE_ADD);
    }

    client = &session->policy_client;
    client->session = session;
    client->update_client = fsm_update_client;
    client->session_name = fsm_get_session_name;
    fsm_policy_register_client(&session->policy_client);
    fsm_notify_identical_sessions(session, true);
    fsm_notify_dispatcher_tap_type(session);

    fsm_walk_sessions_tree();
    fsm_mem_monitor_plugin_init_exit(conf->handler);

    return;

err_free_session:
    ds_tree_remove(sessions, session);
    fsm_free_session(session);

    return;
}


/**
 * @brief delete a fsm session
 *
 * @param conf the ovsdb Flow_Service_Manager_Config entry
 */
void
fsm_delete_session(struct schema_Flow_Service_Manager_Config *conf)
{
    struct fsm_session *session;
    ds_tree_t *sessions;

    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);

    if (session == NULL) return;

    /* Notify parser plugins of the web categorization plugin removal */
    if (session->type == FSM_WEB_CAT)
    {
        fsm_web_cat_service_update(session, FSM_SERVICE_DELETE);
    }

    fsm_policy_deregister_client(&session->policy_client);
    ds_tree_remove(sessions, session);
    fsm_notify_identical_sessions(session, false);

    if (kconfig_enabled(CONFIG_FSM_MAP_LEGACY_PLUGINS))
    {
        fsm_unmap_plugin(session);
    }

    fsm_free_session(session);
    fsm_walk_sessions_tree();
}


/**
 * @brief modify a fsm session
 *
 * @param conf the ovsdb Flow_Service_Manager_Config entry
 */
void
fsm_modify_session(struct schema_Flow_Service_Manager_Config *conf)
{
    struct fsm_session *session;
    ds_tree_t *sessions;

    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);

    if (session == NULL) return;

    fsm_session_update(session, conf);
    if (session->ops.update != NULL) session->ops.update(session);

    fsm_notify_dispatcher_tap_type(session);
}


/**
 * @brief registered callback for OVSDB Flow_Service_Manager_Config events
 */
static void
callback_Flow_Service_Manager_Config(ovsdb_update_monitor_t *mon,
                                     struct schema_Flow_Service_Manager_Config *old_rec,
                                     struct schema_Flow_Service_Manager_Config *conf)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW) fsm_add_session(conf);
    if (mon->mon_type == OVSDB_UPDATE_DEL) fsm_delete_session(old_rec);
    if (mon->mon_type == OVSDB_UPDATE_MODIFY) fsm_modify_session(conf);
}


/**
 * @brief gather mqtt records from AWLAN_Node's mqtt headers table
 *
 * Records the mqtt_headers (locationId, nodeId) in the fsm manager
 * Update existing fsm sessions to point to the manager's records.
 * @param awlan AWLAN_Node record
 */
void
fsm_get_awlan_headers(struct schema_AWLAN_Node *awlan)
{
    char *location = "locationId";
    struct fsm_session *session;
    struct str_pair *pair;
    char *node = "nodeId";
    struct fsm_mgr *mgr;
    ds_tree_t *sessions;
    size_t key_size;
    size_t val_size;
    size_t nelems;

    /* Get the manager */
    mgr = fsm_get_mgr();

    /* Free previous headers if any */
    free_str_tree(mgr->mqtt_headers);

    /* Get AWLAN_Node's mqtt_headers element size */
    key_size = sizeof(awlan->mqtt_headers_keys[0]);
    val_size = sizeof(awlan->mqtt_headers[0]);

    /* Get AWLAN_Node's number of elements */
    nelems = awlan->mqtt_headers_len;

    mgr->mqtt_headers = schema2tree(key_size, val_size, nelems,
                                    awlan->mqtt_headers_keys,
                                    awlan->mqtt_headers);
    if (mgr->mqtt_headers == NULL)
    {
        mgr->location_id = NULL;
        mgr->node_id = NULL;
        goto update_sessions;
    }

    /* Check the presence of locationId in the mqtt headers */
    pair = ds_tree_find(mgr->mqtt_headers, location);
    if (pair != NULL) mgr->location_id = pair->value;
    else
    {
        free_str_tree(mgr->mqtt_headers);
        mgr->mqtt_headers = NULL;
        mgr->location_id = NULL;
        mgr->node_id = NULL;
        goto update_sessions;
    }

    /* Check the presence of nodeId in the mqtt headers */
    pair = ds_tree_find(mgr->mqtt_headers, node);
    if (pair != NULL) mgr->node_id = pair->value;
    else
    {
        free_str_tree(mgr->mqtt_headers);
        mgr->mqtt_headers = NULL;
        mgr->location_id = NULL;
        mgr->node_id = NULL;
    }

update_sessions:
    /* Lookup sessions, update their header info */
    sessions = fsm_get_sessions();
    session = ds_tree_head(sessions);

    while (session != NULL) {
        session->mqtt_headers = mgr->mqtt_headers;
        session->location_id  = mgr->location_id;
        session->node_id  = mgr->node_id;
        session = ds_tree_next(sessions, session);
    }
}


/**
 * @brief delete recorded mqtt headers
 */
void
fsm_rm_awlan_headers(void)
{
    struct fsm_session *session;
    struct fsm_mgr *mgr;
    ds_tree_t *sessions;

    /* Get the manager */
    mgr = fsm_get_mgr();

    /* Free previous headers if any */
    free_str_tree(mgr->mqtt_headers);
    mgr->mqtt_headers = NULL;

    /* Lookup sessions, update their header info */
    sessions = fsm_get_sessions();
    session = ds_tree_head(sessions);
    while (session != NULL) {
        session->mqtt_headers = NULL;
        session = ds_tree_next(sessions, session);
    }
}


/**
 * @brief registered callback for AWLAN_Node events
 */
static void
callback_AWLAN_Node(ovsdb_update_monitor_t *mon,
                    struct schema_AWLAN_Node *old_rec,
                    struct schema_AWLAN_Node *awlan)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW) fsm_get_awlan_headers(awlan);
    if (mon->mon_type == OVSDB_UPDATE_DEL) fsm_rm_awlan_headers();
    if (mon->mon_type == OVSDB_UPDATE_MODIFY) fsm_get_awlan_headers(awlan);
}

/**
 * @brief Upserts the Node_State ovsdb table entry for fsm's max mem limit
 *
 * Advertizes in Node_State the max amount of memory FSM is allowed to use
 * @param module the Node_State module name
 * @param key the Node_State key
 * @param key the Node_State value
 */
void
fsm_set_node_state(const char *module, const char *key, const char *value)
{
    struct schema_Node_State node_state;
    json_t *where;
    json_t *cond;

    where = json_array();

    cond = ovsdb_tran_cond_single("module", OFUNC_EQ, (char *)module);
    json_array_append_new(where, cond);

    MEMZERO(node_state);
    SCHEMA_SET_STR(node_state.module, module);
    SCHEMA_SET_STR(node_state.key, key);
    SCHEMA_SET_STR(node_state.value, value);
    ovsdb_table_upsert_where(&table_Node_State, where, &node_state, false);

    return;
}


/**
 * @brief processes the addition of an entry in Node_Config
 */
void
fsm_get_node_config(struct schema_Node_Config *node_cfg)
{
    struct fsm_mgr *mgr;
    char str_value[32];
    char *module;
    long value;
    char *key;

    int rc;

    module = node_cfg->module;
    rc = strcmp("fsm", module);
    if (rc != 0) return;

    key = node_cfg->key;
    rc = strcmp("clear_gatekeeper_cache", key);
    if (rc == 0)
    {
        clear_gatekeeper_cache();
        dns_cache_cleanup();
        return;
    }

    rc = strcmp("max_mem_percent", key);
    if (rc != 0) return;

    errno = 0;
    value = strtol(node_cfg->value, NULL, 10);
    if (errno != 0)
    {
        LOGE("%s: error reading value %s: %s", __func__,
             node_cfg->value, strerror(errno));
        return;
    }

    if (value < 0) return;
    if (value > 100) return;

    /* Get the manager */
    mgr = fsm_get_mgr();

    if (mgr->sysinfo.totalram == 0) return;

    mgr->max_mem = mgr->sysinfo.totalram * mgr->sysinfo.mem_unit;
    mgr->max_mem = (mgr->max_mem * value) / 100;
    mgr->max_mem /= 1000; /* kB */

    LOGI("%s: set fsm max mem usage to %" PRIu64 " kB", __func__,
         mgr->max_mem);
    snprintf(str_value, sizeof(str_value), "%" PRIu64 " kB", mgr->max_mem);
    fsm_set_node_state(FSM_NODE_MODULE, FSM_NODE_STATE_MEM_KEY, str_value);
}


/**
 * @brief processes the removal of an entry in Node_Config
 */
void
fsm_rm_node_config(struct schema_Node_Config *old_rec)
{
    struct fsm_mgr *mgr;
    char str_value[32];
    char *module;
    int rc;

    module = old_rec->module;
    rc = strcmp("fsm", module);
    if (rc != 0) return;

    /* Reset the memory */
    fsm_set_max_mem();

    mgr = fsm_get_mgr();

    snprintf(str_value, sizeof(str_value), "%" PRIu64 " kB", mgr->max_mem);
    fsm_set_node_state(FSM_NODE_MODULE, FSM_NODE_STATE_MEM_KEY, str_value);
}


/**
 * @brief processes the removal of an entry in Node_Config
 */
void
fsm_update_node_config(struct schema_Node_Config *node_cfg)
{
    fsm_get_node_config(node_cfg);
}


/**
 * @brief registered callback for Node_Config events
 */
static void
callback_Node_Config(ovsdb_update_monitor_t *mon,
                    struct schema_Node_Config *old_rec,
                    struct schema_Node_Config *node_cfg)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        fsm_get_node_config(node_cfg);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        fsm_rm_node_config(old_rec);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        fsm_update_node_config(node_cfg);
    }
}


static void
fsm_enable_ct_stats_comms(struct schema_FCM_Collector_Config *node_cfg)
{
    struct fsm_mgr *mgr;
    bool ret;
    int cmp;

    mgr = fsm_get_mgr();

    /* Bail if the osbus connection to FCM is already established */
    if (mgr->osbus_flags & FSM_OSBUS_FCM) return;

    /* Check if the fcm feature is of interest */
    cmp = strcmp(node_cfg->name, "ct_stats");
    if (cmp != 0) return;

    /* Initialize the FSM osbus end point */
    ret = fsm_ipc_init_client();
    if (!ret)
    {
        LOGD("%s: could not initiate client", __func__);
        return;
    }

    mgr->osbus_flags |= FSM_OSBUS_FCM;

    return;
}


static void
fsm_disable_ct_stats_comms(struct schema_FCM_Collector_Config *old_rec)
{
    struct fsm_mgr *mgr;
    int cmp;

    mgr = fsm_get_mgr();

    /* Bail if the osbus connection to FCM is not established */
    if (!(mgr->osbus_flags & FSM_OSBUS_FCM)) return;

    /* Check if the fcm feature is of interest */
    cmp = strcmp(old_rec->name, "ct_stats");
    if (cmp != 0) return;

    /* Close the FSM osbus end point */
    fsm_ipc_terminate_client();

    mgr->osbus_flags &= ~FSM_OSBUS_FCM;

    return;
}


/**
 * @brief registered callback for Node_Config events
 */
static void
callback_FCM_Collector_Config(ovsdb_update_monitor_t *mon,
                              struct schema_FCM_Collector_Config *old_rec,
                              struct schema_FCM_Collector_Config *node_cfg)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        fsm_enable_ct_stats_comms(node_cfg);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        fsm_disable_ct_stats_comms(old_rec);
    }
}

struct fsm_session;


/**
 * @brief register ovsdb callback events
 */
int
fsm_ovsdb_init(void)
{
    struct fsm_mgr *mgr;
    char str_value[32];

    LOGI("Initializing FSM tables");
    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
    OVSDB_TABLE_INIT_NO_KEY(Flow_Service_Manager_Config);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Local_Tag);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag_Group);
    OVSDB_TABLE_INIT_NO_KEY(Node_Config);
    OVSDB_TABLE_INIT_NO_KEY(Node_State);
    OVSDB_TABLE_INIT_NO_KEY(FCM_Collector_Config);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(AWLAN_Node, false);
    OVSDB_TABLE_MONITOR(Flow_Service_Manager_Config, false);
    om_standard_callback_openflow_tag(&table_Openflow_Tag);
    om_standard_callback_openflow_local_tag(&table_Openflow_Local_Tag);
    om_standard_callback_openflow_tag_group(&table_Openflow_Tag_Group);
    OVSDB_TABLE_MONITOR(Node_Config, false);
    OVSDB_TABLE_MONITOR(FCM_Collector_Config, false);

    // Initialize the plugin loader routine
    mgr = fsm_get_mgr();
    mgr->init_plugin = fsm_init_plugin;
    mgr->update_session_tap = fsm_update_session_tap;
    fsm_policy_init();
    network_zone_init();

    // Advertize default memory limit usage
    snprintf(str_value, sizeof(str_value), "%" PRIu64 " kB", mgr->max_mem);
    fsm_set_node_state(FSM_NODE_MODULE, FSM_NODE_STATE_MEM_KEY, str_value);

    if (!kconfig_enabled(CONFIG_FSM_TAP_INTF))
    {
        mgr->get_br = fsm_get_no_bridge;
    }
    else
    {
        mgr->get_br = get_home_bridge;
    }

    return 0;
}
