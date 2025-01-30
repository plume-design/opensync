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

#include "qosm_qos.h"
#include "qosm_filter.h"

#include <stdbool.h>

#include "ovsdb.h"
#include "memutil.h"
#include "ds_tree.h"
#include "const.h"
#include "evx.h"
#include "log.h"
#include "hw_acc.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

/* Interface QoS config debounce time */
#define CONFIG_APPLY_DEBOUNCE    0.500

/* Adaptive QoS apply debounce time -- a bit higher, since Adaptive QoS
 * is only on top of a base QoS, where exactly 2 interfaces are defined for Adaptive QoS */
#define ADAPTIVE_CONFIG_APPLY_DEBOUNCE    1.500


/* Pending interface QoS reconfiguration. */
struct intf_pending_qos
{
    ovs_uuid_t      pq_uuid;        /* OVSDB uuid of the interface for which reconfiguration is pending */

    bool            pq_adaptive_qos;/* Optional Adaptive QoS config on top (requires base QoS config) */

    ev_debounce     pq_debouncer;   /* debouncer */

    ds_tree_node_t  pq_tnode;
};

static void apply_debouncer_fn(struct ev_loop *loop, ev_debounce *w, int revent);
static bool qosm_mgr_apply_config(struct intf_pending_qos *pending_qos);

static struct intf_pending_qos *qosm_pending_qos_new(const ovs_uuid_t *uuid);
static bool qosm_pending_qos_del(struct intf_pending_qos *pending_qos);
static struct intf_pending_qos *qosm_pending_qos_get(const ovs_uuid_t *uuid);
static void qosm_pending_qos_debounce(struct intf_pending_qos *pending_qos);

static void adaptive_apply_debouncer_fn(struct ev_loop *loop, ev_debounce *w, int revent);
static void qosm_mgr_adaptive_qos_debounce();

/* Interface QoS reconfiguration apply queue */
static ds_tree_t  qosm_apply_queue = DS_TREE_INIT(ds_str_cmp, struct intf_pending_qos, pq_tnode);

static ev_debounce qosm_adpt_qos_debouncer;

/* Create a pending intf qos and add it to apply queue. */
static struct intf_pending_qos *qosm_pending_qos_new(const ovs_uuid_t *uuid)
{
    struct intf_pending_qos *pending_qos;

    pending_qos = CALLOC(1, sizeof(*pending_qos));
    pending_qos->pq_uuid = *uuid;

    ds_tree_insert(&qosm_apply_queue, pending_qos, pending_qos->pq_uuid.uuid);

    // Initialize debuncer:
    ev_debounce_init(&pending_qos->pq_debouncer, apply_debouncer_fn, CONFIG_APPLY_DEBOUNCE);

    return pending_qos;
}

/* Cancel the pending intf qos, remove it from the apply queue. */
static bool qosm_pending_qos_del(struct intf_pending_qos *pending_qos)
{
    ev_debounce_stop(EV_DEFAULT, &pending_qos->pq_debouncer);

    ds_tree_remove(&qosm_apply_queue, pending_qos);
    FREE(pending_qos);
    return true;
}

/* Find the pending intf qos in the apply queue, return NULL if not found. */
static struct intf_pending_qos *qosm_pending_qos_get(const ovs_uuid_t *uuid)
{
    return ds_tree_find(&qosm_apply_queue, uuid->uuid);
}

/* Start (or restart) the pending intf qos debounce timer. */
static void qosm_pending_qos_debounce(struct intf_pending_qos *pending_qos)
{
    if (pending_qos != NULL)
    {
        ev_debounce_start(EV_DEFAULT, &pending_qos->pq_debouncer);
    }
}

/* Apply interface pending QoS (osn_qos and tc-filter) configuration,
 * and any optional adaptive QoS on top */
static bool qosm_mgr_apply_config(struct intf_pending_qos *pending_qos)
{
    bool qos_qdisc_cfg_exists = false;
    ovs_uuid_t uuid;
    bool rv;

    uuid = pending_qos->pq_uuid;
    LOG(INFO, "qosm_mgr: Applying QoS and TC-filter config: %s", uuid.uuid);

    /* Before anything else, stop any Adaptive QoS service, if this interface
     * is marked as having Adaptive QoS config. */
    if (pending_qos->pq_adaptive_qos)
    {
        qosm_adaptive_qos_stop();
    }

    /*
     * First, Interface_QoS/Interface_Queue (osn_qos).
     *
     * qos_qdisc_cfg_exists flag will tell as if there was any actual QoS qdisc-based config applied.
     */

    LOG(NOTICE, "qosm_mgr: Applying QoS config: %s", uuid.uuid);
    rv = qosm_qos_config_apply(&uuid, &qos_qdisc_cfg_exists);
    if (!rv)
    {
        LOGE("qosm_mgr: Error applying Interface_Qos/Interface_Queue config for: %s", uuid.uuid);
        return false;
    }

    /*
     * Then, Interface_Classifier (osn_tc).
     *
     * If there was osn_qos config, we should not reset egress.
     */
    bool reset_egress = !qos_qdisc_cfg_exists;
    LOG(DEBUG, "qosm: %s: reset_egress=%d", uuid.uuid, reset_egress);

    LOG(NOTICE, "qosm_mgr: Applying Interface_Classifier config for: %s", uuid.uuid);
    rv = qosm_filter_config_apply(&uuid, reset_egress);
    if (!rv)
    {
        LOGE("qosm_mgr: Error applying Interface_Classifier config for: %s", uuid.uuid);
        return false;
    }

    LOG(INFO, "qosm_mgr: Done: Applied QoS and TC-filter config: %s", uuid.uuid);

    hw_acc_flush_all_flows();

    /* Any optional Adaptive QoS on top: */
    if (pending_qos->pq_adaptive_qos)
    {
        /* Debounce-schedule adaptive QoS: */
        qosm_mgr_adaptive_qos_debounce();
    }

    return true;
}

/* Debouncer callback for interface pending reconfiguration: */
static void apply_debouncer_fn(struct ev_loop *loop, ev_debounce *w, int revent)
{
    struct intf_pending_qos *pending_qos;

    pending_qos = CONTAINER_OF(w, struct intf_pending_qos, pq_debouncer);

    qosm_mgr_apply_config(pending_qos);
}

/* Schedule QoS or QoS/Classifier reconfiguration.
 *  - classifier == true --> schedule classifier reconfiguration, adaptive flag is ignored
 *  - otherwise, schedule QoS reconfiguration; if adaptive==true --> there is Adaptive QoS on top as well
 */
void __qosm_mgr_schedule_qos_config(const ovs_uuid_t *uuid, bool classifier, bool adaptive)
{
    struct intf_pending_qos *pending_qos;

    if (classifier)
    {
        LOG(DEBUG, "qosm_mgr: %s: uuid=%s, Classifier", __func__, uuid->uuid);
    }
    else
    {
        LOG(DEBUG, "qosm_mgr: %s: uuid=%s, QoS: adaptive=%d", __func__, uuid->uuid, adaptive);
    }

    LOG(INFO, "qosm_mgr: Schedule reconfiguration: %s", uuid->uuid);

    pending_qos = qosm_pending_qos_get(uuid);
    if (pending_qos == NULL)
    {
        pending_qos = qosm_pending_qos_new(uuid);
    }

    if (!classifier) // Only if not classifier config, we should touch the adaptive flag
    {
        pending_qos->pq_adaptive_qos = adaptive;
    }

    qosm_pending_qos_debounce(pending_qos);
}

/**
 * Schedule reconfiguration for this interface.
 *
 * The reconfiguration is marked as pending and will be
 * executed with a debounce timer.
 *
 * @param[in]   uuid   OVSDB uuid of the interface
 */
void qosm_mgr_schedule_qos_config(const ovs_uuid_t *uuid)
{
    __qosm_mgr_schedule_qos_config(uuid, false, false);
}

/**
 * Schedule reconfiguration for this interface, where this interface
 * also has Adaptive QoS config attached.
 *
 * The reconfiguration is marked as pending and will be
 * executed with a debounce timer.
 *
 * @param[in]   uuid   OVSDB uuid of the interface
 */
void qosm_mgr_schedule_adaptive_qos_config(const ovs_uuid_t *uuid)
{
    __qosm_mgr_schedule_qos_config(uuid, false, true);
}

/**
 * Schedule Classifier reconfiguration for this interface.
 *
 * The reconfiguration is marked as pending and will be
 * executed with a debounce timer.
 *
 * @param[in]   uuid   OVSDB uuid of the interface
 */
void qosm_mgr_schedule_classifier_qos_config(const ovs_uuid_t *uuid)
{
    __qosm_mgr_schedule_qos_config(uuid, true, false);
}

/**
 * Cancel any pending reconfiguration for this interface.
 *
 * @param[in]   uuid   OVSDB uuid of the interface
 */
void qosm_mgr_stop_qos_config(const ovs_uuid_t *uuid)
{
    struct intf_pending_qos *pending_qos;

    LOG(INFO, "qosm_mgr: Stop reconfiguration: %s", uuid->uuid);

    pending_qos = qosm_pending_qos_get(uuid);
    if (pending_qos != NULL)
    {
        qosm_pending_qos_del(pending_qos);
    }
}

/* Debounced action for Adaptive QoS config: */
static void adaptive_apply_debouncer_fn(struct ev_loop *loop, ev_debounce *w, int revent)
{
    struct intf_pending_qos *pending_qos;
    int num_adpt_qos_intfs = 0;

    LOG(DEBUG, "qosm_mgr: Adaptive QoS debouncer timer expired");

    /* Check if there are exactly 2 interfaces with adaptive_qos flag: */
    ds_tree_foreach(&qosm_apply_queue, pending_qos)
    {
        if (pending_qos->pq_adaptive_qos)
        {
            num_adpt_qos_intfs++;
        }
    }
    LOG(DEBUG, "qosm_mgr: Num interfaces with Adaptive QoS config: %d", num_adpt_qos_intfs);

    if (num_adpt_qos_intfs != 2)
    {
        LOG(NOTICE, "qosm_mgr: Number of interfaces with Adaptive QoS config not 2, but %d. Cannot apply Adaptive QoS",
                num_adpt_qos_intfs);
        return;
    }

    /* Call Adaptive QoS apply: */
    LOG(NOTICE, "qosm_mgr: Applying Adaptive QoS");
    qosm_adaptive_qos_apply();

    hw_acc_flush_all_flows();
}

/* Schedule debounced adaptive_qos configuration apply */
static void qosm_mgr_adaptive_qos_debounce()
{
    static bool initited;

    if (!initited)
    {
        ev_debounce_init(&qosm_adpt_qos_debouncer, adaptive_apply_debouncer_fn, ADAPTIVE_CONFIG_APPLY_DEBOUNCE);
        initited = true;
    }

    LOG(DEBUG, "qosm_mgr: Scheduling Adaptive QoS apply in %f seconds", ADAPTIVE_CONFIG_APPLY_DEBOUNCE);

    ev_debounce_start(EV_DEFAULT, &qosm_adpt_qos_debouncer);
}

