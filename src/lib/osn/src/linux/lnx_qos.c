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

/*
 * ===========================================================================
 *  Linux QoS module
 *
 *  This module implements a Linux QoS layer using the HTB qdisc. Although HTB
 *  supports hierarchical queue definitions, this implementation supports only
 *  flat queues as that's the current limitation of the OVSDB schema.
 *
 *  The HTB qdisc is attached to the root of the interface, qdisc classes are
 *  created for each queue definition and a fq_codel qdisc is attached to them.
 *
 *  Each queue also adds its own `tc filter` to forward any fw marks to the
 *  specific queue.
 * ===========================================================================
 */
#include <stdlib.h>

#include "const.h"
#include "execsh.h"
#include "log.h"
#include "util.h"

#include "lnx_qos.h"

#define LNX_QOS_MAX             256             /**< Maximum number of lnx_qos_t objects */
#define LNX_QOS_ID_MAX          1               /**< Maximum ID as acllocated by lnx_qos_begin() */
#define LNX_QOS_ID_QUEUE_MAX    255             /**< Maximum ID as allocated by lnx_qos_queue_begin() */
#define LNX_QOS_MARK_BASE       0x44000000      /**< Base mask for calculating the fwmark. The fwmark is calcualted
                                                     from the lnx_qos_t ID, the qos ID and the queue ID */

/* List of free IDs, each lnx_qos_t object allocates its own ID from this array */
uint8_t lnx_qos_obj_id_map[LNX_QOS_MAX + 7 / 8];

/*
 * "tc qdisc del" may return an error if there's no qdisc configured on the
 * interface. Ignore errors.
 */
static char lnx_qos_qdisc_reset[] = _S(tc qdisc del dev "$1" root || true);

static char lnx_qos_qdisc_set[] = _S(tc qdisc add dev "$1" root handle 1: htb);

static char lnx_qos_qdisc_add[] = _S(
        ifname="$1";
        qid="$2";
        mark="$3";
        priority="$4";
        bandwidth="$5";

        tc class add dev "$ifname" \
                parent 1: \
                classid "1:${qid}" \
                htb \
                prio "${priority}" \
                rate "${bandwidth}kbit" \
                burst 15k;

        tc qdisc add dev "$ifname" parent "1:${qid}" fq_codel;

        tc filter add dev "$ifname" \
                protocol ip \
                parent 1: \
                prio 1 \
                handle "${mark}" fw \
                flowid "1:${qid}");


bool lnx_qos_init(lnx_qos_t *self, const char *ifname)
{
    int qid;

    memset(self, 0, sizeof(*self));
    STRSCPY(self->lq_ifname, ifname);

    /* Allocate a unique ID for this object */
    for (qid = 0; qid < LNX_QOS_MAX; qid++)
    {
        int idx = qid >> 3;
        int bit = 1 << (qid & 7);

        if (!(lnx_qos_obj_id_map[idx] & bit))
        {
            lnx_qos_obj_id_map[idx] |= bit;
            break;
        }
    }

    /* Maximum number of lnx_qos_t objects reached, return error */
    if (qid >= LNX_QOS_MAX)
    {
        LOG(ERR, "qos: %s: Maximum number of objects reached.", ifname);
        return false;
    }

    self->lq_obj_id = qid;
    self->lq_qos_id = 1;

    return true;
}

void lnx_qos_fini(lnx_qos_t *self)
{
    int rc;

    /* Clear the bit that corresponds to this object id */
    lnx_qos_obj_id_map[self->lq_obj_id >> 3] &= ~(1 << (self->lq_obj_id & 7));

    LOG(INFO, "qos: %s: Resetting QoS.", self->lq_ifname);
    rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_qos_qdisc_reset, self->lq_ifname);
    if (rc != 0)
    {
        LOG(ERR, "qos: %s: Error resetting QoS.", self->lq_ifname);
        return;
    }

}

bool lnx_qos_apply(lnx_qos_t *self)
{
    if (self->lq_qos_id <= 0 || self->lq_que_id <= 0)
    {
        LOG(ERR, "qos: %s: Invalid QoS configuration.", self->lq_ifname);
        return false;
    }

    return true;
}

bool lnx_qos_begin(lnx_qos_t *self, struct osn_qos_other_config *other_config)
{
    (void)other_config;

    int rc;

    if (self->lq_qos_id > LNX_QOS_ID_MAX)
    {
        LOG(ERR, "qos: %s: Maximum number (%d) of QoS definitions reached.",
                self->lq_ifname, LNX_QOS_ID_MAX);
        return false;
    }

    self->lq_que_id = 1;

    LOG(INFO, "qos: %s: Initializing QoS [%d:%d].",
            self->lq_ifname, self->lq_obj_id, self->lq_qos_id);

    rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_qos_qdisc_reset, self->lq_ifname);
    if (rc != 0)
    {
        LOG(ERR, "qos: %s: Error resetting QoS [%d:%d].",
                self->lq_ifname, self->lq_obj_id, self->lq_qos_id);
        return false;
    }

    rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_qos_qdisc_set, self->lq_ifname);
    if (rc != 0)
    {
        LOG(ERR, "qos: %s: Error setting QoS [%d:%d].",
                self->lq_ifname, self->lq_obj_id, self->lq_qos_id);
        return false;
    }

    return true;
}

bool lnx_qos_end(lnx_qos_t *self)
{
    self->lq_qos_id++;
    return true;
}

bool lnx_qos_queue_begin(
        lnx_qos_t *self,
        int priority,
        int bandwidth,
        const struct osn_qos_other_config *other_config,
        struct osn_qos_queue_status *qqs)
{
    (void)other_config;

    char sqid[C_INT32_LEN];
    char sprio[C_INT32_LEN];
    char sbw[C_INT32_LEN];
    char smark[C_INT32_LEN];
    uint32_t mark;
    int rc;

    memset(qqs, 0, sizeof(*qqs));

    if (self->lq_que_begin)
    {
        LOG(ERR, "qos: %s: Queue nesting not supported.", self->lq_ifname);
        return false;
    }
    self->lq_que_begin = true;

    if (self->lq_que_id > LNX_QOS_ID_QUEUE_MAX)
    {
        LOG(ERR, "qos: Maximum number of queues reached.");
        return false;
    }

    if (bandwidth <= 0)
    {
        LOG(WARN, "qos: %s: Bandwidth set to 1000 kbit/s.", self->lq_ifname);
        bandwidth = 1000;
    }

    mark = LNX_QOS_MARK_BASE |
            (self->lq_obj_id << 16) |
            (self->lq_qos_id << 8) |
            self->lq_que_id;

    snprintf(sqid, sizeof(sqid), "%d", self->lq_que_id);
    snprintf(sprio, sizeof(sprio), "%d", priority);
    snprintf(sbw, sizeof(sbw), "%d", bandwidth);
    snprintf(smark, sizeof(smark), "%u", mark);

    rc = execsh_log(
            LOG_SEVERITY_DEBUG,
            lnx_qos_qdisc_add,
            self->lq_ifname,
            sqid,
            smark,
            sprio,
            sbw);
    if (rc != 0)
    {
        LOG(ERR, "qos: %s: Error applying Queue [%d:%d] configuration.",
                self->lq_ifname, self->lq_qos_id, self->lq_que_id);
        return false;
    }

    LOG(INFO, "qos: %s: Configured Queue [%d:%d:%d]: bandwidth=%d priority=%d",
            self->lq_ifname,
            self->lq_obj_id,
            self->lq_qos_id,
            self->lq_que_id,
            bandwidth,
            priority);

    qqs->qqs_fwmark = mark;
    snprintf(qqs->qqs_class, sizeof(qqs->qqs_class), "1:%d", self->lq_que_id);

    /* Increase the Queue number */
    self->lq_que_id++;

    return true;
}

bool lnx_qos_queue_end(lnx_qos_t *self)
{
    if (!self->lq_que_begin)
    {
        LOG(ERR, "qos: %s: Queue begin/end mismatch.", self->lq_ifname);
        return false;
    }
    self->lq_que_begin = false;

    return true;
}
