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

#include "fsm_internal.h"
#include "policy_tags.h"
#include "log.h"
#include "nf_utils.h"

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
    }
};



/**
 * @brief sets the session type based on the ovsdb values
 *
 * @param the ovsdb configuration
 * @return an integer representing the type of service
 */
static int
fsm_tap_type(struct fsm_session *session)
{
    const struct fsm_tap_type *map;
    char *conf_type;
    size_t nelems;
    size_t i;
    int cmp;

    conf_type = session->ops.get_config(session, "tap_type");
    if (conf_type == NULL) return FSM_TAP_PCAP;

    /* Walk the known types */
    nelems = (sizeof(tap_map) / sizeof(tap_map[0]));
    map = tap_map;
    for (i = 0; i < nelems; i++)
    {
        cmp = strcmp(conf_type, map->tap_str_type);
        if (!cmp) return map->tap_type;

        map++;
    }

    /* Default to pcap */
    return FSM_TAP_PCAP;
}


/**
 * @brief returns the tapping mode for a session
 *
 * @param session the fsm session to probe
 * @return the tapping mode
 */
int
fsm_session_tap_mode(struct fsm_session *session)
{
    int tap_type;
    bool rc;

    rc = fsm_plugin_has_intf(session);
    if (!rc) return FSM_TAP_NONE;

    tap_type = fsm_tap_type(session);

    LOGI("%s: session %s: tap type: %d", __func__,
         session->name, tap_type);

    return tap_type;
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
    int orig_tap_type;
    int tap_type;
    bool rc;

    /* Get the current tap type */
    orig_tap_type = session->tap_type;

    /* Get the configured tap type */
    tap_type = fsm_session_tap_mode(session);

    if (tap_type != orig_tap_type)
    {
        /* The tap type has changed, free existing tap resources */
        fsm_free_tap_resources(session);
    }

    /* Update the session tap type */
    session->tap_type = tap_type;

    switch (tap_type)
    {
        case FSM_TAP_NONE:
            rc = true;
            break;

        case FSM_TAP_PCAP:
            rc = fsm_pcap_update(session);
            break;

        case FSM_TAP_NFQ:
            rc = fsm_nfq_tap_update(session);
            break;

        case FSM_TAP_RAW:
            rc = fsm_raw_tap_update(session);
            break;

        default:
            rc = false;
            break;
    }

    return rc;
}


/**
 * @brief frees fsm tap resources pcap, nfqueues and raw socket
 *
 * @param session the fsm session involved
 */
void fsm_free_tap_resources(struct fsm_session *session)
{
    struct fsm_pcaps *pcaps;
    int tap_type;

    tap_type = session->tap_type;
    switch (tap_type)
    {
    case FSM_TAP_NONE:
    case FSM_TAP_PCAP:
        /* Free pcap resources */
        pcaps = session->pcaps;
        if (pcaps != NULL)
        {
            fsm_pcap_close(session);
        }
        break;

    case FSM_TAP_NFQ:
        /* Free nfq resources */
        nf_queue_exit();
        break;

    case FSM_TAP_RAW:
        /* Free raw socket resources */
        break;
    }
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
                     struct dpi_client, node);
        dpi_plugin->clients_init = true;
    }

    fsm_dpi_register_clients(session);
    return true;
}


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

    default:
        ret = true;;
    }

    return ret;
}
