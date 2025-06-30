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

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "osn_qdisc.h"
#include "lnx_qdisc.h"

#include "ds_tree.h"
#include "memutil.h"
#include "execsh.h"
#include "const.h"
#include "util.h"
#include "log.h"
#include "ev.h"
#include "evx.h"
#include "os.h"

/* Linux qdisc configuration for an interface */
struct lnx_qdisc_cfg
{
    const char *lq_if_name; /* Interface name */

    ds_tree_t lq_qdiscs; /* qdisc definitions inventory for this interface (osn_qdisc_params) */

    bool lq_applied; /* qdisc definitions applied to system ? */

    osn_qdisc_status_fn_t *lq_status_fn_cb; /* qdisc status notification callback */

    ds_tree_node_t lq_tnode;
};

static char lnx_qdiscs_reset[] = _S(tc qdisc del dev "$1" root || true);

/* Per interface qdisc configurations. (lnx_qdisc_cfg) */
static ds_tree_t intf_qdisc_cfgs = DS_TREE_INIT(ds_str_cmp, struct lnx_qdisc_cfg, lq_tnode);

static bool _lnx_qdisc_cfg_reset(lnx_qdisc_cfg_t *self, bool clear_config);

bool lnx_qdisc_cfg_init(lnx_qdisc_cfg_t *self, const char *if_name)
{
    if (if_name == NULL) return false;

    if (ds_tree_find(&intf_qdisc_cfgs, if_name) != NULL)
    {
        LOG(ERR, "lnx_qdisc: %s: A qdisc configuration object already exists for this interface", if_name);
        return false;
    }

    memset(self, 0, sizeof(*self));

    self->lq_if_name = STRDUP(if_name);

    ds_tree_init(&self->lq_qdiscs, ds_str_cmp, struct osn_qdisc_params, oq_tnode);

    ds_tree_insert(&intf_qdisc_cfgs, self, self->lq_if_name);

    return true;
}

lnx_qdisc_cfg_t *lnx_qdisc_cfg_new(const char *if_name)
{
    lnx_qdisc_cfg_t *self = CALLOC(1, sizeof(lnx_qdisc_cfg_t));

    if (!lnx_qdisc_cfg_init(self, if_name))
    {
        LOG(ERR, "lnx_qdisc: %s: Error creating and initializing lnx_qdisc_cfg_t object", if_name);
        FREE(self);
        return NULL;
    }
    return self;
}

bool lnx_qdisc_cfg_fini(lnx_qdisc_cfg_t *self)
{
    bool rv = true;

    ds_tree_remove(&intf_qdisc_cfgs, self);

    /* Remove all qdisc configurations for this interface: */
    rv &= lnx_qdisc_cfg_reset(self);
    FREE(self->lq_if_name);

    return rv;
}

bool lnx_qdisc_cfg_del(lnx_qdisc_cfg_t *self)
{
    bool rv = true;

    rv &= lnx_qdisc_cfg_fini(self);
    FREE(self);

    return rv;
}

bool lnx_qdisc_cfg_add(lnx_qdisc_cfg_t *self, const struct osn_qdisc_params *qdisc)
{
    struct osn_qdisc_params *qdisc_new = osn_qdisc_params_clone(qdisc);

    qdisc_new->_configured = false; /* Not yet actually configured on the system */

    /* Add this qdisc definition to the inventory for this interface,
     * no actually apply to system yet. */
    ds_tree_insert(&self->lq_qdiscs, qdisc_new, qdisc_new->oq_id);

    LOG(DEBUG, "%s: %s: Adding: %s", __func__, self->lq_if_name, FMT_osn_qdisc_params(*qdisc));

    return true;
}

/* Check if this qdisc is on interface root. */
static bool qdisc_is_on_root(const struct osn_qdisc_params *qdisc)
{
    if (strcmp(qdisc->oq_parent_id, "root") == 0)
    {
        return true;
    }
    return false;
}

/* Find and return the parent of the specified qdisc, or NULL if not found. */
static struct osn_qdisc_params *qdisc_find_parent(lnx_qdisc_cfg_t *self, const struct osn_qdisc_params *qdisc)
{
    struct osn_qdisc_params *parent_qdisc;

    parent_qdisc = ds_tree_find(&self->lq_qdiscs, qdisc->oq_parent_id);

    return parent_qdisc;
}

/* Configure the specified qdisc on the system for this interface. */
static bool system_qdisc_configure(lnx_qdisc_cfg_t *self, struct osn_qdisc_params *qdisc)
{
    char tc_cmd[256];
    int rc;

    if (qdisc->_configured)
    {
        return true;  // noop, if already configured
    }

    LOG(INFO, "%s: Configuring: %s", self->lq_if_name, FMT_osn_qdisc_params(*qdisc));

    if (qdisc->oq_is_class)
    {
        snprintf(
                tc_cmd,
                sizeof(tc_cmd),
                "tc class add dev %s parent %s classid %s %s %s",
                self->lq_if_name,
                qdisc->oq_parent_id,
                qdisc->oq_id,
                qdisc->oq_qdisc,
                qdisc->oq_params);
    }
    else
    {
        snprintf(
                tc_cmd,
                sizeof(tc_cmd),
                "tc qdisc add dev %s parent %s handle %s %s %s",
                self->lq_if_name,
                qdisc->oq_parent_id,
                qdisc->oq_id,
                qdisc->oq_qdisc,
                qdisc->oq_params);
    }

    rc = execsh_log(LOG_SEVERITY_DEBUG, tc_cmd);
    if (rc != 0)
    {
        LOG(ERR,
            "lnx_qdisc: %s: Failed configuring qdisc on system: %s",
            self->lq_if_name,
            FMT_osn_qdisc_params(*qdisc));
        return false;
    }

    qdisc->_configured = true;
    return true;
}

/*
 * Configure qdiscs on the system, in the proper order.
 */
static bool lnx_qdisc_configure(lnx_qdisc_cfg_t *self, struct osn_qdisc_params *qdisc)
{
    /*
     * Note: this is a depth-first configuration. Breadth-first may feel more
     * natural to some. In reality, it really does not matter.
     */

    if (qdisc_is_on_root(qdisc))
    {
        // Configure this qdisc on the system:
        if (!system_qdisc_configure(self, qdisc))
        {
            return false;
        }
    }
    else
    {
        // Identify and find the parent:
        struct osn_qdisc_params *parent_qdisc = qdisc_find_parent(self, qdisc);
        if (parent_qdisc == NULL)
        {
            LOG(ERR, "lnx_qdisc: %s: Cannot find parent for: %s", self->lq_if_name, FMT_osn_qdisc_params(*qdisc));
            return false;
        }

        // First, configure the parent qdisc on the system:
        if (!lnx_qdisc_configure(self, parent_qdisc))
        {
            return false;
        }

        // Then, after the parent configured, configure this child qdisc:
        if (!system_qdisc_configure(self, qdisc))
        {
            return false;
        }
    }
    return true;
}

bool lnx_qdisc_cfg_notify_status_set(lnx_qdisc_cfg_t *self, osn_qdisc_status_fn_t *status_fn_cb)
{
    self->lq_status_fn_cb = status_fn_cb;
    return true;
}

bool lnx_qdisc_cfg_apply(lnx_qdisc_cfg_t *self)
{
    struct osn_qdisc_params *qdisc;

    // First, reset (clear) qdisc on the system for this interface:
    _lnx_qdisc_cfg_reset(self, false);

    LOG(DEBUG, "%s: %s: num qdiscs = %zu", __func__, self->lq_if_name, ds_tree_len(&self->lq_qdiscs));

    self->lq_applied = true;
    ds_tree_foreach (&self->lq_qdiscs, qdisc)
    {
        LOG(DEBUG, "%s: %s: ds_tree_foreach() at: %s", __func__, self->lq_if_name, FMT_osn_qdisc_params(*qdisc));

        if (!lnx_qdisc_configure(self, qdisc))
        {
            LOG(ERR, "lnx_qdisc: %s: Error configuring qdiscs", self->lq_if_name);
            self->lq_applied = false;
            break;
        }

        /* Notify qdisc as successfully applied: */
        if (self->lq_status_fn_cb != NULL)
        {
            struct osn_qdisc_status qdisc_status = {.qs_applied = true, .qs_ctx = qdisc->oq_ctx};

            self->lq_status_fn_cb(&qdisc_status);
        }
    }
    return (self->lq_applied);
}

static bool _lnx_qdisc_cfg_reset(lnx_qdisc_cfg_t *self, bool clear_config)
{
    struct osn_qdisc_params *qdisc;
    struct osn_qdisc_params *tmp;
    int rc;

    self->lq_applied = false;

    if (clear_config)
    {
        /* Remove all qdisc definitions from inventory: */
        ds_tree_foreach_safe (&self->lq_qdiscs, qdisc, tmp)
        {
            ds_tree_remove(&self->lq_qdiscs, qdisc);

            osn_qdisc_params_del(qdisc);
        }
    }

    LOG(INFO, "lnx_qdisc: %s: Resetting qdisc configuration", self->lq_if_name);

    /* The actual removal of the qdiscs from the system: */
    rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_qdiscs_reset, (char *)self->lq_if_name);
    if (rc != 0)
    {
        LOG(ERR, "lnx_qdisc: %s: Error resetting qdiscs", self->lq_if_name);
        return false;
    }
    return true;
}

bool lnx_qdisc_cfg_reset(lnx_qdisc_cfg_t *self)
{
    return _lnx_qdisc_cfg_reset(self, true);
}
