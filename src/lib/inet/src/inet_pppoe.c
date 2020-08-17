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

#include "log.h"
#include "util.h"

#include "inet.h"
#include "inet_base.h"
#include "inet_pppoe.h"

static bool inet_pppoe_parent_ifname_set(inet_t *super, const char *parent_ifname);
static bool inet_pppoe_service_commit(inet_base_t *super, enum inet_base_services srv, bool enable);
static bool inet_pppoe_service_IF_ENABLE(inet_pppoe_t *self, bool enable);
static bool inet_pppoe_service_IF_CREATE(inet_pppoe_t *self, bool enable);
static osn_pppoe_status_fn_t inet_pppoe_status_fn;
static osn_netif_status_fn_t inet_pppoe_netif_status_fn;
static bool inet_pppoe_credential_set(inet_t *self, const char *username, const char *password);

/*
 * Default inet_pppoe class constructor
 */
inet_t *inet_pppoe_new(const char *ifname)
{
    inet_pppoe_t *self;

    self = calloc(1, sizeof(*self));
    if (self == NULL)
    {
        LOG(ERR, "inet_pppoe: %s: Unable to allocate inet_pppoe object.", ifname);
        return NULL;
    }

    if (!inet_pppoe_init(self, ifname))
    {
        LOG(ERR, "inet_pppoe: %s: Failed to initialize inet_pppoe instance.", ifname);
        free(self);
        return NULL;
    }

    return &self->inet;
}

/*
 * Destructor, called by inet_del()
 */
bool inet_pppoe_dtor(inet_t *super)
{
    inet_pppoe_t *self = CONTAINER_OF(super, inet_pppoe_t, inet);

    return inet_pppoe_fini(self);
}

/*
 * inet_pppoe class initializer
 */
bool inet_pppoe_init(inet_pppoe_t *self, const char *ifname)
{
    memset(self, 0, sizeof(*self));

    if (!inet_base_init(&self->base, ifname))
    {
        LOG(ERR, "inet_pppoe: %s: Failed to instantiate inet_base class.", ifname);
        return false;
    }

    /*
     * Override inet_t methods
     */
    self->inet.in_dtor_fn = inet_pppoe_dtor;
    self->inet.in_parent_ifname_set_fn = inet_pppoe_parent_ifname_set;
    self->inet.in_credential_set_fn = inet_pppoe_credential_set;

    /*
     * Override inet_base_t methods
     */
    self->base.in_service_commit_fn = inet_pppoe_service_commit;

    /*
     * The pppd daemon creates the interface only when there's a successful
     * connection to the peer. But in order to run pppd, the parent interface
     * must exists.
     *
     * To handle these cases, we use the following services:
     * - INET_BASE_IF_ENABLE: starts monitoring the parent interface
     * - INET_BASE_IF_CREATE: is enabled when the parent interface exists and
     *                        starts the PPP daemon
     * - INET_BASE_IF_READY:  is enabled when the PPPoE interface is created
     *                        by pppoe
     *
     * Disable all these services by default.
     */
    inet_unit_stop(self->base.in_units, INET_BASE_IF_CREATE);
    inet_unit_stop(self->base.in_units, INET_BASE_IF_READY);

    /*
     * Start an instance of the PPPoE object right away by artificially
     * triggering the IF_CREATE service
     */
    inet_pppoe_service_IF_CREATE(self, false);

    return true;
}

/**
 * inet_pppoe class finalizer
 */
bool inet_pppoe_fini(inet_pppoe_t *self)
{
    bool retval = true;

    if (!inet_base_fini(&self->base))
    {
        LOG(ERR, "inet_pppoe: %s: Error shutting down base class.", self->inet.in_ifname);
        retval = false;
    }

    if (self->in_parent_netif != NULL)
    {
        osn_netif_del(self->in_parent_netif);
        retval = false;
    }

    if (self->in_pppoe != NULL)
    {
        osn_pppoe_del(self->in_pppoe);
        retval = false;
    }

    return retval;
}

bool inet_pppoe_service_commit(inet_base_t *super, enum inet_base_services srv, bool enable)
{
    bool retval;

    inet_pppoe_t *self = CONTAINER_OF(super, inet_pppoe_t, base);

    switch (srv)
    {
        case INET_BASE_IF_ENABLE:
            retval = inet_pppoe_service_IF_ENABLE(self, enable);
            break;

        case INET_BASE_IF_CREATE:
            retval = inet_pppoe_service_IF_CREATE(self, enable);
            break;

        default:
            return inet_base_service_commit(super, srv, enable);
    }

    return retval;
}

bool inet_pppoe_service_IF_ENABLE(inet_pppoe_t *self, bool enable)
{
    /*
     * Enable monitoring of the parent interface
     */
    if (self->in_parent_netif != NULL)
    {
        osn_netif_del(self->in_parent_netif);
        self->in_parent_netif = NULL;
    }

    if (!enable) return true;

    if (self->in_parent_ifname[0] == '\0')
    {
        LOG(NOTICE, "inet_pppoe: %s: Parent interface not set.", self->inet.in_ifname);
        return false;
    }

    /*
     * IF_CREATE will be enabled from the parent interface status monitor
     */
    inet_unit_stop(self->base.in_units, INET_BASE_IF_CREATE);

    /*
     * If the interface is enabled start monitoring the parent interface
     */
    self->in_parent_netif = osn_netif_new(self->in_parent_ifname);
    if (self->in_parent_netif == NULL)
    {
        LOG(ERR, "inet_pppoe: %s: Error creating parent interface %s OSN netif object.",
                self->inet.in_ifname, self->in_parent_ifname);
        return false;
    }

    /*
     * The osn_netif object is used only for monitoring the parent's interface status
     */
    osn_netif_data_set(self->in_parent_netif, self);
    osn_netif_status_notify(self->in_parent_netif, inet_pppoe_netif_status_fn);

    return true;
}


bool inet_pppoe_service_IF_CREATE(inet_pppoe_t *self, bool enable)
{
    /*
     * Always recreate the PPPoE interface -- the osn_pppoe_t instance must
     * be always active in order to support monitoring mode.
     */
    if (self->in_pppoe != NULL)
    {
        osn_pppoe_del(self->in_pppoe);
    }

    self->in_pppoe = osn_pppoe_new(self->inet.in_ifname);
    if (self->in_pppoe == NULL)
    {
        LOG(ERR, "inet_pppoe: %s: Error creating PPPoE OSN object.", self->inet.in_ifname);
        return false;
    }

    /* Register status callbacks */
    osn_pppoe_data_set(self->in_pppoe, self);
    osn_pppoe_status_notify(self->in_pppoe, inet_pppoe_status_fn);

    /*
     * Set the configuration
     */
    if (self->in_parent_ifname[0] != '\0')
    {
        osn_pppoe_parent_set(self->in_pppoe, self->in_parent_ifname);
    }

    if (self->in_ppp_username[0] != '\0')
    {
        osn_pppoe_secret_set(self->in_pppoe, self->in_ppp_username, self->in_ppp_password);
    }

    if (!enable) return true;

    if (!osn_pppoe_apply(self->in_pppoe))
    {
        LOG(ERR, "inet_pppoe: %s: Error applying PPPoE configuration.",
                self->inet.in_ifname);
        return false;
    }

    return true;
}

bool inet_pppoe_parent_ifname_set(inet_t *super, const char *parent_ifname)
{
    inet_pppoe_t *self = CONTAINER_OF(super, inet_pppoe_t, inet);

    if (parent_ifname == NULL)
    {
        if (self->in_parent_ifname[0] == '\0')
        {
            return true;
        }

        self->in_parent_ifname[0] = '\0';
    }
    else if (strcmp(self->in_parent_ifname, parent_ifname) == 0)
    {
        return true;
    }
    else
    {
        STRSCPY(self->in_parent_ifname, parent_ifname);
    }

    /*
     * Restart the top-level service when the parent interface -- we essentially
     * need to recreate the PPP link from scratch
     */
    return inet_unit_restart(self->base.in_units, INET_BASE_IF_ENABLE, false);
}

bool inet_pppoe_credential_set(inet_t *super, const char *username, const char *password)
{
    inet_pppoe_t *self = CONTAINER_OF(super, inet_pppoe_t, inet);

    if (username == NULL) username = "";
    if (password == NULL) password = "";

    if (strcmp(self->in_ppp_username, username) == 0 &&
            strcmp(self->in_ppp_password, password) == 0)
    {
        return true;
    }

    STRSCPY(self->in_ppp_username, username);
    STRSCPY(self->in_ppp_password, password);

    /* Trigger PPP interface recreation */
    return inet_unit_restart(self->base.in_units, INET_BASE_IF_CREATE, false);
}

void inet_pppoe_status_fn(osn_pppoe_t *pppoe, struct osn_pppoe_status *status)
{
    inet_pppoe_t *self = osn_pppoe_data_get(pppoe);

    self->base.in_state.in_assign_scheme = INET_ASSIGN_NONE;
    self->base.in_state.in_bcaddr = OSN_IP_ADDR_INIT;
    memset(&self->base.in_state.in_macaddr, 0, sizeof(self->base.in_state.in_macaddr));
    osn_ip_addr_from_str(&self->base.in_state.in_netmask, "255.255.255.255");

    self->base.in_state.in_port_status = status->ps_carrier;
    self->base.in_state.in_mtu = status->ps_mtu;
    self->base.in_state.in_ipaddr = status->ps_local_ip;
    self->base.in_state.in_gateway = status->ps_remote_ip;

    inet_base_state_update(&self->base);

    if (inet_unit_is_enabled(self->base.in_units, INET_BASE_IF_READY) != status->ps_exists)
    {
        inet_unit_enable(self->base.in_units, INET_BASE_IF_READY, status->ps_exists);
        inet_commit(&self->inet);
    }
}

/*
 * Check if the criteria for enabling the interface has been met and enable it.
 * The criteria is as follows:
 */
void inet_pppoe_netif_status_fn(osn_netif_t *netif, struct osn_netif_status *status)
{
    inet_pppoe_t *self = osn_netif_data_get(netif);

    if (inet_unit_is_enabled(self->base.in_units, INET_BASE_IF_CREATE) == status->ns_exists)
    {
        return;
    }

    if (status->ns_exists)
    {
        LOG(INFO, "inet_pppoe: %s: Parent interface %s exists, initiating PPPoE.",
                self->inet.in_ifname, self->in_parent_ifname);
    }
    else
    {
        LOG(INFO, "inet_pppoe: %s: Parent interface %s ceased to exist, terminating PPPoE",
                self->inet.in_ifname, self->in_parent_ifname);
    }

    if (!inet_unit_enable(self->base.in_units, INET_BASE_IF_CREATE, status->ns_exists))
    {
        LOG(ERR, "inet_pppoe: %s: Error starting PPPoE interface creation.", self->inet.in_ifname);
    }

    inet_commit(&self->inet);
}
