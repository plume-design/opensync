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

static ds_tree_t qosm_ip_interface_list = DS_TREE_INIT(
        ds_str_cmp,
        struct qosm_ip_interface,
        ipi_tnode);

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

void qosm_ip_interface_qos_reflink_fn(reflink_t *ref, reflink_t *sender)
{
    struct qosm_ip_interface *ipi = CONTAINER_OF(ref, struct qosm_ip_interface, ipi_interface_qos_reflink);

    if (sender == NULL)
    {
        return;
    }

    LOG(INFO, "qosm: QoS: %s: %s  Schedule reconfiguration", ipi->ipi_ifname, ipi->ipi_uuid.uuid);

    /* Schedule reconfiguration: */
    qosm_mgr_schedule_qos_config(&ipi->ipi_uuid);
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
    struct qosm_interface_queue *que;
    bool qos_status = false;
    int qi;

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

    qos = ipi->ipi_interface_qos; // QoS config from OVSDB
    if (qos == NULL)
    {
        LOG(NOTICE, "qosm: %s: No QoS configuration.", ipi->ipi_ifname);
        return true;
    }

    /*
     * Some backends require that queues are applied ordered by priority, where
     * the highest priority (lowest number) comes first.
     */
    qsort(
            qos->qos_interface_queue,
            qos->qos_interface_queue_len,
            sizeof(qos->qos_interface_queue[0]),
            qosm_ip_interface_queue_prio_cmp);

    /* Clear the current status */
    if (!qosm_interface_qos_set_status(qos, NULL))
    {
        LOG(WARN, "qosm: %s: Error updating Interface_QoS status.", ipi->ipi_ifname);
    }

    /* OSN QoS config object: */
    ipi->ipi_qos = osn_qos_new(ipi->ipi_ifname);
    if (ipi->ipi_qos == NULL)
    {
        LOG(ERR, "qosm: %s: Error creating QoS configuration object.", ipi->ipi_ifname);
        goto error;
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
        goto error;
    }

    for (qi = 0; qi < qos->qos_interface_queue_len; qi++)
    {
        struct osn_qos_queue_status qqs;
        bool rc;

        que = qos->qos_interface_queue[qi];

        LOG(INFO, "qosm: %s: Adding queue tag:%s prio:%d bandwidth:%d",
                ipi->ipi_ifname, que->que_tag, que->que_priority, que->que_bandwidth);

        rc = osn_qos_queue_begin(
                ipi->ipi_qos,
                que->que_priority,
                que->que_bandwidth,
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
            goto error;
        }

        LOG(INFO, "qosm: %s: Registered queue tag:%s mark:%u",
                ipi->ipi_ifname, que->que_tag, qqs.qqs_fwmark);

        if (!osn_qos_queue_end(ipi->ipi_qos))
        {
            LOG(ERR, "qosm: %s: Error finalizing queue (tag: %s) config.",
                    ipi->ipi_ifname, que->que_tag);
            goto error;
        }
    }

    if (!osn_qos_end(ipi->ipi_qos))
    {
        LOG(ERR, "qosm: %s: Error finalizing QoS configuration.", ipi->ipi_ifname);
        goto error;
    }

    if (!osn_qos_apply(ipi->ipi_qos))
    {
        LOG(ERR, "qosm: %s: Error applying QoS configuration.", ipi->ipi_ifname);
        goto error;
    }

    qos_status = true;
error:
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
    qosm_mgr_schedule_qos_config(&ipi->ipi_uuid);
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
        qosm_mgr_schedule_qos_config(&ipi->ipi_uuid);
    }
    else
    {
        LOG(WARN, "qosm: %s: Reconfiguration needed event: Unknown QoS status notification: %d", if_name, event);
    }
}
