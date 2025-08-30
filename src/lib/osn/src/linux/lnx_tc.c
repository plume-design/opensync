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
 *  Linux TC module
 *
 *  This module implements a Linux TC layer using the PRIO and ingress qdisc.
 *  The main purpose of these qdiscs is for filtering ingress and egress
 *  traffic based on various match parameters using flower module.
 *  Once there is a match, the action could be taken on such traffic to either
 *  mirror the traffic or redirect the traffic to a tap interface.
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

#include "lnx_tc.h"

/*
 * "tc qdisc del" may return an error if there's no qdisc configured on the
 * interface. Ignore errors.
 */

static char lnx_tc_qdisc_egress_reset[] = _S(tc qdisc del dev "$1" root || true);

static char lnx_tc_qdisc_ingress_set[] = _S(tc qdisc add dev "$1" ingress handle ffff:fff1);

static char lnx_tc_qdisc_clsact_reset[] = _S(tc qdisc del dev "$1" parent ffff:fff1);

static char lnx_tc_qdisc_clsact_set[] = _S(tc qdisc add dev "$1" clsact);

static char lnx_tc_qdisc_egress_set[] = _S(
        tc qdisc add dev "$1" \
                handle 1:0 \
                root prio;
        tc qdisc add dev "$1" parent 1:1 handle 10: sfq limit 1024;
        tc qdisc add dev "$1" parent 1:2 handle 20: sfq limit 1024;
        tc qdisc add dev "$1" parent 1:3 handle 30: sfq limit 1024;

        );
static char lnx_tc_qdisc_ingress_filter_add[] = _S(
        ifname="$1";
        match="$2";
        action="$3";
        priority="$4";

        tc filter add dev "$ifname" \
                parent ffff: \
                prio ${priority} \
                ${match} \
                ${action});

static char lnx_tc_qdisc_clsact_filter_add[] = _S(
        ifname="$1";
        match="$2";
        action="$3";
        priority="$4";

        tc filter add dev "$ifname" \
                ingress \
                prio ${priority} \
                ${match} \
                ${action});

static char lnx_tc_qdisc_egress_filter_add[] = _S(
        ifname="$1";
        match="$2";
        action="$3";
        priority="$4";

        tc filter add dev "$ifname" \
                parent 1: \
                prio ${priority} \
                ${match} \
                ${action});
static int lnx_tc_filters_cmp(const void *_a, const void *_b)
{
    struct lnx_tc_filter *a = (struct lnx_tc_filter *)_a;
    struct lnx_tc_filter *b = (struct lnx_tc_filter *)_b;

    if (a->ingress == b->ingress &&
        !strcmp(a->match, b->match)) return 0;

    return 1;

}
/*
 * ===========================================================================
 *  Public implementation
 * ===========================================================================
 */

static void lnx_tc_free_filters(lnx_tc_t *self)
{
    struct lnx_tc *lt, *next;
    ds_tree_t *tree = &self->lt_filters;

    if (tree == NULL) return;

    lt = ds_tree_head(tree);
    while (lt != NULL)
    {
        next = ds_tree_next(tree, lt);
        ds_tree_remove(tree, lt);
        FREE(lt);
        lt = next;
    }

    return;
}
bool lnx_tc_init(lnx_tc_t *self, const char *ifname)
{
    memset(self, 0, sizeof(*self));
    self->lt_ifname = STRDUP(ifname);

    ds_tree_init(&self->lt_filters, lnx_tc_filters_cmp,
                 struct lnx_tc_filter, lt_node);

    /*
     * By default, reset egress qdiscs unless reset_egress flag explicitly unconfigured.
     * (Usually in cases where another module resets/sets initial qdiscs)
     */
    self->lt_reset_egress = true;

    return true;
}

void lnx_tc_set_reset_egress(lnx_tc_t *self, bool reset)
{
    self->lt_reset_egress = reset;
}

static bool lnx_tc_reset_if_needed(lnx_tc_t *self)
{
    int rc;

    /*
     * Reset egress qdiscs, unless reset disabled (usually in cases where another
     * module (for instance QoS module) is expected to reset/set initial egress qdiscs).
     */
    if (self->lt_reset_egress)
    {
        LOG(INFO, "tc: %s: Resetting egress", self->lt_ifname);

        rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_tc_qdisc_egress_reset, self->lt_ifname);
        if (rc != 0)
        {
            LOG(INFO, "tc: %s: Error resetting egress TC.", self->lt_ifname);
        }
        rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_tc_qdisc_egress_set, self->lt_ifname);
        if (rc != 0)
        {
            LOG(ERR, "tc: %s: Error Setting egress TC.", self->lt_ifname);
            return false;
        }
    }

    /*
     * Always reset ingress qdiscs. Ingress qdiscs are used only by tc-filters (this module),
     * thus they can always be reset independently of egress qdiscs.
     */
    LOG(INFO, "tc: %s: Resetting clsact/ingress", self->lt_ifname);
    rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_tc_qdisc_clsact_reset, self->lt_ifname);
    if (rc != 0)
    {
        LOG(INFO, "tc: %s: Error resetting clsact/ingress TC or nothing to reset.", self->lt_ifname);
    }
    rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_tc_qdisc_clsact_set, self->lt_ifname);
    if (rc != 0)
    {
        LOG(INFO, "tc: %s: Error setting clsact TC, setting ingress as fallback.", self->lt_ifname);
        rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_tc_qdisc_ingress_set, self->lt_ifname);
        if (rc != 0)
        {
            LOG(ERR, "tc: %s: Error setting ingress TC.", self->lt_ifname);
            return false;
        }
    }

    return true;
}

void lnx_tc_fini(lnx_tc_t *self)
{
    /* Reset ingress, and egress if needed: */
    if (!lnx_tc_reset_if_needed(self))
    {
        LOG(ERR, "tc: %s: Failed at tc-filter reset-if-needed stage", self->lt_ifname);
        return;
    }

    lnx_tc_free_filters(self);
    FREE(self->lt_ifname);
}

bool lnx_tc_begin(lnx_tc_t *self)
{
    if (self->lt_tc_begin)
    {
        LOG(ERR, "tc: %s: tc_begin/tc_end mismatch.", self->lt_ifname);
        return false;
    }

    LOG(INFO, "tc: %s Initializing TC-filter configuration.", self->lt_ifname);

    /* Reset ingress, and egress if needed: */
    if (!lnx_tc_reset_if_needed(self))
    {
        LOG(ERR, "tc: %s: Failed at tc-filter reset-if-needed stage", self->lt_ifname);
        return false;
    }

    self->lt_tc_begin = true;

    return true;
}

bool lnx_tc_end(lnx_tc_t *self)
{
    if (!self->lt_tc_begin)
    {
        LOG(ERR, "tc: %s: tc_begin/tc_end mismatch.", self->lt_ifname);
        return false;
    }

    self->lt_tc_begin = false;

    return true;
}

bool lnx_tc_filter_begin(lnx_tc_t *self, bool ingress, int priority, const char  *match, const char  *action)
{
    struct lnx_tc_filter *key;
    struct lnx_tc_filter *lkp;

    if (self->lt_tc_filter_begin)
    {
        LOG(ERR, "tc: %s: tc_begin/tc_end mismatch.", self->lt_ifname);
        return false;
    }
    self->lt_tc_filter_begin = true;

    key = MALLOC(sizeof(struct lnx_tc_filter));

    key->ingress = ingress;
    key->priority = priority;
    key->match = STRDUP(match);
    if (!strcmp(action, "pass")) key->action = STRDUP("action classid 1:");
    else key->action = STRDUP(action);
    lkp = ds_tree_find(&self->lt_filters, key);
    if (lkp == NULL)
    {
        ds_tree_insert(&self->lt_filters, key, key);
    }
    else
    {
        LOG(INFO, "tc: %s: filter already exists.", self->lt_ifname);
        FREE(key->match);
        FREE(key->action);
        FREE(key);
    }

    return true;
}

bool lnx_tc_filter_end(lnx_tc_t *self)
{
    if (!self->lt_tc_filter_begin)
    {
        LOG(ERR, "tc: %s: TC begin/end mismatch.", self->lt_ifname);
        return false;
    }
    self->lt_tc_filter_begin = false;

    return true;
}

bool lnx_tc_apply(lnx_tc_t *self)
{
    struct lnx_tc_filter *tf;
    int rc;
    char priority[C_INT32_LEN];

    if (self->lt_tc_begin || self->lt_tc_filter_begin)
    {
        LOG(ERR, "tc: %s: tc_end() or tc_filter_end() missing.", self->lt_ifname);
        return false;
    }

    LOG(INFO, "tc: %s: Initializing TC configuration.", self->lt_ifname);
    ds_tree_foreach(&self->lt_filters, tf)
    {
        snprintf(priority, sizeof(priority), "%d", tf->priority);
        if (tf->ingress)
        {
           rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_tc_qdisc_clsact_filter_add,
                           self->lt_ifname,
                           tf->match,
                           tf->action ? tf->action : "",
                           priority);
           if (rc != 0)
           {
                rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_tc_qdisc_ingress_filter_add,
                               self->lt_ifname,
                               tf->match,
                               tf->action ? tf->action : "",
                               priority);
                if (rc != 0)
                {
                    LOG(ERR, "tc: %s: Error setting TC filter configuration.", self->lt_ifname);
                }
           }
        }
        else
        {
           rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_tc_qdisc_egress_filter_add,
                           self->lt_ifname,
                           tf->match,
                           tf->action ? tf->action : "",
                           priority);
           if (rc != 0)
           {
                LOG(ERR, "tc: %s: Error setting TC filter configuration.", self->lt_ifname);
           }
        }
    }

    return true;
}
