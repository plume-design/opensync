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
#include "qosm_internal.h"

#include "osn_qos.h"
#include "osn_qdisc.h"
#include "osn_adaptive_qos.h"

#include "memutil.h"

static void callback_IP_Interface(
        ovsdb_update_monitor_t *mon,
        struct schema_IP_Interface *old,
        struct schema_IP_Interface *new);

static ovsdb_table_t table_IP_Interface;
static reflink_fn_t qosm_ip_interface_reflink_fn;
static reflink_fn_t qosm_ip_interface_qos_reflink_fn;
static int qosm_ip_interface_queue_prio_cmp(const void *a, const void *b);
static void qosm_on_qos_event(const char *if_name, enum osn_qos_event status);
static bool qosm_ip_interface_has_adaptive_qos(struct qosm_ip_interface *ipi, const char *direction);
static void qosm_ip_interface_schedule_reconfiguration(struct qosm_ip_interface *ipi);

static ds_tree_t qosm_ip_interface_list = DS_TREE_INIT(
        ds_str_cmp,
        struct qosm_ip_interface,
        ipi_tnode);

static osn_adaptive_qos_t *qosm_adpt_qos;

void qosm_ip_interface_init(void)
{
    OVSDB_TABLE_INIT(IP_Interface, if_name);
    OVSDB_TABLE_MONITOR_F(IP_Interface, C_VPACK("if_name", "qos", "enable"));
}

struct qosm_ip_interface *qosm_ip_interface_get(ovs_uuid_t *uuid)
{
    struct qosm_ip_interface *ipi;

    ipi = ds_tree_find(&qosm_ip_interface_list, (void *)uuid->uuid);
    if (ipi != NULL)
    {
        return ipi;
    }

    /* Allocate a new empty structure */
    ipi = CALLOC(1, sizeof(struct qosm_ip_interface));

    ipi->ipi_uuid = *uuid;
    reflink_init(&ipi->ipi_reflink, "IP_Interface");
    reflink_set_fn(&ipi->ipi_reflink, qosm_ip_interface_reflink_fn);

    reflink_init(&ipi->ipi_interface_qos_reflink, "IP_Interface.qos");
    reflink_set_fn(&ipi->ipi_interface_qos_reflink, qosm_ip_interface_qos_reflink_fn);

    ds_tree_insert(&qosm_ip_interface_list, ipi, ipi->ipi_uuid.uuid);

    return ipi;
}

void qosm_ip_interface_reflink_fn(reflink_t *ref, reflink_t *sender)
{
    struct qosm_ip_interface *ipi = CONTAINER_OF(ref, struct qosm_ip_interface, ipi_reflink);

    if (sender != NULL)
    {
        return;
    }

    LOG(DEBUG, "qosm: %s: IP_Interface reached 0 refcount.", ipi->ipi_ifname);

    if (ipi->ipi_qos != NULL)
    {
        LOG(DEBUG, "qosm: %s: Removing QoS configuration.", ipi->ipi_ifname);
        osn_qos_del(ipi->ipi_qos);
        ipi->ipi_qos = NULL;
    }

    if (ipi->ipi_qdisc_cfg != NULL)
    {
        LOG(DEBUG, "qosm: %s: Removing qdisc configuration.", ipi->ipi_ifname);
        osn_qdisc_cfg_del(ipi->ipi_qdisc_cfg);
        ipi->ipi_qdisc_cfg = NULL;
    }

    if (qosm_ip_interface_has_adaptive_qos(ipi, NULL))
    {
        LOG(DEBUG, "qosm: %s: IP_Interface deleted and had Adaptive QoS config: Stop Adaptive QoS", ipi->ipi_ifname);

        /*
         * If this interface has Adaptive QoS config, then stop Adaptive QoS.
         *
         * Note: Adaptive QoS is /not/ per interface, but is per 2 interfaces, but as soon
         * as one interface that previously had Adaptive QoS config defined, the Adaptive QoS
         * config needs to be stopped.
        */
        qosm_adaptive_qos_stop();
    }

    if (ipi->ipi_interface_qos != NULL)
    {
        reflink_disconnect(
                &ipi->ipi_interface_qos_reflink,
                &ipi->ipi_interface_qos->qos_reflink);
        ipi->ipi_interface_qos = NULL;
    }

    /* Stop any pending reconfigurations: */
    qosm_mgr_stop_qos_config(&ipi->ipi_uuid);

    ds_tree_remove(&qosm_ip_interface_list, ipi);

    FREE(ipi);
}

/* Schedule reconfiguration for this interface. */
static void qosm_ip_interface_schedule_reconfiguration(struct qosm_ip_interface *ipi)
{
    bool has_adaptive_qos_cfg;

    /* Check if there is Adaptive QoS config for this interface: */
    has_adaptive_qos_cfg = qosm_ip_interface_has_adaptive_qos(ipi, "DL") || qosm_ip_interface_has_adaptive_qos(ipi, "UL");

    /* Schedule reconfiguration: */
    if (has_adaptive_qos_cfg)
    {
        qosm_mgr_schedule_adaptive_qos_config(&ipi->ipi_uuid);
    }
    else
    {
        qosm_mgr_schedule_qos_config(&ipi->ipi_uuid);
    }
}

void qosm_ip_interface_qos_reflink_fn(reflink_t *ref, reflink_t *sender)
{
    struct qosm_ip_interface *ipi = CONTAINER_OF(ref, struct qosm_ip_interface, ipi_interface_qos_reflink);

    if (sender == NULL)
    {
        return;
    }

    LOG(INFO, "qosm: QoS: %s: %s  Schedule reconfiguration", ipi->ipi_ifname, ipi->ipi_uuid.uuid);

    /* Schedule reconfiguration: */
    qosm_ip_interface_schedule_reconfiguration(ipi);
}

/* Apply Interface_QoS/Interface_Queue configuration. */
static bool qosm_interface_queue_config_apply(
        struct qosm_ip_interface *ipi,
        struct qosm_interface_qos *qos,
        bool *qos_qdisc_cfg_exists)
{
    struct qosm_interface_queue *que;
    int qi;

    /*
     * Some backends require that queues are applied ordered by priority, where
     * the highest priority (lowest number) comes first.
     */
    qsort(
            qos->qos_interface_queue,
            qos->qos_interface_queue_len,
            sizeof(qos->qos_interface_queue[0]),
            qosm_ip_interface_queue_prio_cmp);

    /* OSN QoS config object: */
    ipi->ipi_qos = osn_qos_new(ipi->ipi_ifname);
    if (ipi->ipi_qos == NULL)
    {
        LOG(ERR, "qosm: %s: Error creating QoS configuration object.", ipi->ipi_ifname);
        return false;
    }

    /* OVSDB QoS config exists && the underlying QoS backend is qdisc-based. */
    *qos_qdisc_cfg_exists = osn_qos_is_qdisc_based(ipi->ipi_qos);

    /* Set QoS notify event callback for this interface.
     *
     * This is currently needed only if the lower layer osn_qos reports an event
     * that a reconfiguration may be needed for some reason. In that case we
     * forward the reconfiguration request to the upper layer qosm_mgr that will
     * do the reconfiguration (including any dependencies, for instance tc-filters).
     */
    osn_qos_notify_event_set(ipi->ipi_qos, qosm_on_qos_event);

    if (!osn_qos_begin(ipi->ipi_qos, NULL))
    {
        LOG(ERR, "qosm: %s: Error initializing QoS configuration.", ipi->ipi_ifname);
        return false;
    }

    for (qi = 0; qi < qos->qos_interface_queue_len; qi++)
    {
        struct osn_qos_queue_status qqs;
        bool rc;

        que = qos->qos_interface_queue[qi];

        LOG(INFO, "qosm: %s: Adding queue tag:%s prio:%d bandwidth:%d bandwidth_ceil:%d",
                ipi->ipi_ifname, que->que_tag, que->que_priority, que->que_bandwidth, que->que_bandwidth_ceil);

        rc = osn_qos_queue_begin(
                ipi->ipi_qos,
                que->que_priority,
                que->que_bandwidth,
                que->que_bandwidth_ceil,
                que->que_tag[0] == '\0' ? NULL : que->que_tag,
                &que->que_other_config,
                &qqs);

        if (!qosm_interface_queue_set_status(que, rc, &qqs))
        {
            LOG(WARN, "qosm: %s: Error updating queue status.", ipi->ipi_ifname);
        }

        if (!rc)
        {
            LOG(ERR, "qosm: %s: Error adding queue with tag: %s.",
                    ipi->ipi_ifname, que->que_tag);
            return false;
        }

        LOG(INFO, "qosm: %s: Registered queue tag:%s mark:%u",
                ipi->ipi_ifname, que->que_tag, qqs.qqs_fwmark);

        if (!osn_qos_queue_end(ipi->ipi_qos))
        {
            LOG(ERR, "qosm: %s: Error finalizing queue (tag: %s) config.",
                    ipi->ipi_ifname, que->que_tag);
            return false;
        }
    }

    if (!osn_qos_end(ipi->ipi_qos))
    {
        LOG(ERR, "qosm: %s: Error finalizing QoS configuration.", ipi->ipi_ifname);
        return false;
    }

    if (!osn_qos_apply(ipi->ipi_qos))
    {
        LOG(ERR, "qosm: %s: Error applying QoS configuration.", ipi->ipi_ifname);
        return false;
    }
    return true;
}

/* Callback to be called by the backend implementation to report qdisc apply success */
static void qosm_linux_queue_status_report_callback(const struct osn_qdisc_status *qdisc_status)
{
    struct qosm_linux_queue *que = (struct qosm_linux_queue *)qdisc_status->qs_ctx;

    if (que != NULL)
    {
        que->que_applied = qdisc_status->qs_applied;
    }
}

/* Apply Interface_QoS/Linux_Queue configuration. */
static bool qosm_linux_queue_config_apply(
        struct qosm_ip_interface *ipi,
        struct qosm_interface_qos *qos,
        bool *qos_qdisc_cfg_exists)
{
    struct qosm_linux_queue *que;
    int qi;
    bool rv = false;

    /* Clear the current status for any Linux_Queue rows: */
    if (!qosm_linux_queue_set_status_all(qos, NULL))
    {
        LOG(WARN, "qosm: %s: Error clearing Linux_Queue rows status", ipi->ipi_ifname);
    }

    /* OSN QDISC config object: */
    ipi->ipi_qdisc_cfg = osn_qdisc_cfg_new(ipi->ipi_ifname);
    if (ipi->ipi_qdisc_cfg == NULL)
    {
        LOG(ERR, "qosm: %s: Error creating QDISC configuration object.", ipi->ipi_ifname);
        return false;
    }

    /* OVSDB QoS config exists && the underlying QoS backend is qdisc-based.
     *
     * In this case the underlying backend is inherently qdisc-based.
     */
    *qos_qdisc_cfg_exists = true;

    /* Add all linux queues config parameters (all qdisc/class parameters)*/
    for (qi = 0; qi < qos->qos_linux_queue_len; qi++)
    {
        que = qos->qos_linux_queue[qi];

        que->que_applied = false; // Initially assume status as not applied

        struct osn_qdisc_params qdisc_params =
            {
                .oq_id = que->que_id,
                .oq_parent_id = que->que_parent_id,
                .oq_qdisc = que->que_qdisc,
                .oq_params = que->que_params,
                .oq_is_class = que->que_is_class,

                .oq_ctx = (void *)que // context for status reporting callback
            };

        if (!osn_qdisc_cfg_add(ipi->ipi_qdisc_cfg, &qdisc_params))
        {
            LOG(ERR, "qosm: %s: Error adding qdisc config parameters: %s", ipi->ipi_ifname, FMT_osn_qdisc_params(qdisc_params));
            return false;
        }
    }

    /* Set callback for qdisc applied status report: */
    osn_qdisc_cfg_notify_status_set(ipi->ipi_qdisc_cfg, qosm_linux_queue_status_report_callback);

    /* Apply the QDISC configuration: */
    if (!osn_qdisc_cfg_apply(ipi->ipi_qdisc_cfg))
    {
        LOG(ERR, "qosm: %s: Error applying qdisc configuration", ipi->ipi_ifname);

        osn_qdisc_cfg_del(ipi->ipi_qdisc_cfg);
        ipi->ipi_qdisc_cfg = NULL;
        goto out;
    }

    rv = true;
out:
    /* Set status for all Linux_Queue rows: */
    for (qi = 0; qi < qos->qos_linux_queue_len; qi++)
    {
        que = qos->qos_linux_queue[qi];

        const char *status = que->que_applied ? "success" : "error";

        if (!qosm_linux_queue_set_status(que, status))
        {
            LOG(WARN, "qosm: %s: %s: Error setting Linux_Queue status", ipi->ipi_ifname, que->que_uuid.uuid);
        }
    }

    return rv;
}

/**
 * Apply QoS configuration to the system for an interface.
 *
 * @param[in]   uuid                    OVSDB uuid of the interface
 * @param[out]  qos_qdisc_cfg_exists    Will be set to true if there was any actual QoS
 *                                      configuration for this interface and the underlying
 *                                      QoS backend is qdisc-based.
 */
bool qosm_qos_config_apply(const ovs_uuid_t *uuid, bool *qos_qdisc_cfg_exists)
{
    struct qosm_ip_interface *ipi;
    struct qosm_interface_qos *qos;
    bool qos_status = false;

    *qos_qdisc_cfg_exists = false;

    ipi = ds_tree_find(&qosm_ip_interface_list, (void *)uuid->uuid);
    if (ipi == NULL)
    {
        LOG(DEBUG, "qosm: QoS: Cannot find IP_Interface object for uuid=%s. Nothing to do.", uuid->uuid);
        return true; // Nothing to do
    }
    LOG(INFO, "qosm: QoS: %s: %s: Reconfiguring QoS for interface", ipi->ipi_ifname, uuid->uuid);

    // If this interface has QoS applied from before, delete the old config first:
    if (ipi->ipi_qos != NULL)
    {
        LOG(DEBUG, "qosm: %s: Removing old QoS configuration.", ipi->ipi_ifname);
        osn_qos_del(ipi->ipi_qos);
        ipi->ipi_qos = NULL;
    }

    // If this interface has QDISC configuration applied from before, delete the old config first:
    if (ipi->ipi_qdisc_cfg != NULL)
    {
        LOG(DEBUG, "qosm: %s: Removing old qdisc configuration.", ipi->ipi_ifname);
        osn_qdisc_cfg_del(ipi->ipi_qdisc_cfg);
        ipi->ipi_qdisc_cfg = NULL;
    }

    qos = ipi->ipi_interface_qos; // QoS config from OVSDB
    if (qos == NULL)
    {
        LOG(NOTICE, "qosm: %s: No Interface_QoS configuration.", ipi->ipi_ifname);
        return true;
    }

    /* Clear the current Interface_QoS status */
    if (!qosm_interface_qos_set_status(qos, NULL))
    {
        LOG(WARN, "qosm: %s: Error updating Interface_QoS status.", ipi->ipi_ifname);
    }

    /* Configurations of Interface_QoS->queues and Interface_QoS->lnx_queues are mutually exclusive: */
    if (qos->qos_interface_queue_len > 0 && qos->qos_linux_queue_len > 0)
    {
        LOG(ERR, "qosm: %s: Both Interface_QoS->queues and Interface_QoS->lnx_queues configured at the same time",
                ipi->ipi_ifname);
        qos_status = false;
        goto error;
    }

    /* Apply the configuration: */
    if (qos->qos_interface_queue_len > 0)
    {
        LOG(INFO, "qosm: %s: Applying Interface_QoS/Interface_Queue configuration.", ipi->ipi_ifname);

        qos_status = qosm_interface_queue_config_apply(ipi, qos, qos_qdisc_cfg_exists);
    }
    else if (qos->qos_linux_queue_len > 0)
    {
        LOG(INFO, "qosm: %s: Applying Interface_QoS/Linux_Queue configuration.", ipi->ipi_ifname);

        qos_status = qosm_linux_queue_config_apply(ipi, qos, qos_qdisc_cfg_exists);
    }

    if (!qos_status)
    {
        LOG(ERROR, "qosm: %s: Error applying QoS configuration", ipi->ipi_ifname);
    }

error:
    /* Set Interface_QoS status: */
    if (!qosm_interface_qos_set_status(qos, qos_status ? "success" : "error"))
    {
        LOG(WARN, "qosm: %s: Error updating Interface_QoS status.", ipi->ipi_ifname);
    }
    return qos_status;
}

void callback_IP_Interface(
        ovsdb_update_monitor_t *mon,
        struct schema_IP_Interface *old,
        struct schema_IP_Interface *new)
{
    (void)old;

    struct qosm_ip_interface *ipi;
    ovs_uuid_t ipi_uuid;

    STRSCPY(ipi_uuid.uuid, mon->mon_uuid);

    ipi = qosm_ip_interface_get(&ipi_uuid);
    if (ipi == NULL)
    {
        LOG(ERR, "qosm: Error acquiring the IP_Interface object for uuid %s.", ipi_uuid.uuid);
        return;
    }

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            reflink_ref(&ipi->ipi_reflink, 1);
            break;

        case OVSDB_UPDATE_MODIFY:
            break;

        case OVSDB_UPDATE_DEL:
            reflink_ref(&ipi->ipi_reflink, -1);
            return;

        default:
            LOG(ERR, "qosm: IP_Interface monitor error.");
            break;
    }

    STRSCPY(ipi->ipi_ifname, new->if_name);
    if (ipi->ipi_interface_qos != NULL)
    {
        reflink_disconnect(&ipi->ipi_interface_qos_reflink, &ipi->ipi_interface_qos->qos_reflink);
        ipi->ipi_interface_qos = NULL;
    }

    if (new->qos_exists)
    {
        ipi->ipi_interface_qos = qosm_interface_qos_get(&new->qos);
        if (ipi->ipi_interface_qos == NULL)
        {
            LOG(ERR, "qosm: %s: Error acquiring Interface_QoS object.", ipi->ipi_ifname);
        }
        else
        {
            reflink_connect(&ipi->ipi_interface_qos_reflink, &ipi->ipi_interface_qos->qos_reflink);
        }
    }

    LOG(INFO, "qosm: QoS: %s: %s: Schedule reconfiguration", ipi->ipi_ifname, ipi->ipi_uuid.uuid);

    /* Schedule reconfiguration: */
    qosm_ip_interface_schedule_reconfiguration(ipi);
}

int qosm_ip_interface_queue_prio_cmp(
        const void *_a, const void *_b)
{
    const struct qosm_interface_queue *const *a = _a;
    const struct qosm_interface_queue *const *b = _b;

    return (*a)->que_priority - (*b)->que_priority;
}

static struct qosm_ip_interface *qosm_find_intf_by_name(const char *if_name)
{
    struct qosm_ip_interface *ipi;

    ds_tree_foreach(&qosm_ip_interface_list, ipi)
    {
        if (strncmp(ipi->ipi_ifname, if_name, sizeof(ipi->ipi_ifname)) == 0)
        {
            return ipi;
        }
    }
    return NULL;
}

/* osn_qos event callback handler. */
static void qosm_on_qos_event(const char *if_name, enum osn_qos_event event)
{
    struct qosm_ip_interface *ipi;

    if (event == OSN_QOS_EVENT_RECONFIGURATION_NEEDED)
    {
        LOG(NOTICE, "qosm: %s: Reconfiguration needed event", if_name);

        ipi = qosm_find_intf_by_name(if_name);
        if (ipi == NULL)
        {
            LOG(ERR, "qosm: %s: Reconfiguration needed event: Cannot find interface object", if_name);
            return;
        }

        LOG(INFO, "qosm: QoS: %s: %s: Schedule reconfiguration", ipi->ipi_ifname, ipi->ipi_uuid.uuid);

        /*
         * Schedule a reconfiguration for this interface with qosm_mgr:
         */
        qosm_ip_interface_schedule_reconfiguration(ipi);
    }
    else
    {
        LOG(WARN, "qosm: %s: Reconfiguration needed event: Unknown QoS status notification: %d", if_name, event);
    }
}

/* Does this interface has Adaptive QoS config attached/defined? */
static bool qosm_ip_interface_has_adaptive_qos(struct qosm_ip_interface *ipi, const char *direction)
{
    bool has_adaptive_qos_cfg = false;

    if (direction != NULL)
    {
        if (!(strcmp(direction, "UL") == 0 || strcmp(direction, "DL") == 0))
        {
            LOG(ERR, "qosm: Invalid direction for Adaptive QoS config: %s", direction);
            return false;
        }

    }

    if (ipi->ipi_interface_qos != NULL && ipi->ipi_interface_qos->qos_adaptive_qos != NULL)
    {
        ds_map_str_t *qos_adaptive_qos = ipi->ipi_interface_qos->qos_adaptive_qos;

        if (!ds_map_str_empty(qos_adaptive_qos))
        {
            if (direction != NULL) // interested in specific direction
            {
                char *val;
                if (ds_map_str_find(qos_adaptive_qos, "direction", &val) && strcmp(val, direction) == 0)
                {
                    has_adaptive_qos_cfg = true;
                }
            }
            else // just check if adaptive_qos has any key/values defined
            {
                has_adaptive_qos_cfg = true;
            }
        }
    }
    return has_adaptive_qos_cfg;
}

/* Check if Adaptive QoS is defined for direction @param direction (possible values: "UL" or "DL").
 * If such an interface found, return the qosm_ip_interface for it.
 */
static struct qosm_ip_interface *qosm_adaptive_qos_get_ipi(const char *direction)
{
    struct qosm_ip_interface *ipi;

    if (direction == NULL) return NULL;

    ds_tree_foreach(&qosm_ip_interface_list, ipi)
    {
        if (qosm_ip_interface_has_adaptive_qos(ipi, direction))
        {
            return ipi;
        }
    }
    return NULL;
}

static bool qosm_adaptive_qos_get_config_int_param(ds_map_str_t *qos_adaptive_qos, const char *key, int *value)
{
    char *val;

    if (ds_map_str_find(qos_adaptive_qos, key, &val))
    {
        *value = atoi(val);
        return true;
    }
    return false;
}

/* Check if there are min_rate, base_rate, max_rate defined in adaptive_qos config for this interface,
 * if yes, set them to OSN adaptive_qos config. */
static bool qosm_adaptive_qos_config_set(struct qosm_ip_interface *ipi, const char *direction)
{
    int min_rate;
    int base_rate;
    int max_rate;
    bool rv = true;

    if (qosm_adpt_qos == NULL) return false;
    if (ipi->ipi_interface_qos == NULL || ipi->ipi_interface_qos->qos_adaptive_qos == NULL) return false;

    rv &= qosm_adaptive_qos_get_config_int_param(ipi->ipi_interface_qos->qos_adaptive_qos, "min_rate", &min_rate);
    rv &= qosm_adaptive_qos_get_config_int_param(ipi->ipi_interface_qos->qos_adaptive_qos, "base_rate", &base_rate);
    rv &= qosm_adaptive_qos_get_config_int_param(ipi->ipi_interface_qos->qos_adaptive_qos, "max_rate", &max_rate);
    if (!rv)
    {
        LOG(INFO, "qosm: adaptive_qos: %s: direction=%s: min_rate/base_rate/max_rate not defined. Defaults will be used.", ipi->ipi_ifname, direction);
        return true;
    }

    LOG(INFO, "qosm: adaptive_qos: %s: direction=%s: Setting shaper parameters: min_rate=%d, base_rate=%d, max_rate=%d",
        ipi->ipi_ifname, direction, min_rate, base_rate, max_rate);

    if (strcmp(direction, "DL") == 0)
    {
        rv &= osn_adaptive_qos_DL_shaper_params_set(qosm_adpt_qos, min_rate, base_rate, max_rate);
    }
    else if (strcmp(direction, "UL") == 0)
    {
        rv &= osn_adaptive_qos_UL_shaper_params_set(qosm_adpt_qos, min_rate, base_rate, max_rate);
    }
    else
    {
        rv = false;
    }

    if (!rv)
    {
        LOG(ERR, "qosm: adaptive_qos: %s: direction=%s: Error setting shaper parameters", ipi->ipi_ifname, direction);
    }
    return rv;
}

/* Apply Adaptive QoS (Interface_QoS->adaptive_qos)
 * and any optional additional common adaptive QoS configuration (AdaptiveQoS). */
bool qosm_adaptive_qos_apply()
{
    struct qosm_ip_interface *ipi_dl;
    struct qosm_ip_interface *ipi_ul;
    struct qosm_adaptive_qos_cfg *adpt_qos_cfg;

    ipi_dl = qosm_adaptive_qos_get_ipi("DL");
    ipi_ul = qosm_adaptive_qos_get_ipi("UL");

    if (ipi_dl == NULL || ipi_ul == NULL)
    {
        LOG(DEBUG, "qosm: No Adaptive QoS configuration");
        return true;
    }

    LOG(INFO, "qosm: Adaptive QoS defined: DL:%s, UL:%s. Applying.", ipi_dl->ipi_ifname, ipi_ul->ipi_ifname);

    /* Just in case, if any previous Adaptive QoS still running: stop it: */
    qosm_adaptive_qos_stop();

    /* Create OSN Adaptive QoS object: */
    qosm_adpt_qos = osn_adaptive_qos_new(ipi_dl->ipi_ifname, ipi_ul->ipi_ifname);
    if (qosm_adpt_qos == NULL)
    {
        LOG(ERR, "qosm: Error creating OSN Adaptive QoS object");
        return false;
    }

    /* Set Adaptive QoS per UL and per DL parameters: */
    if (!qosm_adaptive_qos_config_set(ipi_dl, "DL") || !qosm_adaptive_qos_config_set(ipi_ul, "UL"))
    {
        LOG(ERR, "qosm: adaptive_qos: Error setting shaper parameters");
        return false;
    }

    /*
     * Set any additional (optional) common adaptive QoS configuration
     * (not per interface, i.e. not per UL/DL):
     */
    adpt_qos_cfg = qosm_adaptive_qos_cfg_get();
    if (adpt_qos_cfg != NULL)
    {
        bool rv = true;

        LOG(DEBUG, "qosm: adaptive_qos: Setting global adaptive QoS configuration");

        if (adpt_qos_cfg->num_reflectors > 0)
        {
            rv &= osn_adaptive_qos_reflector_list_add(qosm_adpt_qos, adpt_qos_cfg->reflectors, adpt_qos_cfg->num_reflectors);
        }
        rv &= osn_adaptive_qos_reflectors_randomize_set(qosm_adpt_qos, adpt_qos_cfg->rand_reflectors);
        rv &= osn_adaptive_qos_reflectors_ping_interval_set(qosm_adpt_qos, adpt_qos_cfg->ping_interval);
        rv &= osn_adaptive_qos_num_pingers_set(qosm_adpt_qos, adpt_qos_cfg->num_pingers);
        rv &= osn_adaptive_qos_active_threshold_set(qosm_adpt_qos, adpt_qos_cfg->active_thresh);
        rv &= osn_adaptive_qos_other_config_set(qosm_adpt_qos, adpt_qos_cfg->other_config);

        if (!rv)
        {
            LOG(ERR, "qosm: adaptive_qos: Error setting global adaptive QoS configuration to osn_adaptive_qos");
            return false;
        }
    }

    LOG(NOTICE, "qosm: Starting Adaptive QoS");

    /* Apply adaptive QoS configuration to the system: */
    if (!osn_adaptive_qos_apply(qosm_adpt_qos))
    {
        LOG(ERR, "qosm: Error applying Adaptive QoS");

        osn_adaptive_qos_del(qosm_adpt_qos);
        qosm_adpt_qos = NULL;

        return false;
    }

    return true;
}

/* Stop any Adaptive QoS config, if running. */
bool qosm_adaptive_qos_stop()
{
    if (qosm_adpt_qos != NULL)
    {
        LOG(NOTICE, "qosm: Stopping Adaptive QoS");

        if (!osn_adaptive_qos_del(qosm_adpt_qos))
        {
            LOG(ERR, "qosm: Error stopping Adaptive QoS");
        }
        qosm_adpt_qos = NULL;
    }
    return true;
}
