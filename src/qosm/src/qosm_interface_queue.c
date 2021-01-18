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

static void callback_Interface_Queue(
        ovsdb_update_monitor_t *mon,
        struct schema_Interface_Queue *old,
        struct schema_Interface_Queue *new);

static ovsdb_table_t table_Interface_Queue;
static ovsdb_table_t table_Openflow_Tag;
static reflink_fn_t qosm_interface_queue_reflink_fn;
static void qosm_interface_queue_free_other_config(struct qosm_interface_queue *que);
static bool qosm_interface_queue_set_oftag(const char *oftag, const char *value);
static bool qosm_interface_queue_update_tags(struct qosm_interface_queue *que, bool status, struct osn_qos_queue_status *qqs);

static ds_tree_t qosm_interface_queue_list = DS_TREE_INIT(
        ds_str_cmp,
        struct qosm_interface_queue,
        que_tnode);

void qosm_interface_queue_init(void)
{
    OVSDB_TABLE_INIT_NO_KEY(Interface_Queue);
    OVSDB_TABLE_MONITOR_F(Interface_Queue, C_VPACK("-", "_version", "mark"));
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag);
}

struct qosm_interface_queue *qosm_interface_queue_get(ovs_uuid_t *uuid)
{
    struct qosm_interface_queue *que;

    que = ds_tree_find(&qosm_interface_queue_list, (void *)uuid->uuid);
    if (que != NULL)
    {
        return que;
    }

    /* Allocate a new empty structure */
    que = calloc(1, sizeof(struct qosm_interface_queue));
    ASSERT(que != NULL, "Error allocating qosm_interface_queue");

    que->que_uuid = *uuid;
    reflink_init(&que->que_reflink, "Interface_Queue");
    reflink_set_fn(&que->que_reflink, qosm_interface_queue_reflink_fn);

    ds_tree_insert(&qosm_interface_queue_list, que, que->que_uuid.uuid);

    return que;
}

void qosm_interface_queue_reflink_fn(reflink_t *ref, reflink_t *sender)
{
    struct qosm_interface_queue *que = CONTAINER_OF(ref, struct qosm_interface_queue, que_reflink);

    if (sender != NULL)
    {
        return;
    }

    LOG(DEBUG, "qosm: Interface_Queue reached 0 refcount.");

    if (!qosm_interface_queue_update_tags(que, false, NULL))
    {
        LOG(ERR, "qosm: Error deleting associated Openflow_Tag row with tag: %s",
                que->que_tag);
    }

    qosm_interface_queue_free_other_config(que);

    ds_tree_remove(&qosm_interface_queue_list, que);

    free(que);
}

void qosm_interface_queue_free_other_config(struct qosm_interface_queue *que)
{
    int ci;

    /* Free other config */
    for (ci = 0; ci < que->que_other_config.oc_len; ci++)
    {
        free(que->que_other_config.oc_config[ci].ov_key);
        free(que->que_other_config.oc_config[ci].ov_value);
    }
    free(que->que_other_config.oc_config);
    que->que_other_config.oc_len = 0;
    que->que_other_config.oc_config = NULL;
}

void callback_Interface_Queue(
        ovsdb_update_monitor_t *mon,
        struct schema_Interface_Queue *old,
        struct schema_Interface_Queue *new)
{
    (void)old;

    struct qosm_interface_queue *que;
    ovs_uuid_t que_uuid;
    int ci;

    STRSCPY(que_uuid.uuid, mon->mon_uuid);

    que = qosm_interface_queue_get(&que_uuid);
    if (que == NULL)
    {
        LOG(ERR, "qosm: Error acquiring the Interface_Queue object for uuid %s.", que_uuid.uuid);
        return;
    }

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            reflink_ref(&que->que_reflink, 1);
            break;

        case OVSDB_UPDATE_MODIFY:
            if (!qosm_interface_queue_update_tags(que, false, NULL))
            {
                LOG(DEBUG, "qosm: Error deleting %s tags.",
                        que->que_tag);
            }
            break;

        case OVSDB_UPDATE_DEL:
            reflink_ref(&que->que_reflink, -1);
            return;

        default:
            LOG(ERR, "qosm: Interface_Queue monitor error.");
            break;
    }

    que->que_priority = new->priority;
    que->que_bandwidth = new->bandwidth;
    STRSCPY(que->que_tag, new->tag);

    qosm_interface_queue_free_other_config(que);

    /* Rebuild other_config */
    que->que_other_config.oc_config = calloc(new->other_config_len, sizeof(struct osn_qos_oc_kv_pair));
    ASSERT(que->que_other_config.oc_config != NULL, "Error allocating queue other_config");

    que->que_other_config.oc_len = new->other_config_len;
    for (ci = 0; ci < new->other_config_len; ci++)
    {
        que->que_other_config.oc_config[ci].ov_key = strdup(new->other_config_keys[ci]);
        ASSERT(que->que_other_config.oc_config[ci].ov_key != NULL, "Error allocating other_config key");
        que->que_other_config.oc_config[ci].ov_value = strdup(new->other_config[ci]);
        ASSERT(que->que_other_config.oc_config[ci].ov_value != NULL, "Error allocating other_config value");
    }

    /* Signal to listeners that the configuration may have changed */
    reflink_signal(&que->que_reflink);
}

bool qosm_interface_queue_set_status(
        struct qosm_interface_queue *que,
        bool status,
        struct osn_qos_queue_status *qqs)
{
    struct schema_Interface_Queue schema_queue;

    bool retval = true;

    /*
     * Update the Interface_Queue:mark field
     */
    MEMZERO(schema_queue);
    schema_queue._partial_update = true;
    schema_queue.mark_present = true;

    if (status && qqs->qqs_fwmark != 0)
    {
        SCHEMA_SET_INT(schema_queue.mark, qqs->qqs_fwmark);
    }

    if (ovsdb_table_update_where(
            &table_Interface_Queue,
            ovsdb_tran_cond(OCLM_UUID, "_uuid", OFUNC_EQ, que->que_uuid.uuid),
            &schema_queue) <= 0)
    {
        LOG(DEBUG, "qosm: Error updating status for queue: %s", que->que_tag);
        retval = false;
    }

    if (!qosm_interface_queue_update_tags(que, status, qqs))
    {
        LOG(DEBUG, "qosm: Error updating tags for queue: %s", que->que_tag);
        retval = false;
    }

    return retval;
}

/*
 * Update or delete Openflow tags for the queue @p que
 */
bool qosm_interface_queue_update_tags(
        struct qosm_interface_queue *que,
        bool status,
        struct osn_qos_queue_status *qqs)
{
    struct schema_Openflow_Tag *ptag;
    char class_tag[sizeof(ptag->name)];
    char svalue[sizeof(ptag->device_value[0])];

    bool retval = true;

    svalue[0] = '\0';
    if (status && qqs->qqs_fwmark != 0)
    {
        snprintf(svalue, sizeof(svalue), "%u", qqs->qqs_fwmark);
    }

    if (!qosm_interface_queue_set_oftag(
                que->que_tag,
                (svalue[0] == '\0') ? NULL : svalue))
    {
        retval = false;
    }

    snprintf(class_tag, sizeof(class_tag), "%s_class", que->que_tag);
    if (!qosm_interface_queue_set_oftag(
                class_tag,
                (status && qqs->qqs_class[0] != '\0') ? qqs->qqs_class : NULL))
    {
        retval = false;
    }

    return retval;
}

/*
 * Set or delete a single tag in the Openflow_Tag table
 */
bool qosm_interface_queue_set_oftag(
        const char *oftag,
        const char *value)
{
    struct schema_Openflow_Tag schema_oftag;
    int rc;

    if (value == NULL)
    {
        /* Delete case */
        rc = ovsdb_table_delete_where(
                &table_Openflow_Tag,
                ovsdb_tran_cond(OCLM_STR, "name", OFUNC_EQ, oftag));

        return rc >= 0;
    }

    /* Upseert case */
    MEMZERO(schema_oftag);
    schema_oftag._partial_update = true;
    schema_oftag.device_value_present = true;
    SCHEMA_SET_STR(schema_oftag.name, oftag);
    schema_oftag.device_value_len = 1;
    STRSCPY(schema_oftag.device_value[0], value);

    return ovsdb_table_upsert_where(
            &table_Openflow_Tag,
            ovsdb_tran_cond(OCLM_STR, "name", OFUNC_EQ, oftag),
            &schema_oftag,
            true);
}
