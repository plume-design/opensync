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
#include <net/if.h>

#include <stdlib.h>
#include <unistd.h>

#include "const.h"
#include "ds_tree.h"
#include "execsh.h"
#include "log.h"
#include "util.h"
#include "memutil.h"

#include "lnx_qos.h"

#define LNX_QOS_ID_MAX          1024            /**< Maximum ID as allocated by lnx_qos_begin() */
#define LNX_QOS_MARK_BASE       0x44000000      /**< Base mask for calculating the fwmark. The fwmark is calculated
                                                     from the lnx_qos_t ID, the qos ID and the queue ID */
/*
 * Structure representing an allocated Queue ID
 *
 * Queues with the same tag should return the same queue ID; this structure
 * keeps track of allocated tag <-> qid mappings
 */
struct lnx_qos_qid
{
    int             qi_id;
    char           *qi_tag;
    int             qi_refcnt;
    ds_tree_node_t  qi_qid_tnode;
    ds_tree_node_t  qi_tag_tnode;
};

/* List of free queue IDs; new IDs should be allocated from this pool */
static uint8_t lnx_qos_qid_free[(LNX_QOS_ID_MAX + 7) / 8];

/* Map between tags and queue ID objects */
static ds_tree_t lnx_qos_tag_map = DS_TREE_INIT(ds_str_cmp, struct lnx_qos_qid, qi_tag_tnode);
/* Map between queue ID and their respective objects */
static ds_tree_t lnx_qos_qid_map = DS_TREE_INIT(ds_int_cmp, struct lnx_qos_qid, qi_qid_tnode);

/*
 * "tc qdisc del" may return an error if there's no qdisc configured on the
 * interface. Ignore errors.
 */
static char lnx_qos_qdisc_reset[] = _S(tc qdisc del dev "$1" root || true);

static char lnx_qos_qdisc_set[] = _S(
        tc qdisc add dev "$1" root handle 1: htb default fffe;

        tc class add dev "$1" \
                parent 1: \
                classid "1:fffe" \
                htb \
                prio 0 \
                rate "3.5gbit" \
                burst 15k;

        tc qdisc add dev "$1" parent "1:fffe" fq_codel;
        );

static char lnx_qos_qdisc_add[] = _S(
        ifname="$1";
        qid="$2";
        mark="$3";
        priority="$4";
        bandwidth="$5";
        shared="$6";

        tc class add dev "$ifname" \
                parent 1: \
                classid "1:${qid}" \
                htb \
                prio "${priority}" \
                rate "${bandwidth}kbit" \
                burst 15k \
                ${shared:+shared ${shared}};

        tc qdisc add dev "$ifname" parent "1:${qid}" fq_codel;

        tc filter add dev "$ifname" \
                protocol ip \
                parent 1: \
                prio 1 \
                handle "${mark}" fw \
                flowid "1:${qid}");


static bool lnx_qos_reconfigure(lnx_qos_t *self);
static int lnx_qos_id_get(const char *tag);
static void lnx_qos_id_put(int qid);
static lnx_netlink_fn_t lnx_qos_netlink_fn;

/*
 * ===========================================================================
 *  Public implementation
 * ===========================================================================
 */
bool lnx_qos_init(lnx_qos_t *self, const char *ifname)
{
    memset(self, 0, sizeof(*self));
    STRSCPY(self->lq_ifname, ifname);

    lnx_netlink_init(&self->lq_netlink, lnx_qos_netlink_fn);
    lnx_netlink_set_ifname(&self->lq_netlink, self->lq_ifname);
    lnx_netlink_set_events(&self->lq_netlink, LNX_NETLINK_LINK);

    return true;
}

void lnx_qos_fini(lnx_qos_t *self)
{
    int rc;
    struct lnx_qos_queue *qp;

    lnx_netlink_fini(&self->lq_netlink);

    LOG(INFO, "qos: %s: Resetting QoS.", self->lq_ifname);
    rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_qos_qdisc_reset, self->lq_ifname);
    if (rc != 0)
    {
        LOG(ERR, "qos: %s: Error resetting QoS.", self->lq_ifname);
        return;
    }

    for (qp = self->lq_queue; qp < self->lq_queue_e; qp++)
    {
        lnx_qos_id_put(qp->qq_id);
        free(qp->qq_shared);
    }

    free(self->lq_queue);
}

bool lnx_qos_apply(lnx_qos_t *self)
{
    if (self->lq_qos_begin || self->lq_que_begin)
    {
        LOG(ERR, "qos: %s: qos_end() or queue_end() missing.", self->lq_ifname);
        return false;
    }

    /* Restart netlink monitoring */
    lnx_netlink_stop(&self->lq_netlink);
    self->lq_ifindex = 0;
    lnx_netlink_start(&self->lq_netlink);

    return true;
}

bool lnx_qos_begin(lnx_qos_t *self, struct osn_qos_other_config *other_config)
{
    (void)other_config;

    if (self->lq_qos_begin)
    {
        LOG(ERR, "qos: %s: QoS nesting not supported.", self->lq_ifname);
        return false;
    }
    self->lq_qos_begin = true;

    return true;
}

bool lnx_qos_end(lnx_qos_t *self)
{
    if (!self->lq_qos_begin)
    {
        LOG(ERR, "qos: %s: qos_begin/qos_end mismatch.", self->lq_ifname);
        return false;
    }

    self->lq_qos_begin = false;

    return true;
}

bool lnx_qos_queue_begin(
        lnx_qos_t *self,
        int priority,
        int bandwidth,
        const char *tag,
        const struct osn_qos_other_config *other_config,
        struct osn_qos_queue_status *qqs)
{
    struct lnx_qos_queue *qp;
    uint32_t mark;
    int ii;

    memset(qqs, 0, sizeof(*qqs));

    if (self->lq_que_begin)
    {
        LOG(ERR, "qos: %s: Queue nesting not supported.", self->lq_ifname);
        return false;
    }
    self->lq_que_begin = true;

    if (bandwidth <= 0)
    {
        LOG(WARN, "qos: %s: Bandwidth set to 1000 kbit/s.", self->lq_ifname);
        bandwidth = 1000;
    }

    qp = MEM_APPEND(&self->lq_queue, &self->lq_queue_e, sizeof(struct lnx_qos_queue));
    qp->qq_priority = priority;
    qp->qq_bandwidth = bandwidth;

    qp->qq_id= lnx_qos_id_get(tag);
    if (qp->qq_id < 0)
    {
        LOG(ERR, "qos: %s: Error allocating queue ID for tag %s.",
                self->lq_ifname, tag);
        return false;
    }

    qp->qq_shared = NULL;
    /* Scan otherconfig for the shared options */
    for (ii = 0; ii < other_config->oc_len; ii++)
    {
        if (strcmp(other_config->oc_config[ii].ov_key, "shared") == 0)
        {
            qp->qq_shared = strdup(other_config->oc_config[ii].ov_value);
        }
    }

    mark = LNX_QOS_MARK_BASE | qp->qq_id;
    qqs->qqs_fwmark = mark;
    snprintf(qqs->qqs_class, sizeof(qqs->qqs_class), "1:%d", qp->qq_id);

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

/*
 * ===========================================================================
 *  Helper functions
 * ===========================================================================
 */
bool lnx_qos_reconfigure(lnx_qos_t *self)
{
    struct lnx_qos_queue *qp;
    char sqid[C_INT32_LEN];
    char sprio[C_INT32_LEN];
    char sbw[C_INT32_LEN];
    char smark[C_INT32_LEN];
    uint32_t mark;
    int rc;

    LOG(INFO, "qos: %s: Initializing QoS configuration.", self->lq_ifname);

    rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_qos_qdisc_reset, self->lq_ifname);
    if (rc != 0)
    {
        LOG(ERR, "qos: %s: Error resetting QoS configuration.", self->lq_ifname);
        return false;
    }

    rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_qos_qdisc_set, self->lq_ifname);
    if (rc != 0)
    {
        LOG(ERR, "qos: %s: Error setting QoS configuration.", self->lq_ifname);
        return false;
    }

    for (qp = self->lq_queue; qp < self->lq_queue_e; qp++)
    {
        mark = LNX_QOS_MARK_BASE | qp->qq_id;

        snprintf(sqid, sizeof(sqid), "%d", qp->qq_id);
        snprintf(sprio, sizeof(sprio), "%d", qp->qq_priority);
        snprintf(sbw, sizeof(sbw), "%d", qp->qq_bandwidth);
        snprintf(smark, sizeof(smark), "%u", mark);

        rc = execsh_log(
                LOG_SEVERITY_DEBUG,
                lnx_qos_qdisc_add,
                self->lq_ifname,
                sqid,
                smark,
                sprio,
                sbw,
                qp->qq_shared == NULL ? "" : qp->qq_shared);
        if (rc != 0)
        {
            LOG(ERR, "qos: %s: Error applying Queue configuration [%d].",
                    self->lq_ifname, qp->qq_id);
            return false;
        }

        LOG(INFO, "qos: %s: Configured Queue [%d]: bandwidth=%d priority=%d",
                self->lq_ifname,
                qp->qq_id,
                qp->qq_bandwidth,
                qp->qq_priority);
    }

    return true;
}

int lnx_qos_id_get(const char *tag)
{
    struct lnx_qos_qid *qi;
    int qid;

    if (tag != NULL)
    {
        qi = ds_tree_find(&lnx_qos_tag_map, (void *)tag);
        if (qi != NULL)
        {
            qi->qi_refcnt++;
            return qi->qi_id;
        }
    }

    qi = calloc(1, sizeof(*qi) + ((tag == NULL) ? 0 : strlen(tag) + 1));

    /* Remember the tag, if present */
    if (tag != NULL)
    {
        qi->qi_tag = (char *)qi + sizeof(*qi);
        strcpy(qi->qi_tag, tag);
    }

    /* Allocate the ID; start counting from 1 as the tc class id cannot be 0 */
    for (qid = 1; qid < LNX_QOS_ID_MAX; qid++)
    {
        if ((lnx_qos_qid_free[qid >> 3] & (1 << (qid & 7))) == 0)
        {
            break;
        }
    }

    if (qid >= LNX_QOS_ID_MAX)
    {
        free(qi);
        return -1;
    }

    lnx_qos_qid_free[qid >> 3] |= 1 << (qid & 7);

    qi->qi_refcnt = 1;
    qi->qi_id = qid;

    if (tag != NULL)
    {
        ds_tree_insert(&lnx_qos_tag_map, qi, qi->qi_tag);
    }
    ds_tree_insert(&lnx_qos_qid_map, qi, &qi->qi_id);

    return qi->qi_id;
}

void lnx_qos_id_put(int qid)
{
    struct lnx_qos_qid *qi;

    if (qid <= 0)
    {
        return;
    }

    qi = ds_tree_find(&lnx_qos_qid_map, (void *)&qid);
    if (qi == NULL)
    {
        LOG(ERR, "lnx_qos: Unable to deallocate QID %d.", qid);
        return;
    }

    qi->qi_refcnt--;

    if (qi->qi_refcnt > 0) return;

    if (qi->qi_tag != NULL)
    {
        ds_tree_remove(&lnx_qos_tag_map, qi);
    }
    ds_tree_remove(&lnx_qos_qid_map, qi);

    lnx_qos_qid_free[qi->qi_id >> 3] &= ~(1 << (qi->qi_id & 7));

    free(qi);
}

void lnx_qos_netlink_fn(lnx_netlink_t *nl, uint64_t event, const char *ifname)
{
    (void)event;
    (void)ifname;

    unsigned int ifindex;
    char master_path[C_MAXPATH_LEN];

    struct lnx_qos *self = CONTAINER_OF(nl, struct lnx_qos, lq_netlink);

    ifindex = if_nametoindex(self->lq_ifname);

    /*
     * OVS wipes the qdisc configuration when an interface is added to an OVS
     * bridge. We need to re-apply the configuration when this happens.
     *
     * Adding/removing an interface to/from an OVS bridge generates a netlink
     * event. To check if the interface was added to a bridge, we can simply check
     * if the /sys/class/net/IF/master symbolic link exists.
     */
    snprintf(master_path, sizeof(master_path), "/sys/class/net/%s/master", self->lq_ifname);
    if (access(master_path, F_OK) == 0)
    {
        /* Flip a bit, this will force an interface index change */
        ifindex ^= 0x4000;
    }

    LOG(DEBUG, "qosm: %s: Interface status changed, index %u->%u", self->lq_ifname, self->lq_ifindex, ifindex);

    if (self->lq_ifindex == ifindex)
    {
        LOG(DEBUG, "qos: %s: No interface change.", self->lq_ifname);
    }
    else if (ifindex == 0)
    {
        LOG(NOTICE, "qos: %s: Interface ceased to exist.", self->lq_ifname);
    }
    else
    {
        LOG(NOTICE, "qos: %s: Interface exists. Reconfiguring QoS.", self->lq_ifname);
        if (!lnx_qos_reconfigure(self))
        {
            LOG(ERR, "qos: %s: QoS reconfiguration failed.", self->lq_ifname);
        }
    }

    self->lq_ifindex = ifindex;
}
