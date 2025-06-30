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

#include "qosm.h"
#include "qosm_internal.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "ovsdb_table.h"
#include "schema.h"
#include "memutil.h"
#include "util.h"
#include "osn_types.h"
#include "ds_map_str.h"
#include "osa_assert.h"
#include "log.h"

/* OVSDB table AdaptiveQoS */
static ovsdb_table_t table_AdaptiveQoS;

/* Internal AdaptiveQoS config structure. Singleton. */
static struct qosm_adaptive_qos_cfg *g_adaptive_qos = NULL;

/* Create new AdaptiveQoS config, initialized to OpenSync defaults. */
static struct qosm_adaptive_qos_cfg *qosm_adaptive_qos_cfg_new(void)
{
    struct qosm_adaptive_qos_cfg *adpt_qos_cfg;

    ASSERT(g_adaptive_qos == NULL, "qosm_adaptive_qos_cfg is singleton");

    adpt_qos_cfg = CALLOC(1, sizeof(*adpt_qos_cfg));

    adpt_qos_cfg->rand_reflectors = OSN_ADAPTIVE_QOS_DEFAULT_RAND_REFLECTORS;
    adpt_qos_cfg->ping_interval = OSN_ADAPTIVE_QOS_DEFAULT_PING_INTERVAL;
    adpt_qos_cfg->num_pingers = OSN_ADAPTIVE_QOS_DEFAULT_NUM_PINGERS;
    adpt_qos_cfg->active_thresh = OSN_ADAPTIVE_QOS_DEFAULT_ACTIVE_THRESH;

    adpt_qos_cfg->other_config = ds_map_str_new();

    g_adaptive_qos = adpt_qos_cfg;
    return adpt_qos_cfg;
}

/* Delete the AdaptiveQoS config */
static bool qosm_adaptive_qos_cfg_del(struct qosm_adaptive_qos_cfg *adpt_qos_cfg)
{
    ASSERT(g_adaptive_qos != NULL, "No qosm_adaptive_qos_cfg singleton object to delete, already deleted?");

    ds_map_str_delete(&adpt_qos_cfg->other_config);
    FREE(adpt_qos_cfg);
    g_adaptive_qos = NULL;
    return true;
}

/* Get AdaptiveQoS config. If no AdaptiveQoS config present, NULL is returned. */
struct qosm_adaptive_qos_cfg *qosm_adaptive_qos_cfg_get(void)
{
    return g_adaptive_qos;
}

/* Set AdaptiveQoS config from schema to internal config structure. */
static bool qosm_adaptive_qos_cfg_set(
        struct qosm_adaptive_qos_cfg *adpt_qos_cfg,
        ovsdb_update_monitor_t *mon,
        struct schema_AdaptiveQoS *old,
        struct schema_AdaptiveQoS *new)
{
    if (new->latency_measure_type_exists && strcmp(new->latency_measure_type, "passive") == 0)
    {
        LOG(ERR, "qosm_adaptive_qos: Adaptive QoS with passive latency measurements not supported");
        return false;
    }

    adpt_qos_cfg->rand_reflectors =
            new->rand_reflectors_exists ? new->rand_reflectors : OSN_ADAPTIVE_QOS_DEFAULT_RAND_REFLECTORS;
    adpt_qos_cfg->ping_interval =
            new->ping_interval_exists ? new->ping_interval : OSN_ADAPTIVE_QOS_DEFAULT_PING_INTERVAL;
    adpt_qos_cfg->num_pingers = new->num_pingers_exists ? new->num_pingers : OSN_ADAPTIVE_QOS_DEFAULT_NUM_PINGERS;
    adpt_qos_cfg->active_thresh =
            new->active_thresh_kbps_exists ? new->active_thresh_kbps : OSN_ADAPTIVE_QOS_DEFAULT_ACTIVE_THRESH;

    /* Custom reflectors: */
    adpt_qos_cfg->num_reflectors = 0;
    if (new->reflectors_list_exists)
    {
        char *token;
        char ip_list[sizeof(new->reflectors_list)];

        memcpy(ip_list, new->reflectors_list, sizeof(ip_list));

        token = strtok(ip_list, " ");
        while (token != NULL && adpt_qos_cfg->num_reflectors < QOSM_ADAPTIVE_QOS_MAX_REFLECTORS)
        {
            osn_ipany_addr_t ip_addr;
            if (osn_ipany_addr_from_str(&ip_addr, token))
            {
                adpt_qos_cfg->reflectors[adpt_qos_cfg->num_reflectors++] = ip_addr;
            }
            else
            {
                LOG(ERR, "qosm_adaptive_qos: Failed parsing IP address: %s", token);
            }
            token = strtok(NULL, " ");
        }
        if (adpt_qos_cfg->num_reflectors == QOSM_ADAPTIVE_QOS_MAX_REFLECTORS)
        {
            LOG(WARN, "qosm_adaptive_qos: Maximum number of reflectors (%d) reached", QOSM_ADAPTIVE_QOS_MAX_REFLECTORS);
        }
    }

    /* other config */
    ds_map_str_clear(adpt_qos_cfg->other_config);
    if (new->other_config_len > 0)
    {
        ds_map_str_insert_schema_map(
                adpt_qos_cfg->other_config,
                new->other_config,
                LOG_SEVERITY_WARN,
                "AdaptiveQoS.other_config duplicate key");
    }

    return true;
}

/*
 * OVSDB monitor update callback for AdaptiveQoS OVSDB table.
 */
static void callback_AdaptiveQoS(
        ovsdb_update_monitor_t *mon,
        struct schema_AdaptiveQoS *old,
        struct schema_AdaptiveQoS *new)
{
    struct qosm_adaptive_qos_cfg *adpt_qos_cfg = NULL;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            LOG(INFO, "qosm_adaptive_qos: AdaptiveQoS update: NEW row");

            adpt_qos_cfg = qosm_adaptive_qos_cfg_new();
            if (adpt_qos_cfg == NULL)
            {
                LOG(ERR, "qosm_adaptive_qos: Failed creating new AdaptiveQoS config.");
                return;
            }
            break;

        case OVSDB_UPDATE_MODIFY:
            LOG(INFO, "qosm_adaptive_qos: AdaptiveQoS update: MODIFY row");

            adpt_qos_cfg = qosm_adaptive_qos_cfg_get();
            if (adpt_qos_cfg == NULL)
            {
                LOG(WARN, "qosm_adaptive_qos: Cannot modify AdaptiveQoS config: object not found");
                return;
            }
            break;

        case OVSDB_UPDATE_DEL:
            LOG(INFO, "qosm_adaptive_qos: AdaptiveQoS update: DELETE row");

            adpt_qos_cfg = qosm_adaptive_qos_cfg_get();
            if (adpt_qos_cfg == NULL)
            {
                LOG(WARN, "qosm_adaptive_qos: Cannot delete AdaptiveQoS config: object not found");
                return;
            }

            /* Delete the AdaptiveQoS config.
             * Effectively this means that the custom common adaptive QoS config will use defaults.
             */
            qosm_adaptive_qos_cfg_del(adpt_qos_cfg);
            break;

        default:
            LOG(ERROR, "qosm_adaptive_qos: Monitor update error.");
            return;
    }

    if (mon->mon_type == OVSDB_UPDATE_NEW || mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        if (adpt_qos_cfg == NULL)
        {
            return;
        }

        /* Set AdaptiveQoS parameters from OVSDB to internal struct: */
        if (!qosm_adaptive_qos_cfg_set(adpt_qos_cfg, mon, old, new))
        {
            LOG(ERR, "qosm_adaptive_qos: Failed setting AdaptiveQoS params to internal structures");
            return;
        }
    }

    /*
     * Schedule AdaptiveQoS config change with QoS manager. QoS mgr has info about which
     * interfaces are actually marked for adaptive QoS and have per-interface adaptive QoS
     * configuration, and thus it knows when to or when not to reapply adaptive QoS.
     */
    qosm_mgr_schedule_adaptive_qos_config_change();
}

/* Initialize AdaptiveQoS OVSDB table handling */
void qosm_adaptive_qos_init(void)
{
    OVSDB_TABLE_INIT_NO_KEY(AdaptiveQoS);
    OVSDB_TABLE_MONITOR(AdaptiveQoS, false);
}
