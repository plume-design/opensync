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

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#include "fsm_internal.h"
#include "fsm_tap_socket.h"
#include "log.h"
#include "nf_utils.h"
#include "policy_tags.h"
#include "fsm_dpi_utils.h"
#include "network_zone.h"
#include "kconfig.h"

static const struct fsm_tap_type
{
    char *tap_str_type;
    int tap_type;
} tap_map[] =
{
    {
        .tap_str_type = "fsm_tap_pcap",
        .tap_type = FSM_TAP_PCAP,
    },
    {
        .tap_str_type = "fsm_tap_nfqueues",
        .tap_type = FSM_TAP_NFQ,
    },
    {
        .tap_str_type = "fsm_tap_raw",
        .tap_type = FSM_TAP_RAW,
    },
    {
        .tap_str_type = "fsm_tap_socket",
        .tap_type = FSM_TAP_SOCKET,
    }
};


char *
fsm_get_network_id(os_macaddr_t *device)
{
    char *nz;

    nz = network_zone_get_zone(device);
    if (nz == NULL) nz = "Unknown";

    LOGT("%s: network zone for " PRI_os_macaddr_lower_t " is %s",
         __func__, FMT_os_macaddr_pt(device), nz);

    return nz;
}


/**
 * @brief sets the session type based on the conf_type
 *
 * @param conf_type string value pulled from ovsdb
 *        The string contains a comma separated list of the taps to be open.
 * @return an integer representing the type of tap we are handling
 *         There can be more than one service (i.e., more than one bit can be set)
 */
uint32_t
fsm_tap_type_from_str(char *conf_type)
{
    char *tmp_conf_type;
    char *base_ptr;
    size_t nelems;
    char *token;
    int retval;
    size_t i;
    int cmp;

    if (conf_type == NULL) return FSM_TAP_PCAP;

    /* If this stays to 0, then we should default to PCAP */
    retval = 0;

    /* We cannot tokenize conf_type directly as it is a reference. */
    tmp_conf_type = STRDUP(conf_type);

    nelems = ARRAY_SIZE(tap_map);

    base_ptr = tmp_conf_type;
    while ((token = strtok_r(base_ptr, ",", &base_ptr)) != NULL)
    {
        /* Walk the known types */
        for (i = 0; i < nelems; i++)
        {
            cmp = strcmp(token, tap_map[i].tap_str_type);
            if (cmp == 0)
            {
                retval |= tap_map[i].tap_type;
                break;
            }
        }
    }

    FREE(tmp_conf_type);

    /* Default to pcap if nothing was set*/
    return (retval == 0) ? FSM_TAP_PCAP : retval;
}

/**
 * @brief sets the session type based on the ovsdb values
 *
 * @param the ovsdb configuration
 * @return an integer representing the type of tap we are handling.
 *         There can be more than one service (more than one bit on).
 */
uint32_t
fsm_tap_type(struct fsm_session *session)
{
    char *conf_type;

    conf_type = session->ops.get_config(session, "tap_type");

    return fsm_tap_type_from_str(conf_type);
}

/**
 * @brief returns the tapping mode for a session
 *
 * @param session the fsm session to probe
 * @return the tapping mode
 */
uint32_t
fsm_session_tap_mode(struct fsm_session *session)
{
    int tap_type;
    bool rc;

    rc = fsm_plugin_has_intf(session);
    if (!rc) return FSM_TAP_NONE;

    tap_type = fsm_tap_type(session);

    LOGI("%s: session %s: tap type: %d", __func__, session->name, tap_type);

    return tap_type;
}

static bool
fsm_update_open_taps(uint32_t taps_to_open, struct fsm_session *session)
{
    bool rc = false;

    if (taps_to_open == FSM_TAP_NONE) rc = true;

    if (taps_to_open & FSM_TAP_PCAP) rc |= fsm_pcap_tap_update(session);

    if (taps_to_open & FSM_TAP_NFQ) rc |= fsm_nfq_tap_update(session);

    if (taps_to_open & FSM_TAP_RAW) rc |= fsm_raw_tap_update(session);

    if (taps_to_open & FSM_TAP_SOCKET) rc |= fsm_socket_tap_update(session);

    return rc;
}

static void
fsm_update_close_taps(uint32_t taps_to_close, struct fsm_session *session)
{
    struct fsm_pcaps *pcaps;

    if (taps_to_close & FSM_TAP_PCAP)
    {
        /* Free pcap resources */
        pcaps = session->pcaps;
        if (pcaps != NULL) fsm_pcap_close(session);
    }

    if (taps_to_close & FSM_TAP_NFQ)
    {
        /* Free nfq resources */
        nf_queue_exit();
    }

    if (taps_to_close & FSM_TAP_RAW)
    {
        /* Free raw socket resources */
        return;
    }

    if (taps_to_close & FSM_TAP_SOCKET)
    {
        fsm_socket_tap_close(session);
    }
}


static int
set_dpi_mark(struct net_header_parser *net_hdr,
             struct dpi_mark_policy *mark_policy)
{
    int ret;

    if (net_hdr == NULL) return -1;

    switch (net_hdr->source)
    {
        case PKT_SOURCE_PCAP:
            ret = fsm_set_dpi_mark(net_hdr, mark_policy);
            break;

        case PKT_SOURCE_NFQ:
            ret = nf_queue_set_dpi_mark(net_hdr, mark_policy);
            break;

        default:
            ret = -1;
    }

    return ret;
}


/**
 * @brief Initializes the tap context for the given session
 *
 * @param session the fsm session to probe
 * @return true if success, false otherwise
 */
bool
fsm_update_session_tap(struct fsm_session *session)
{
    uint32_t before_tap_type;
    uint32_t after_tap_type;
    uint32_t taps_to_close;
    uint32_t taps_to_open;

    /* Get the current tap type */
    before_tap_type = session->tap_type;

    /* Get the configured tap type */
    after_tap_type = fsm_session_tap_mode(session);

    /* what to close and what to open */
    taps_to_close = before_tap_type & ~after_tap_type;
    taps_to_open = ~before_tap_type & after_tap_type;

    /* Update the session tap type */
    session->tap_type = after_tap_type;

    session->set_dpi_mark = set_dpi_mark;

    fsm_update_close_taps(taps_to_close, session);

    return fsm_update_open_taps(taps_to_open, session);
}

/**
 * @brief frees fsm tap resources pcap, nfqueues and raw socket
 *
 * @param session the fsm session involved
 */
void
fsm_free_tap_resources(struct fsm_session *session)
{
    /* Taps to be closed are the taps currently open */
    fsm_update_close_taps(session->tap_type, session);
}

/**
 * @brief wrap a dpi plugin initialization
 *
 * @param session the session getting initialized
 * @return true if successful, false otherwise
 */
bool
fsm_wrap_init_dpi_plugin(struct fsm_session *session)
{
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    union fsm_dpi_context *dpi_context;
    struct fsm_dpi_plugin *dpi_plugin;
    ds_tree_t *tree;

    dpi_context = session->dpi;
    if (dpi_context == NULL) return false;

    dpi_plugin = &dpi_context->plugin;
    dpi_plugin_ops = &session->p_ops->dpi_plugin_ops;
    if (dpi_plugin_ops->flow_attr_cmp == NULL) return true;

    if (!dpi_plugin->clients_init)
    {
        tree = &dpi_plugin->dpi_clients;

        ds_tree_init(tree, dpi_plugin_ops->flow_attr_cmp,
                     struct dpi_client, next);
        dpi_plugin->clients_init = true;
    }

    fsm_dpi_register_clients(session);
    return true;
}


static bool
fsm_wrap_init_dpi_client_plugin(struct fsm_session *session)
{
    struct fsm_session *dpi_plugin;
    char *dpi_plugin_handler;
    ds_tree_t *sessions;

    /* Look up the dpi plugin handler in the other_config settings */
    dpi_plugin_handler = session->ops.get_config(session, "dpi_plugin");

    /* Bail if not provided */
    if (dpi_plugin_handler == NULL) return false;

    /* Look up the corresponding session */
    sessions = fsm_get_sessions();
    dpi_plugin = ds_tree_find(sessions, dpi_plugin_handler);
    /*
     * The dpi plugin session might not yet be configured
     * Bail now, the registration of the client plugin will resume
     * once the dpi plugin is configured.
     */
    if (dpi_plugin == NULL) return true;
    fsm_dpi_client_process_attributes(session, dpi_plugin);

    return true;
}


#if defined(CONFIG_FSM_MAP_LEGACY_PLUGINS)
static struct plugin_map_table plugin_map_table[] =
{
    {
        .handler = "http",
        .ip_protocol = IPPROTO_TCP,
        .port = 80,
    },
    {
        .handler = "mdns",
        .ip_protocol = IPPROTO_UDP,
        .port = 5353,
    },
    {
        .handler = "upnp",
        .ip_protocol = IPPROTO_UDP,
        .port = 1900,
    },
    {
        .handler = "dns",
        .ip_protocol = IPPROTO_UDP,
        .port = 53,
    },
};


static void
fsm_map_plugin(struct fsm_session *session)
{
    char *prefix;
    size_t i;
    int ret;

    if (session == NULL) return;
    if (session->name == NULL) return;
    if (session->type != FSM_PARSER) return;

    for (i = 0; i < ARRAY_SIZE(plugin_map_table); i++)
    {
        prefix = plugin_map_table[i].handler;
        ret = strncmp(session->name, prefix, strlen(prefix));
        if (ret != 0) continue;

        plugin_map_table[i].session = session;
        return;
    }

    return;
}


void
fsm_unmap_plugin(struct fsm_session *session)
{
    char *prefix;
    size_t i;
    int ret;

    if (session == NULL) return;
    if (session->name == NULL) return;
    if (session->type != FSM_PARSER) return;

    for (i = 0; i < ARRAY_SIZE(plugin_map_table); i++)
    {
        prefix = plugin_map_table[i].handler;
        ret = strncmp(session->name, prefix, strlen(prefix));
        if (ret != 0) continue;

        plugin_map_table[i].session = NULL;
        return;
    }

    return;
}


struct fsm_session *
fsm_map_plugin_find_session(struct net_header_parser *net_parser)
{
    struct plugin_map_table *entry;
    uint16_t ethertype;
    int ip_protocol;
    size_t i;

    if (net_parser == NULL) return NULL;

    ethertype = net_header_get_ethertype(net_parser);
    if (ethertype != ETH_P_IP && ethertype != ETH_P_IPV6) return NULL;

    for (i = 0; i < ARRAY_SIZE(plugin_map_table); i++)
    {
        entry = &plugin_map_table[i];
        ip_protocol = entry->ip_protocol;
        if (ip_protocol != net_parser->ip_protocol) continue;

        if (net_parser->ip_protocol == IPPROTO_TCP)
        {
            struct tcphdr *tcph;

            tcph = net_parser->ip_pld.tcphdr;

            if (ntohs(tcph->source) != entry->port &&
                ntohs(tcph->dest) != entry->port)
            {
                continue;
            }
        }
        else if (net_parser->ip_protocol == IPPROTO_UDP)
        {
            struct udphdr *udph;

            udph = net_parser->ip_pld.udphdr;
            if (ntohs(udph->source) != entry->port &&
                ntohs(udph->dest) != entry->port)
            {
                continue;
            }
        }
        else
        {
            continue;
        }

        LOGT("%s: dispatching to legacy handler %s", __func__, entry->handler);
        net_header_logt(net_parser);
        return entry->session;

    }

    LOGT("%s: no legacy handler found", __func__);
    return NULL;
}


#else
static void
fsm_map_plugin(struct fsm_session *session)
{
    return;
}


void
fsm_unmap_plugin(struct fsm_session *session)
{
    return;
}


struct fsm_session *
fsm_map_plugin_find_session(struct net_header_parser *net_parser)
{
    return NULL;
}

#endif

/**
 * @brief wrap plugin initialization
 *
 * @param session the session getting initialized
 * @return true if successful, false otherwise
 *
 * Completes the fsm core initialization of the plugin
 * after the plugin initialization routine was called
 */
bool
fsm_wrap_init_plugin(struct fsm_session *session)
{
    bool ret;

    switch (session->type)
    {
        case FSM_DPI_PLUGIN:
            ret = fsm_wrap_init_dpi_plugin(session);
            break;

        case FSM_DPI_PLUGIN_CLIENT:
            ret = fsm_wrap_init_dpi_client_plugin(session);
            break;

        default:
            ret = true;
    }

    if (kconfig_enabled(CONFIG_FSM_MAP_LEGACY_PLUGINS))
    {
        fsm_map_plugin(session);
    }
    return ret;
}

/**
 * @brief notify tap type to all fsm_sessions
 *
 * @param session dispatcher session
 * @param tap_type dispatcher tap_type
 */
void
notify_dispatcher_tap_type(struct fsm_session *session, uint32_t tap_type)
{
    struct fsm_session *plugin;
    ds_tree_t *sessions;

    if (session->type != FSM_DPI_DISPATCH) return;

    sessions = fsm_get_sessions();
    if (sessions == NULL) return;

    plugin = ds_tree_head(sessions);
    while (plugin != NULL)
    {
        if (plugin->ops.notify_dispatcher_tap_type != NULL)
        {
            plugin->ops.notify_dispatcher_tap_type(plugin, tap_type);
        }
        plugin = ds_tree_next(sessions, plugin);
    }
}

/**
 * @brief notify tap type to fsm session
 *
 * @param session the fsm session involved
 */
void
fsm_notify_dispatcher_tap_type(struct fsm_session *session)
{
    struct fsm_session *plugin;
    ds_tree_t *sessions;
    uint32_t tap_type;

    /* Added/updated session is dpi_dispatch */
    if (session->type == FSM_DPI_DISPATCH)
    {
        tap_type = fsm_tap_type(session);
        notify_dispatcher_tap_type(session, tap_type);
        return;
    }

    sessions = fsm_get_sessions();
    if (sessions == NULL) return;

    plugin = ds_tree_head(sessions);
    while (plugin != NULL)
    {
        if (plugin->type == FSM_DPI_DISPATCH) break;
        plugin = ds_tree_next(sessions, plugin);
    }

    /* dpi_dispatch session is not added */
    if (plugin == NULL) return;

    tap_type = fsm_tap_type(plugin);

    if (session->ops.notify_dispatcher_tap_type != NULL)
    {
        session->ops.notify_dispatcher_tap_type(session, tap_type);
    }
}

/**
 * @brief notify identical sessions
 *
 * @param session the fsm session involved
 */
void
fsm_notify_identical_sessions(struct fsm_session *session, bool enabled)
{
    enum fsm_plugin_id plugin_id;
    struct fsm_session *plugin;
    ds_tree_t *sessions;
    bool status;
    int rc;

    plugin_id = session->plugin_id;

    status = (plugin_id == FSM_DNS_PLUGIN);
    status |= (plugin_id == FSM_DPI_DNS_PLUGIN);
    if (!status) return;

    sessions = fsm_get_sessions();
    if (sessions == NULL) return;

    status = false;
    plugin = ds_tree_head(sessions);
    while (plugin != NULL)
    {
        if (plugin->plugin_id == plugin_id)
        {
            /* The same session has been inserted to fsm_sessions tree.
             * Ignore if the same session is found */
            rc = strcmp(plugin->name, session->name);
            if (rc != 0)
            {
                if (plugin->ops.notify_identical_sessions != NULL)
                {
                    plugin->ops.notify_identical_sessions(plugin, enabled);
                    status = true;
                }
            }
        }
        plugin = ds_tree_next(sessions, plugin);
    }

    if (status)
    {
        if (session->ops.notify_identical_sessions != NULL)
        {
            session->ops.notify_identical_sessions(session, enabled);
        }
    }
}
