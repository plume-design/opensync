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

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <net/if.h>
#include <arpa/inet.h>

#include <errno.h>
#include <string.h>

#include "execsh.h"
#include "log.h"
#include "osp_l2switch.h"
#include "util.h"

#include "inet.h"
#include "inet_base.h"
#include "inet_vlan.h"

static bool inet_vlan_vlanid_set(inet_t *super, int vlanid);
static bool inet_vlan_parent_ifname_set(inet_t *super, const char *ifname);
static bool inet_vlan_service_commit(inet_base_t *super, enum inet_base_services srv, bool enable);
static bool inet_vlan_service_IF_ENABLE(inet_vlan_t *self, bool enable);
static bool inet_vlan_service_IF_CREATE(inet_vlan_t *self, bool enable);
static osn_netif_status_fn_t inet_vlan_netif_status_fn;

/*
 * ===========================================================================
 *  Initialization
 * ===========================================================================
 */
inet_t *inet_vlan_new(const char *ifname)
{
    inet_vlan_t *self = NULL;

    self = malloc(sizeof(*self));
    if (self == NULL)
    {
        goto error;
    }

    if (!inet_vlan_init(self, ifname))
    {
        LOG(ERR, "inet_vif: %s: Failed to initialize interface instance.", ifname);
        goto error;
    }

    return &self->inet;

error:
    if (self != NULL)
    {
        free(self);
    }

    return NULL;
}

/*
 * Destructor, called by inet_del()
 */
bool inet_vlan_dtor(inet_t *super)
{
    inet_vlan_t *self = CONTAINER_OF(super, inet_vlan_t, inet);

    return inet_vlan_fini(self);
}

bool inet_vlan_init(inet_vlan_t *self, const char *ifname)
{
    memset(self, 0, sizeof(*self));

    /* Initialize the parent class -- inet_eth */
    if (!inet_eth_init(&self->eth, ifname))
    {
        LOG(ERR, "inet_vlan: %s: Failed to instantiate class, parent class inet_eth_init() failed.", ifname);
        return false;
    }

    self->inet.in_vlanid_set_fn = inet_vlan_vlanid_set;
    self->inet.in_parent_ifname_set_fn = inet_vlan_parent_ifname_set;
    self->base.in_service_commit_fn = inet_vlan_service_commit;

    return true;
}

bool inet_vlan_fini(inet_vlan_t *self)
{
    bool retval = true;

    if (!inet_eth_fini(&self->eth))
    {
        LOG(ERR, "inet_vlan: Error in parent constructor.");
        retval = false;
    }

    if (self->in_parent_netif != NULL)
    {
        osn_netif_del(self->in_parent_netif);
    }

    if (self->in_vlan != NULL)
    {
        osn_vlan_del(self->in_vlan);
    }

    return retval;
}

/*
 * ===========================================================================
 *  VLAN class methods
 * ===========================================================================
 */
bool inet_vlan_vlanid_set(
        inet_t *super,
        int vlanid)
{
    inet_vlan_t *self = CONTAINER_OF(super, inet_vlan_t, inet);

    if (vlanid == self->in_vlanid) return true;

    self->in_vlanid = vlanid;

    /* Interface must be recreated, therefore restart the IF_CREATE service */
    return inet_unit_restart(self->base.in_units, INET_BASE_IF_CREATE, false);
}

bool inet_vlan_parent_ifname_set(inet_t *super, const char *parent_ifname)
{
    inet_vlan_t *self = CONTAINER_OF(super, inet_vlan_t, inet);

    if (parent_ifname == NULL) parent_ifname = "";

    if (strcmp(self->in_parent_ifname, parent_ifname) == 0) return true;

    if (strlen(parent_ifname) >= sizeof(self->in_parent_ifname))
    {
        LOG(ERR, "inet_vlan: %s: Parent interface name too long: %s",
                self->inet.in_ifname, parent_ifname);
        return false;
    }

    STRSCPY(self->in_parent_ifname, parent_ifname);

    return inet_unit_restart(self->base.in_units, INET_BASE_IF_CREATE, false);
}

bool inet_vlan_service_commit(
        inet_base_t *super,
        enum inet_base_services srv,
        bool enable)
{
    inet_vlan_t *self = CONTAINER_OF(super, inet_vlan_t, base);

    LOG(DEBUG, "inet_vlan: %s: Service %s -> %s.",
            self->inet.in_ifname,
            inet_base_service_str(srv),
            enable ? "start" : "stop");

    switch (srv)
    {
        case INET_BASE_IF_ENABLE:
            if (!inet_vlan_service_IF_ENABLE(self, enable))
            {
                return false;
            }
            break;

        case INET_BASE_IF_CREATE:
            if (!inet_vlan_service_IF_CREATE(self, enable))
            {
                return false;
            }
            break;

        default:
            break;
    }

    /* Delegate to the parent service */
    return inet_eth_service_commit(super, srv, enable);
}

/*
 * ===========================================================================
 *  Commit and start/stop services
 * ===========================================================================
 */

/*
 * Implement the IF_ENABLE service which is responsible for parent monitoring
 */
bool inet_vlan_service_IF_ENABLE(inet_vlan_t *self, bool enable)
{
    (void)self;
    (void)enable;

    if (self->in_l2s_ifname[0] != '\0')
    {
        osp_l2switch_del(self->in_l2s_ifname);
        self->in_l2s_ifname[0] = '\0';
        self->in_l2s_vlanid = 0;
    }

    if (self->in_parent_netif != NULL)
    {
        osn_netif_del(self->in_parent_netif);
        self->in_parent_netif = NULL;
    }

    if (!enable) return true;

    if (self->in_parent_ifname[0] == '\0')
    {
        LOG(NOTICE, "inet_vlan: %s: Parent interface not set.", self->inet.in_ifname);
        return false;
    }

    if (osp_l2switch_new(self->in_parent_ifname))
    {
        /* Remember the interface name used by the l2switch API */
        STRSCPY_WARN(self->in_l2s_ifname, self->in_parent_ifname);
    }

    /*
     * IF_CREATE will be enabled from the parent interface status monitor
     */
    inet_unit_stop(self->base.in_units, INET_BASE_IF_CREATE);

    self->in_parent_netif = osn_netif_new(self->in_parent_ifname);
    if (self->in_parent_netif == NULL)
    {
        LOG(ERR, "inet_vlan: %s: Error creating parent interface %s OSN netif object.",
                self->inet.in_ifname, self->in_parent_ifname);
        return false;
    }

    /*
     * The osn_netif object is used only for monitoring the parent's interface status
     */
    osn_netif_data_set(self->in_parent_netif, self);
    osn_netif_status_notify(self->in_parent_netif, inet_vlan_netif_status_fn);

    return true;
}

/*
 * Implement the INET_UNIT_IF_CREATE service; this is responsible for
 * creating/destroying the interface
 */
bool inet_vlan_service_IF_CREATE(inet_vlan_t *self, bool enable)
{
    if (self->in_l2s_ifname[0] != '\0' && self->in_l2s_vlanid != 0)
    {
        if (!osp_l2switch_vlan_unset(self->in_l2s_ifname, self->in_l2s_vlanid))
        {
            LOG(WARN, "inet_vlan: %s: Error unsetting VLAN %d on interface %s.",
                    self->inet.in_ifname, self->in_l2s_vlanid, self->in_l2s_ifname);
        }
        self->in_l2s_vlanid = 0;
    }

    if (self->in_vlan != NULL)
    {
        osn_vlan_del(self->in_vlan);
    }

    self->in_vlan = osn_vlan_new(self->inet.in_ifname);
    if (self->in_vlan == NULL)
    {
        LOG(ERR, "inet_vlan: %s: Error creating VLAN OSN object.", self->inet.in_ifname);
        return false;
    }

    /*
     * Set the configuration
     */
    if (self->in_parent_ifname[0] != '\0')
    {
        osn_vlan_parent_set(self->in_vlan, self->in_parent_ifname);
    }

    if (self->in_vlanid >= 1 && self->in_vlanid <= 4095)
    {
        osn_vlan_vid_set(self->in_vlan, self->in_vlanid);
    }

    if (!enable) return true;

    if (!osn_vlan_apply(self->in_vlan))
    {
        LOG(ERR, "inet_vlan: %s: Error applying VLAN configuration.",
                self->inet.in_ifname);
        return false;
    }

    if (self->in_l2s_ifname[0] == '\0') return true;

    if (!osp_l2switch_vlan_set(self->in_l2s_ifname, self->in_vlanid, true))
    {
        LOG(ERR, "inet_vlan: %s: Error setting L2Switch VLAN tag for interface %s.",
                self->inet.in_ifname, self->in_l2s_ifname);
        return false;
    }

    self->in_l2s_vlanid = self->in_vlanid;

    if (!osp_l2switch_apply(self->in_l2s_ifname))
    {
        LOG(ERR, "inet_vlan: %s: Error applying L2Switch configuration for interface %s.",
                self->inet.in_ifname, self->in_l2s_ifname);
        return false;
    }

    return true;
}

/*
 * Check if the criteria for enabling the interface has been met and enable it.
 * The criteria is as follows:
 */
void inet_vlan_netif_status_fn(osn_netif_t *netif, struct osn_netif_status *status)
{
    inet_vlan_t *self = osn_netif_data_get(netif);

    if (inet_unit_is_enabled(self->base.in_units, INET_BASE_IF_CREATE) == status->ns_exists)
    {
        return;
    }

    if (status->ns_exists)
    {
        LOG(INFO, "inet_vlan: %s: Parent interface %s exists, creating VLAN interface.",
                self->inet.in_ifname, self->in_parent_ifname);
    }
    else
    {
        LOG(INFO, "inet_vlan: %s: Parent interface %s ceased to exist, deleting VLAN interface.",
                self->inet.in_ifname, self->in_parent_ifname);
    }

    if (!inet_unit_enable(self->base.in_units, INET_BASE_IF_CREATE, status->ns_exists))
    {
        LOG(ERR, "inet_vlan: %s: Error starting VLAN interface creation.", self->inet.in_ifname);
    }

    inet_commit(&self->inet);
}
