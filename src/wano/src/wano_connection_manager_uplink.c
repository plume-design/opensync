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

struct wano_connmgr_uplink
{
    char                cmu_ifname[C_IFNAME_LEN];   /* Interface name */
    ds_tree_node_t      cmu_tnode;                  /* Tree node */
    reflink_t           cmu_reflink;                /* Structure reflink */
    struct wano_connmgr_uplink_state
                        cmu_state;                  /* Current row state */
    bool                cmu_state_valid;            /* True if state is valid */
};

static ovsdb_table_t table_Connection_Manager_Uplink;

static void callback_Connection_Manager_Uplink(
        ovsdb_update_monitor_t *self,
        struct schema_Connection_Manager_Uplink *old,
        struct schema_Connection_Manager_Uplink *new);

static struct wano_connmgr_uplink *wano_connmgr_uplink_get(const char *ifname);
static reflink_fn_t wano_connmgr_uplink_reflink_fn;
static reflink_fn_t wano_connmgr_uplink_event_cmu_reflink_fn;
void wano_connmgr_uplink_event_async_fn(struct ev_loop *loop, ev_async *w, int revent);

static ds_tree_t wano_connmgr_uplink_list = DS_TREE_INIT(
        ds_str_cmp,
        struct wano_connmgr_uplink,
        cmu_tnode);
/*
 * ===========================================================================
 *  Connection_Manager_Uplink change notification infrastructure
 * ===========================================================================
 */
/*
 * Initialize OVSDB monitoring of the Connection_Manager_Uplink table
 */
bool wano_connmgr_uplink_init(void)
{
    /* Register to Connection_Manager_Uplink */
    OVSDB_TABLE_INIT(Connection_Manager_Uplink, if_name);
    if (!OVSDB_TABLE_MONITOR(Connection_Manager_Uplink, true))
    {
        LOG(INFO, "connmgr_uplink: Error monitoring Connection_Manager_Uplink");
        return false;
    }

    return true;
}

/*
 * Connection_Manager_Uplink local cache management function. Instances are
 * automatically reclaimed when the refcount reaches 0.
 *
 * If a new object is returned, its reference count is 0 therefore must be
 * referenced as soon as this call returns.
 */
struct wano_connmgr_uplink *wano_connmgr_uplink_get(const char *ifname)
{
    struct wano_connmgr_uplink *cmu;

    cmu = ds_tree_find(&wano_connmgr_uplink_list, (void *)ifname);
    if (cmu != NULL)
    {
        return cmu;
    }

    cmu = calloc(1, sizeof(struct wano_connmgr_uplink));
    STRSCPY(cmu->cmu_ifname, ifname);

    reflink_init(&cmu->cmu_reflink, "wano_connmgr_uplink.cmu_reflink");
    reflink_set_fn(&cmu->cmu_reflink, wano_connmgr_uplink_reflink_fn);

    ds_tree_insert(&wano_connmgr_uplink_list, cmu, cmu->cmu_ifname);

    return cmu;
}

/*
 * Reflink callback
 */
void wano_connmgr_uplink_reflink_fn(reflink_t *obj, reflink_t *sender)
{
    struct wano_connmgr_uplink *self;

    /* Received event from subscriber? */
    if (sender != NULL)
    {
        LOG(WARN, "connmgr_uplink: Received event from subscriber.");
        return;
    }

    self = CONTAINER_OF(obj, struct wano_connmgr_uplink, cmu_reflink);
    LOG(DEBUG, "connmgr_uplink: Reached 0 refcount: %s", self->cmu_state.if_name);
    ds_tree_remove(&wano_connmgr_uplink_list, self);
    reflink_fini(&self->cmu_reflink);
    free(self);
}


/**
 * Connection_Manager_Uplink OVSDB monitor function
 */
void callback_Connection_Manager_Uplink(
        ovsdb_update_monitor_t *mon,
        struct schema_Connection_Manager_Uplink *old,
        struct schema_Connection_Manager_Uplink *new)
{
    const char *ifname;
    struct wano_connmgr_uplink *cmu;

    ifname = (mon->mon_type == OVSDB_UPDATE_DEL) ? old->if_name : new->if_name;

    cmu = wano_connmgr_uplink_get(ifname);
    if (cmu == NULL)
    {
        LOG(ERR, "connmgr_uplink: %s: Error acquiring connmgr_uplink object (Connection_Manager_Uplink monitor).",
                ifname);
        return;
    }

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            LOG(DEBUG, "connmgr_uplink: %s: Connection_Manager_Uplink NEW", ifname);
            reflink_ref(&cmu->cmu_reflink, 1);
            break;

        case OVSDB_UPDATE_MODIFY:
            break;

        case OVSDB_UPDATE_DEL:
            cmu->cmu_state_valid = false;
            LOG(DEBUG, "connmgr_uplink: %s: Connection_Manager_Uplink DEL", ifname);
            /* Dereference inet_state and return */
            reflink_ref(&cmu->cmu_reflink, -1);
            return;

        default:
            LOG(ERR, "connmgr_uplink: %s: Connection_Manager_Uplink monitor update error.", ifname);
            return;
    }

    /*
     * Update cache
     */
    cmu->cmu_state_valid = true;
    cmu->cmu_state.has_L2 = new->has_L2_exists && new->has_L2 ? true : false;
    cmu->cmu_state.has_L3 = new->has_L3_exists && new->has_L3 ? true : false;

    STRSCPY(cmu->cmu_state.if_name, new->if_name);

    if (new->bridge_exists)
    {
        STRSCPY(cmu->cmu_state.bridge, new->bridge);
    }
    else
    {
        cmu->cmu_state.bridge[0] = '\0';
    }

    LOG(DEBUG, "connmgr_uplink: %s: Connection_Manager_Uplink ADD/MOD[%d] has_L2=%d has_l3=%d bridge=%s",
            ifname,
            mon->mon_type == OVSDB_UPDATE_MODIFY,
            cmu->cmu_state.has_L2,
            cmu->cmu_state.has_L3,
            cmu->cmu_state.bridge[0] == '\0' ? "(not set)" : cmu->cmu_state.bridge);

    reflink_signal(&cmu->cmu_reflink);
}

/*
 * Register to Connection_Manager_Uplink events
 */
void wano_connmgr_uplink_event_init(
        wano_connmgr_uplink_event_t *self,
        wano_connmgr_uplink_event_fn_t *fn)
{
    memset(self, 0, sizeof(*self));

    self->ce_event_fn = fn;
}

bool wano_connmgr_uplink_event_start(wano_connmgr_uplink_event_t *self, const char *ifname)
{
    if (self->ce_started)
    {
        return true;
    }

    self->ce_started = true;

    /*
     * Acquire reference to parent object and subscribe to events
     */
    self->ce_cmu = wano_connmgr_uplink_get(ifname);
    if (self->ce_cmu == NULL)
    {
        LOG(ERR, "inet_state: %s: Error acquiring wano_connmgr_uplink object.", ifname);
        return false;
    }

    reflink_init(&self->ce_cmu_reflink, "ce_cmu_reflink");
    reflink_set_fn(&self->ce_cmu_reflink, wano_connmgr_uplink_event_cmu_reflink_fn);
    reflink_connect(&self->ce_cmu_reflink, &self->ce_cmu->cmu_reflink);

    /* Register the async handler */
    ev_async_init(&self->ce_async, wano_connmgr_uplink_event_async_fn);
    ev_async_start(EV_DEFAULT, &self->ce_async);

    /* Wake-up early so we sync the state */
    ev_async_send(EV_DEFAULT, &self->ce_async);

    return true;
}

void wano_connmgr_uplink_event_stop(wano_connmgr_uplink_event_t *self)
{
    if (!self->ce_started)
    {
        return;
    }
    self->ce_started = false;

    /* Stop async handlers */
    ev_async_stop(EV_DEFAULT, &self->ce_async);

    /* Lose the reference to the parent object */
    reflink_disconnect(&self->ce_cmu_reflink, &self->ce_cmu->cmu_reflink);
    reflink_fini(&self->ce_cmu_reflink);

    self->ce_cmu = NULL;
}

/*
 * Executed when the wano_connmgr_uplink reflink emits a signal
 */
void wano_connmgr_uplink_event_cmu_reflink_fn(reflink_t *obj, reflink_t *sender)
{
    wano_connmgr_uplink_event_t *self = CONTAINER_OF(obj, wano_connmgr_uplink_event_t, ce_cmu_reflink);

    if (sender == NULL)
    {
        /* We're not interested in refcount events */
        return;
    }

    if (!self->ce_started) return;

    /* Wake up async watchers */
    ev_async_send(EV_DEFAULT, &self->ce_async);
}

void wano_connmgr_uplink_event_async_fn(struct ev_loop *loop, ev_async *w, int revent)
{
    (void)loop;
    (void)revent;

    struct wano_connmgr_uplink_event *self = CONTAINER_OF(w, struct wano_connmgr_uplink_event, ce_async);

    if (self->ce_cmu->cmu_state_valid)
    {
        self->ce_event_fn(self, &self->ce_cmu->cmu_state);
    }
}

/*
 * ===========================================================================
 *  Connection_Manager_Uplink table manipulation functions
 * ===========================================================================
 */
bool wano_connmgr_uplink_flush(void)
{
    int rc;

    ovsdb_table_t table_Connection_Manager_Uplink;
    OVSDB_TABLE_INIT(Connection_Manager_Uplink, if_name);

    /* Passing an empty array as the where statement effectively deletes all rows */
    rc = ovsdb_table_delete_where(&table_Connection_Manager_Uplink, json_array());
    return rc >= 0;
}

bool wano_connmgr_uplink_delete(const char *ifname)
{
    int rc;

    ovsdb_table_t table_Connection_Manager_Uplink;

    OVSDB_TABLE_INIT(Connection_Manager_Uplink, if_name);

    rc = ovsdb_table_delete_simple(
            &table_Connection_Manager_Uplink,
            "if_name",
            ifname);

    return rc > 0;
}

bool wano_connmgr_uplink_update(
        const char *ifname,
        struct wano_connmgr_uplink_args *args)
{
    ovsdb_table_t table_Connection_Manager_Uplink;
    struct schema_Connection_Manager_Uplink conn_up;

    memset(&conn_up, 0, sizeof(conn_up));
    conn_up._partial_update = true;

    SCHEMA_SET_STR(conn_up.if_name, ifname);

    if (args->if_type != NULL)
    {
        SCHEMA_SET_STR(conn_up.if_type, args->if_type);
    }

    if (args->priority != 0)
    {
        SCHEMA_SET_INT(conn_up.priority, args->priority);
    }

    if (args->has_L2 == WANO_TRI_TRUE)
    {
        SCHEMA_SET_INT(conn_up.has_L2, true);
    }
    else if (args->has_L2 == WANO_TRI_FALSE)
    {
        SCHEMA_SET_INT(conn_up.has_L2, false);
    }

    if (args->has_L3 == WANO_TRI_TRUE)
    {
        SCHEMA_SET_INT(conn_up.has_L3, true);
    }
    else if (args->has_L3 == WANO_TRI_FALSE)
    {
        SCHEMA_SET_INT(conn_up.has_L3, false);
    }

    if (args->loop == WANO_TRI_TRUE)
    {
        SCHEMA_SET_INT(conn_up.loop, true);
    }
    else if (args->loop == WANO_TRI_FALSE)
    {
        SCHEMA_SET_INT(conn_up.loop, false);
    }

    OVSDB_TABLE_INIT(Connection_Manager_Uplink, if_name);

    return ovsdb_table_upsert_simple(
            &table_Connection_Manager_Uplink,
            "if_name",
            (char *)ifname,
            &conn_up,
            false);
}
