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

#include "schema.h"
#include "ovsdb_table.h"

#include "wano.h"

struct wano_ovs_port
{
    ds_tree_node_t              op_tnode;                   /**< Tree node */
    reflink_t                   op_reflink;                 /**< Structure reflink */
    struct wano_ovs_port_state  op_state;                   /**< Cached table state */
};

static ovsdb_table_t table_Port;

static void callback_Port(
        ovsdb_update_monitor_t *self,
        struct schema_Port *old,
        struct schema_Port *new);

static struct wano_ovs_port *wano_ovs_port_get(const char *ifname);
static reflink_fn_t wano_ovs_port_reflink_fn;
static reflink_fn_t wano_ovs_port_event_reflink_fn;
void wano_ovs_port_event_async_fn(struct ev_loop *loop, ev_async *w, int revent);

static ds_tree_t wano_ovs_port_list = DS_TREE_INIT(
        ds_str_cmp,
        struct wano_ovs_port,
        op_tnode);

/*
 * ===========================================================================
 *  Port table notification infrastructure
 * ===========================================================================
 */

/*
 *
 * Initialize OVSDB monitoring of the Connection_Manager_Uplink table
 */
bool wano_ovs_port_init(void)
{
    /* Register to the Port table */
    OVSDB_TABLE_INIT(Port, name);
    if (!OVSDB_TABLE_MONITOR(Port, true))
    {
        LOG(INFO, "ovs_port: Error monitoring Port table.");
        return false;
    }

    return true;
}

/*
 * Port table local cache management function. Instances are
 * automatically reclaimed when the refcount reaches 0.
 *
 * If a new object is returned, its reference count is 0 therefore must be
 * referenced as soon as this call returns.
 */
struct wano_ovs_port *wano_ovs_port_get(const char *ifname)
{
    struct wano_ovs_port *op;

    op = ds_tree_find(&wano_ovs_port_list, (void *)ifname);
    if (op != NULL)
    {
        return op;
    }

    op = calloc(1, sizeof(struct wano_ovs_port));
    ASSERT(op != NULL, "Error allocating wano_ovs_port");

    STRSCPY(op->op_state.ps_name, ifname);

    reflink_init(&op->op_reflink, "ovs_port");
    reflink_set_fn(&op->op_reflink, wano_ovs_port_reflink_fn);

    ds_tree_insert(&wano_ovs_port_list, op, op->op_state.ps_name);

    return op;
}

/*
 * Reflink callback
 */
void wano_ovs_port_reflink_fn(reflink_t *obj, reflink_t *sender)
{
    struct wano_ovs_port *self;

    /* Handle only refcount 0 events (sender == NULL), ignore the rest. */
    if (sender != NULL)
    {
        LOG(WARN, "ovs_port: Received event from subscriber.");
        return;
    }

    self = CONTAINER_OF(obj, struct wano_ovs_port, op_reflink);
    LOG(DEBUG, "ovs_port: %s: Reached 0 refcount.", self->op_state.ps_name);
    ds_tree_remove(&wano_ovs_port_list, self);
    reflink_fini(&self->op_reflink);
    free(self);
}

/*
 * Port table OVSDB monitor function
 */
void callback_Port(
        ovsdb_update_monitor_t *mon,
        struct schema_Port *old,
        struct schema_Port *new)
{
    const char *ifname;
    struct wano_ovs_port *op;

    ifname = (mon->mon_type == OVSDB_UPDATE_DEL) ? old->name : new->name;

    op = wano_ovs_port_get(ifname);
    if (op == NULL)
    {
        LOG(ERR, "ovs_port: %s: Error acquiring ovs_port object (Port table monitor).",
                ifname);
        return;
    }

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            LOG(DEBUG, "ovs_port: %s: Port NEW", ifname);
            reflink_ref(&op->op_reflink, 1);
            break;

        case OVSDB_UPDATE_MODIFY:
            break;

        case OVSDB_UPDATE_DEL:
            op->op_state.ps_exists = false;
            LOG(DEBUG, "ovs_port: %s: Port DEL", ifname);
            reflink_signal(&op->op_reflink);
            reflink_ref(&op->op_reflink, -1);
            return;

        default:
            LOG(ERR, "ovs_port: %s: Port table monitor update error.", ifname);
            return;
    }

    /*
     * Update cache
     */
    op->op_state.ps_exists = true;

    LOG(DEBUG, "ovs_port: %s: Port ADD/MOD[%d]",
            ifname,
            mon->mon_type == OVSDB_UPDATE_MODIFY);

    reflink_signal(&op->op_reflink);
}

/*
 * Register to Port table events
 */
void wano_ovs_port_event_init(
        wano_ovs_port_event_t *self,
        wano_ovs_port_event_fn_t *fn)
{
    memset(self, 0, sizeof(*self));
    self->pe_event_fn = fn;
}

bool wano_ovs_port_event_start(wano_ovs_port_event_t *self, const char *ifname)
{
    if (self->pe_started)
    {
        return true;
    }

    self->pe_started = true;

    /*
     * Acquire reference to parent object and subscribe to events
     */
    self->pe_ovs_port = wano_ovs_port_get(ifname);
    if (self->pe_ovs_port == NULL)
    {
        LOG(ERR, "ovs_port: %s: Error acquiring wano_ovs_port object.", ifname);
        return false;
    }

    reflink_init(&self->pe_ovs_port_reflink, "ovs_port_event.ovs_port");
    reflink_set_fn(&self->pe_ovs_port_reflink, wano_ovs_port_event_reflink_fn);
    reflink_connect(&self->pe_ovs_port_reflink, &self->pe_ovs_port->op_reflink);

    /* Register the async handler */
    ev_async_init(&self->pe_async, wano_ovs_port_event_async_fn);
    ev_async_start(EV_DEFAULT, &self->pe_async);

    /* Wake-up early so we sync the state */
    ev_async_send(EV_DEFAULT, &self->pe_async);

    return true;
}

void wano_ovs_port_event_stop(wano_ovs_port_event_t *self)
{
    if (!self->pe_started)
    {
        return;
    }
    self->pe_started = false;

    /* Stop async handlers */
    ev_async_stop(EV_DEFAULT, &self->pe_async);

    /* Lose the reference to the parent object */
    reflink_disconnect(&self->pe_ovs_port_reflink, &self->pe_ovs_port->op_reflink);
    reflink_fini(&self->pe_ovs_port_reflink);
    self->pe_ovs_port = NULL;
}

/*
 * Executed when the wano_ovs_port reflink emits a signal
 */
void wano_ovs_port_event_reflink_fn(reflink_t *obj, reflink_t *sender)
{
    wano_ovs_port_event_t *self = CONTAINER_OF(obj, wano_ovs_port_event_t, pe_ovs_port_reflink);

    /* We're not interested in refcount events (sender == NULL) */
    if (sender == NULL)
    {
        return;
    }

    if (!self->pe_started) return;

    /* Wake up async watchers */
    ev_async_send(EV_DEFAULT, &self->pe_async);
}

void wano_ovs_port_event_async_fn(struct ev_loop *loop, ev_async *w, int revent)
{
    (void)loop;
    (void)revent;

    struct wano_ovs_port_event *self = CONTAINER_OF(w, struct wano_ovs_port_event, pe_async);

    self->pe_event_fn(self, &self->pe_ovs_port->op_state);
}
