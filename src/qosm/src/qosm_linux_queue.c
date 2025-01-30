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
#include "memutil.h"

static void callback_Linux_Queue(
        ovsdb_update_monitor_t *mon,
        struct schema_Linux_Queue *old,
        struct schema_Linux_Queue *new);

static ovsdb_table_t table_Linux_Queue;

static reflink_fn_t qosm_linux_queue_reflink_fn;

static ds_tree_t qosm_linux_queue_list = DS_TREE_INIT(ds_str_cmp, struct qosm_linux_queue, que_tnode);

void qosm_linux_queue_init(void)
{
    OVSDB_TABLE_INIT_NO_KEY(Linux_Queue);
    OVSDB_TABLE_MONITOR_F(Linux_Queue, C_VPACK("-", "_version", "mark"));
}

struct qosm_linux_queue *qosm_linux_queue_get(ovs_uuid_t *uuid)
{
    struct qosm_linux_queue *que;

    que = ds_tree_find(&qosm_linux_queue_list, (void *)uuid->uuid);
    if (que != NULL)
    {
        return que;
    }

    /* Allocate a new empty structure */
    que = CALLOC(1, sizeof(struct qosm_linux_queue));

    que->que_uuid = *uuid;
    reflink_init(&que->que_reflink, "Linux_Queue");
    reflink_set_fn(&que->que_reflink, qosm_linux_queue_reflink_fn);

    ds_tree_insert(&qosm_linux_queue_list, que, que->que_uuid.uuid);

    return que;
}

void qosm_linux_queue_reflink_fn(reflink_t *ref, reflink_t *sender)
{
    struct qosm_linux_queue *que = CONTAINER_OF(ref, struct qosm_linux_queue, que_reflink);

    if (sender != NULL)
    {
        return;
    }

    LOG(DEBUG, "qosm: Linux_Queue reached 0 refcount.");

    ds_tree_remove(&qosm_linux_queue_list, que);

    FREE(que);
}

static void callback_Linux_Queue(
        ovsdb_update_monitor_t *mon,
        struct schema_Linux_Queue *old,
        struct schema_Linux_Queue *new)
{
    (void)old;

    struct qosm_linux_queue *que;
    ovs_uuid_t que_uuid;

    STRSCPY(que_uuid.uuid, mon->mon_uuid);

    que = qosm_linux_queue_get(&que_uuid);
    if (que == NULL)
    {
        LOG(ERR, "qosm: Error acquiring the Linux_Queue object for uuid %s.", que_uuid.uuid);
        return;
    }

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            reflink_ref(&que->que_reflink, 1);
            break;

        case OVSDB_UPDATE_MODIFY:
            break;

        case OVSDB_UPDATE_DEL:
            reflink_ref(&que->que_reflink, -1);
            return;

        default:
            LOG(ERR, "qosm: Linux_Queue monitor error.");
            break;
    }

    /* Copy values from OVSDB to qosm_linux_queue: */
    que->que_is_class = strcmp(new->type, "class") == 0 ? true : false;
    STRSCPY(que->que_qdisc, new->name);
    STRSCPY(que->que_id, new->id);
    STRSCPY(que->que_parent_id, new->parent_id);
    STRSCPY(que->que_params, new->params);

    /* Signal to listeners that the configuration may have changed */
    reflink_signal(&que->que_reflink);
}
