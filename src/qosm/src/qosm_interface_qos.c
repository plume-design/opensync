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

#include "osa_assert.h"
#include "qosm_internal.h"

static void callback_Interface_QoS(
        ovsdb_update_monitor_t *mon,
        struct schema_Interface_QoS *old,
        struct schema_Interface_QoS *new);

static ovsdb_table_t table_Interface_QoS;
static reflink_fn_t qosm_interface_qos_reflink_fn;
static reflink_fn_t qosm_interface_qos_queue_reflink_fn;
static void qosm_interface_qos_queue_free(struct qosm_interface_qos *qos);

static ds_tree_t qosm_interface_qos_list = DS_TREE_INIT(
        ds_str_cmp,
        struct qosm_interface_qos,
        qos_tnode);

void qosm_interface_qos_init(void)
{
    OVSDB_TABLE_INIT_NO_KEY(Interface_QoS);
    OVSDB_TABLE_MONITOR_F(Interface_QoS, C_VPACK("-", "_version", "status"));
}

struct qosm_interface_qos *qosm_interface_qos_get(ovs_uuid_t *uuid)
{
    struct qosm_interface_qos *qos;

    qos = ds_tree_find(&qosm_interface_qos_list, (void *)uuid->uuid);
    if (qos != NULL)
    {
        return qos;
    }

    /* Allocate a new empty structure */
    qos = calloc(1, sizeof(struct qosm_interface_qos));
    ASSERT(qos != NULL, "Error allocating qosm_interface_qos");

    qos->qos_uuid = *uuid;
    reflink_init(&qos->qos_reflink, "Interface_QoS");
    reflink_set_fn(&qos->qos_reflink, qosm_interface_qos_reflink_fn);

    reflink_init(&qos->qos_interface_queue_reflink, "Interface_QoS.queues");
    reflink_set_fn(&qos->qos_interface_queue_reflink, qosm_interface_qos_queue_reflink_fn);

    ds_tree_insert(&qosm_interface_qos_list, qos, qos->qos_uuid.uuid);

    return qos;
}

void qosm_interface_qos_queue_free(struct qosm_interface_qos *qos)
{
    int qi;

    /* Disconnect current queues reflinks */
    for (qi = 0; qi < qos->qos_interface_queue_len; qi++)
    {
        reflink_disconnect(
                &qos->qos_interface_queue_reflink,
                &qos->qos_interface_queue[qi]->que_reflink);
    }

    free(qos->qos_interface_queue);
    qos->qos_interface_queue_len = 0;
    qos->qos_interface_queue = NULL;
}

void qosm_interface_qos_reflink_fn(reflink_t *ref, reflink_t *sender)
{
    struct qosm_interface_qos *qos = CONTAINER_OF(ref, struct qosm_interface_qos, qos_reflink);

    if (sender != NULL)
    {
        return;
    }

    LOG(DEBUG, "qosm: Interface_QoS reached 0 refcount.");

    qosm_interface_qos_queue_free(qos);

    ds_tree_remove(&qosm_interface_qos_list, qos);

    free(qos);
}

void qosm_interface_qos_queue_reflink_fn(reflink_t *ref, reflink_t *sender)
{
    struct qosm_interface_qos *qos = CONTAINER_OF(ref, struct qosm_interface_qos, qos_interface_queue_reflink);

    if (sender == NULL)
    {
        return;
    }

    /* Just forward the update notification upstream */
    reflink_signal(&qos->qos_reflink);
}

void callback_Interface_QoS(
        ovsdb_update_monitor_t *mon,
        struct schema_Interface_QoS *old,
        struct schema_Interface_QoS *new)
{
    (void)old;

    struct qosm_interface_qos *qos;
    ovs_uuid_t qos_uuid;
    int qi;

    STRSCPY(qos_uuid.uuid, mon->mon_uuid);

    qos = qosm_interface_qos_get(&qos_uuid);
    if (qos == NULL)
    {
        LOG(ERR, "qosm: Error acquiring the Interface_QoS object for uuid %s.", qos_uuid.uuid);
        return;
    }

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            reflink_ref(&qos->qos_reflink, 1);
            break;

        case OVSDB_UPDATE_MODIFY:
            break;

        case OVSDB_UPDATE_DEL:
            reflink_ref(&qos->qos_reflink, -1);
            return;

        default:
            LOG(ERR, "qosm: Interface_QoS monitor error.");
            break;
    }

    qosm_interface_qos_queue_free(qos);

    qos->qos_interface_queue = calloc(new->queues_len, sizeof(struct qosm_interface_queue *));
    ASSERT(qos->qos_interface_queue != NULL, "Error allocating qos_interface_queue");

    /* Rebuild the queues array */
    for (qi = 0; qi < new->queues_len; qi++)
    {
        struct qosm_interface_queue *que;

        que = qosm_interface_queue_get(&new->queues[qi]);
        if (que == NULL)
        {
            LOG(WARN, "qosm: Error acquiring object for Interface_Queue with uuid %s.", new->queues[qi].uuid);
            continue;
        }

        reflink_connect(
                &qos->qos_interface_queue_reflink,
                &que->que_reflink);

        qos->qos_interface_queue[qos->qos_interface_queue_len++] = que;
    }

    /* Signal to listeners that the configuration may have changed */
    reflink_signal(&qos->qos_reflink);
}

bool qosm_interface_qos_set_status(struct qosm_interface_qos *qos, const char *status)
{
    struct schema_Interface_QoS schema_qos;
    int rc;

    /*
     * Update the Interface_QoS:status field
     */
    MEMZERO(schema_qos);
    schema_qos._partial_update = true;
    schema_qos.status_present = true;

    if (status != NULL)
    {
        SCHEMA_SET_STR(schema_qos.status, status);
    }

    rc = ovsdb_table_update_where(
            &table_Interface_QoS,
            ovsdb_tran_cond(OCLM_UUID, "_uuid", OFUNC_EQ, qos->qos_uuid.uuid),
            &schema_qos);
    if (rc <= 0)
    {
        return false;
    }

    return true;
}
