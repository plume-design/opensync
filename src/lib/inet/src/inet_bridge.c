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

#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "inet_base.h"
#include "inet_bridge.h"
#include "inet_port.h"
#include "log.h"
#include "memutil.h"
#include "util.h"

#define INET_BRIDGE_DEBOUNCE           1                 /* Configuration delay, in seconds */
static void __inet_bridge_reapply(EV_P_ ev_debounce *w, int revent);
bool inet_bridge_service_BRIDGE_PORT(inet_bridge_t *self, bool enable);

static void inet_bridge_release_ports(ds_tree_t *tree)
{
    struct inet_port *port_node;
    struct inet_port *remove;

    port_node = ds_tree_head(tree);
    while (port_node)
    {
        remove = port_node;
        port_node = ds_tree_next(tree, port_node);
        ds_tree_remove(tree, remove);
        port_node->in_bridge = NULL;
    }
}

void __inet_bridge_reapply(EV_P_ ev_debounce *w, int revent)
{
    inet_bridge_t *bridge;
    (void)revent;

    bridge = CONTAINER_OF(w, inet_bridge_t, in_br_debounce);

    LOGD("%s: debounce called for bridge %s", __func__, bridge->in_br_ifname);

    inet_bridge_service_BRIDGE_PORT(bridge, true);

}

bool inet_bridge_fini(inet_bridge_t *self)
{
    bool retval = true;

    LOGD("%s(): bridge_fini() invoked !", __func__);

    if (!inet_base_fini(&self->base))
    {
        LOG(ERR, "%s(): Error shutting down base class", __func__);
        retval = false;
    }

    /* Stop any active debounce timers */
    ev_debounce_stop(EV_DEFAULT, &self->in_br_debounce);

    inet_bridge_release_ports(&self->in_br_port_list);
    /* delete the bridge */
    osn_bridge_del(self->in_br_ifname);

    return retval;
}

bool inet_bridge_service_BRIDGE(inet_bridge_t *self, bool enable)
{
    if (self->in_br_add == true)
    {
        return osn_bridge_create(self->in_br_ifname);
    }
    else
    {
        return osn_bridge_del(self->in_br_ifname);
    }
}

bool inet_bridge_service_BRIDGE_PORT(inet_bridge_t *self, bool enable)
{
    struct inet_port *current_port;
    struct inet_port *next_port;
    struct inet_port *remove_port;
    bool status = false;

    current_port = ds_tree_head(&self->in_br_port_list);

    while (current_port)
    {
        next_port = ds_tree_next(&self->in_br_port_list, current_port);
        remove_port = current_port;

        /* if current port is already configured, skip and continue */
        if (current_port->in_port_configured)
        {
            current_port = next_port;
            continue;
        }

        /*  skip adding self to bridge. mark the port as configured */
        if (strcmp(self->in_br_ifname, current_port->in_port_ifname) == 0)
        {
            LOGD("%s(): skip adding self to bridge %s", __func__,
                 self->in_br_ifname);
            current_port->in_port_configured = true;
            current_port = next_port;
            continue;
        }

        /* check if the port needs to be added or removed from bridge */
        if (current_port->in_add == true)
        {
            LOGD("%s(): adding port %s to bridge %s", __func__,
                 current_port->in_port_ifname, self->in_br_ifname);

            status = osn_bridge_add_port(self->in_br_ifname, current_port->in_port_ifname);
            if (!status)
            {
                LOGD("%s(): failed to add port %s to bridge %s", __func__,
                     current_port->in_port_ifname, self->in_br_ifname);

                /* reapply this change later */
                ev_debounce_start(EV_DEFAULT, &self->in_br_debounce);

                current_port = next_port;
                continue;
            }
            status = osn_bridge_set_hairpin(current_port->in_port_ifname,
                                            current_port->in_port_hairpin);
            if (!status)
            {
                LOGD("%s(): failed to set hairpin for port %s in bridge %s", __func__,
                     current_port->in_port_ifname, self->in_br_ifname);
                current_port = next_port;
                continue;
            }
        }
        else
        {
            LOGD("%s(): deleting port %s from bridge %s", __func__,
                 current_port->in_port_ifname, self->in_br_ifname);
            status = osn_bridge_del_port(self->in_br_ifname, current_port->in_port_ifname);
            if (!status)
            {
                LOGD("%s(): failed to add port %s to bridge %s", __func__,
                     current_port->in_port_ifname, self->in_br_ifname);
            }
            ds_tree_remove(&self->in_br_port_list, remove_port);
            remove_port->in_bridge = NULL;
        }

        /* mark the port as configured. */
        current_port->in_port_configured = true;
        current_port = next_port;
    }

    return true;
}

/*
 * Destructor, called by inet_del()
 */
bool inet_bridge_dtor(inet_t *super)
{
    inet_bridge_t *self = CONTAINER_OF(super, inet_bridge_t, inet);

    return inet_bridge_fini(self);
}

bool inet_bridge_service_commit(inet_base_t *super, enum inet_base_services srv,
                                bool enable)
{
    bool retval;

    LOGD("%s(): received bridge service request %s (%d).", __func__, inet_base_service_str(srv), srv);
    inet_bridge_t *self = CONTAINER_OF(super, inet_bridge_t, base);

    switch (srv)
    {
    case INET_BASE_BRIDGE:
        retval = inet_bridge_service_BRIDGE(self, enable);
        break;

    case INET_BASE_BRIDGE_PORT:
        retval = inet_bridge_service_BRIDGE_PORT(self, enable);
        break;

    default:
        LOGE("%s(): Invalid service request %d", __func__, srv);
        retval = false;
        break;
    }

    return retval;
}

static void inet_bridge_port_add(inet_bridge_t *self, inet_port_t *in_port)
{
    struct inet_port *node;

    node = ds_tree_find(&self->in_br_port_list, in_port);
    if (node != NULL)
    {
        LOGD("%s() port %s already present, reconfiguring", __func__,
             in_port->in_port_ifname);
    }

    LOGD("%s(): marking port %s for adding to bridge", __func__, in_port->in_port_ifname);
    /* mark this node for deletion */
    in_port->in_add = true;

    /* set flag for reconfiguring */
    in_port->in_port_configured = false;

    if (node == NULL)
    {
        ds_tree_insert(&self->in_br_port_list, in_port, in_port);
        in_port->in_bridge = self;
    }
}

static void inet_bridge_port_del(inet_bridge_t *self, inet_port_t *in_port)
{
    struct inet_port *node;

    node = ds_tree_find(&self->in_br_port_list, in_port);
    if (node == NULL)
    {
        LOGD("%s() port %s not present for deletion", __func__, in_port->in_port_ifname);
        return;
    }

    LOGD("%s(): marking port %s for removing from bridge", __func__,
         in_port->in_port_ifname);
    /* mark this node for deletion */
    node->in_add = false;

    /* set flag for reconfiguring */
    in_port->in_port_configured = false;
}

bool inet_bridge_port_set(inet_t *super, inet_t *port_inet, bool add)
{
    inet_port_t *in_port;
    inet_bridge_t *self;

    self = CONTAINER_OF(super, inet_bridge_t, inet);
    in_port = CONTAINER_OF(port_inet, inet_port_t, inet);

    LOGN("%s: Port %s -> %s", self->in_br_ifname, in_port->in_port_ifname, add ? "add" : "del");

    if (add == false)
    {
        if (in_port->in_bridge == self)
        {
            inet_bridge_port_del(self, in_port);
        }
        else if (in_port->in_bridge != NULL)
        {
            LOGN("%s: Trying to remove port from current bridge (%s) while it is owned by another (%s).",
                    in_port->in_port_ifname,
                    self->in_br_ifname,
                    in_port->in_bridge->in_br_ifname);
        }
        else /* in_port->in_bridge == NULL */
        {
            LOGN("%s: Trying to remove bridge port that is not in bridge.",
                    in_port->in_port_ifname);
        }
    }
    else
    {
        /*
         * When moving ports between bridges, delete the interface from the old
         * bridge first
         */
        if (in_port->in_bridge != NULL && in_port->in_bridge != self)
        {
            LOGN("%s: Moving port from %s -> %s",
                    in_port->in_port_ifname,
                    in_port->in_bridge->in_br_ifname,
                    self->in_br_ifname);
            inet_bridge_port_del(in_port->in_bridge, in_port);
            inet_unit_restart(in_port->in_bridge->base.in_units, INET_BASE_BRIDGE_PORT, true);
            inet_commit(&in_port->in_bridge->inet);
            ASSERT(in_port->in_bridge == NULL, "Port parent bridge is not NULL");
        }

        inet_bridge_port_add(self, in_port);
    }

    inet_unit_restart(self->base.in_units, INET_BASE_BRIDGE_PORT, true);
    return true;
}

bool inet_bridge_set(inet_t *super, const char *brname, bool add)
{

    inet_bridge_t *self = CONTAINER_OF(super, inet_bridge_t, inet);

    self->in_br_add = add;
    inet_unit_restart(self->base.in_units, INET_BASE_BRIDGE, true);
    return true;
}

/**
 * Initializer
 */
bool inet_bridge_init(inet_bridge_t *self, const char *ifname)
{
    int ret;

    memset(self, 0, sizeof(*self));

    self->base.in_units =  inet_unit(INET_BASE_BRIDGE,
                           inet_unit(INET_BASE_BRIDGE_PORT, NULL),
                           NULL);

    self->inet.in_commit_fn = inet_bridge_commit;

    ret = strscpy(self->in_br_ifname, ifname, sizeof(self->in_br_ifname));
    if (ret < 0)
    {
        LOG(ERR, "%s(): Interface name %s too long.", __func__, ifname);
        return false;
    }

    /* initialize bridge ports list */
    ds_tree_init(&self->in_br_port_list, ds_void_cmp, struct inet_port, in_port_tnode);

    /*
     * Override inet_t methods
     */
    self->inet.in_dtor_fn = inet_bridge_dtor;

    self->inet.in_bridge_port_set_fn = inet_bridge_port_set;

    /* set bridge name for creating the bridge */
    self->inet.in_bridge_set_fn = inet_bridge_set;

    /*
     * Override inet_base_t methods
     */
    self->base.in_service_commit_fn = inet_bridge_service_commit;

    /* Initialize debounce timer */
    ev_debounce_init(&self->in_br_debounce, __inet_bridge_reapply, INET_BRIDGE_DEBOUNCE);

    return true;
}

/*
 * ===========================================================================
 *  Public API
 * ===========================================================================
 */

/**
 * Default inet_bridge class constructor
 */
inet_t *inet_bridge_new(const char *ifname)
{
    inet_bridge_t *self;
    bool success;

    self = CALLOC(1, sizeof(*self));
    if (self == NULL)
    {
        LOG(ERR, "%s(): Unable to allocate inet bridge object for %s", __func__, ifname);
        return NULL;
    }

    success = inet_bridge_init(self, ifname);
    if (!success)
    {
        FREE(self);
        return NULL;
    }

    return &self->inet;
}

bool __inet_base_bridge_commit(void *ctx, intptr_t unitid, bool enable)
{
    inet_base_t *self = ctx;

    return inet_service_commit(self, unitid, enable);
}

bool inet_bridge_commit(inet_t *super)
{
    bool retval;

    inet_base_t *self = (inet_base_t *)super;
    LOG(INFO, "inet_base: Committing new bridge configuration.");

    /* Commit all pending units */
    retval = inet_unit_commit(self->in_units, __inet_base_bridge_commit, self);
    return retval;
}
