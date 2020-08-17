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
 * WANO Wifi_Inet_State
 * ===========================================================================
 */
#include "ovsdb_table.h"
#include "schema.h"

#include "wano.h"
#include "wano_internal.h"

static ovsdb_table_t table_Wifi_Inet_State;
static ovsdb_table_t table_Wifi_Master_State;

/**
 * List of cached Wifi_Inet_State structures
 *
 * Note: An entry on this list may exists even if there are no corresponding
 * entries in OVSDB. The reason for this is that plugins can register to events
 * for interfaces that do not exist yet in Wifi_Inet_State.
 */
static ds_tree_t wano_inet_state_list = DS_TREE_INIT(
        ds_str_cmp,
        struct wano_inet_state,
        is_tnode);

static reflink_fn_t wano_inet_state_reflink_fn;
static struct wano_inet_state *wano_inet_state_get(const char *ifname);
static void wano_inet_state_event_async_fn(struct ev_loop *loop, ev_async *w, int revent);

void callback_Wifi_Inet_State(
        ovsdb_update_monitor_t *self,
        struct schema_Wifi_Inet_State *old,
        struct schema_Wifi_Inet_State *new);

void callback_Wifi_Master_State(
        ovsdb_update_monitor_t *self,
        struct schema_Wifi_Master_State *old,
        struct schema_Wifi_Master_State *new);

bool wano_inet_state_init(void)
{
    /* Register to Wifi_Inet_State */
    OVSDB_TABLE_INIT(Wifi_Inet_State, if_name);
    if (!OVSDB_TABLE_MONITOR(Wifi_Inet_State, true))
    {
        LOG(INFO, "inet_state: Error monitoring Wifi_Inet_State");
        return false;
    }

    OVSDB_TABLE_INIT(Wifi_Master_State, if_name);
    if (!OVSDB_TABLE_MONITOR(Wifi_Master_State, true))
    {
        LOG(INFO, "inet_state: Error monitoring Wifi_Master_State");
        return false;
    }

    return true;
}

void wano_inet_state_reflink_fn(reflink_t *obj, reflink_t *sender)
{
    struct wano_inet_state *is;

    /* Received event from subscriber? */
    if (sender != NULL)
    {
        LOG(WARN, "inet_state: Received event from subscriber.");
        return;
    }

    is = CONTAINER_OF(obj, struct wano_inet_state, is_reflink);
    LOG(DEBUG, "inet_state: Reached 0 count: %s", is->is_ifname);
    ds_tree_remove(&wano_inet_state_list, is);
    reflink_fini(&is->is_reflink);
    free(is);
}

struct wano_inet_state *wano_inet_state_get(const char *ifname)
{
    struct wano_inet_state *is;

    is = ds_tree_find(&wano_inet_state_list, (void *)ifname);
    if (is != NULL)
    {
        return is;
    }

    is = calloc(1, sizeof(struct wano_inet_state));
    STRSCPY(is->is_ifname, ifname);

    reflink_init(&is->is_reflink, "wano_inet_state");
    reflink_set_fn(&is->is_reflink, wano_inet_state_reflink_fn);

    ds_tree_insert(&wano_inet_state_list, is, is->is_ifname);

    return is;
}

/**
 * Wifi_Inet_State OVSDB monitor function
 */
void callback_Wifi_Inet_State(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Inet_State *old,
        struct schema_Wifi_Inet_State *new)
{
    struct wano_inet_state *is;
    const char *ifname;
    int ii;

    ifname = (mon->mon_type == OVSDB_UPDATE_DEL) ? old->if_name : new->if_name;

    is = wano_inet_state_get(ifname);
    if (is == NULL)
    {
        LOG(ERR, "inet_state: %s: Error acquiring inet_state object (Wifi_Inet_State monitor).", ifname);
        return;
    }

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            LOG(DEBUG, "inet_state: %s: Wifi_Inet_State NEW", ifname);
            reflink_ref(&is->is_reflink, 1);
            break;

        case OVSDB_UPDATE_MODIFY:
            break;

        case OVSDB_UPDATE_DEL:
            LOG(DEBUG, "inet_state: %s: Wifi_Inet_State DEL", ifname);
            /* Dereference inet_state and return */
            is->is_inet_state_valid = false;
            reflink_ref(&is->is_reflink, -1);
            return;

        default:
            LOG(ERR, "inet_state: %s: Wifi_Inet_state monitor update error.", ifname);
            return;
    }

    /*
     * Update cache
     */
    is->is_inet_state_valid = true;
    is->is_enabled = new->enabled;
    is->is_network = new->network;
    is->is_nat = new->NAT;

    STRSCPY(is->is_ip_assign_scheme, new->ip_assign_scheme);

    is->is_ipaddr = OSN_IP_ADDR_INIT;
    if (new->inet_addr[0] != '\0' &&
            strcmp(new->inet_addr, "0.0.0.0") != 0 &&
            !osn_ip_addr_from_str(&is->is_ipaddr, new->inet_addr))
    {
        LOG(WARN, "inet_state: %s: Error parsing Wifi_Inet_State:inet_addr: '%s'", ifname, new->inet_addr);
    }

    is->is_netmask = OSN_IP_ADDR_INIT;
    if (new->netmask[0] != '\0' &&
            strcmp(new->netmask, "0.0.0.0") != 0 &&
            !osn_ip_addr_from_str(&is->is_netmask, new->netmask))
    {
        LOG(WARN, "inet_state: %s: Error parsing Wifi_Inet_State:netmask: '%s'", ifname, new->netmask);
    }

    is->is_gateway = OSN_IP_ADDR_INIT;
    if (new->gateway[0] != '\0' &&
            strcmp(new->gateway, "0.0.0.0") != 0 &&
            !osn_ip_addr_from_str(&is->is_gateway, new->gateway))
    {
        LOG(WARN, "inet_state: %s: Error parsing Wifi_Inet_State:gateway: '%s'", ifname, new->gateway);
    }

    is->is_dns1 = OSN_IP_ADDR_INIT;
    is->is_dns2 = OSN_IP_ADDR_INIT;

    for (ii = 0; ii < new->dns_len; ii++)
    {
        osn_ip_addr_t *dns;

        if (strcmp(new->dns_keys[ii], "primary") == 0)
        {
            dns = &is->is_dns1;
        }
        else if (strcmp(new->dns_keys[ii], "secondary") == 0)
        {
            dns = &is->is_dns2;
        }
        else
        {
            continue;
        }

        if (!osn_ip_addr_from_str(dns, new->dns[ii]))
        {
            LOG(DEBUG, "inet_state: %s: Error parsing Wifi_Inet_State:dns[%s] = %s",
                    ifname, new->dns_keys[ii], new->dns[ii]);
            continue;
        }
    }

    LOG(DEBUG, "inet_state: %s: Wifi_Inet_State ADD/MOD[%d] enabled=%d, network=%d, inet_addr="
            PRI_osn_ip_addr" netmask="PRI_osn_ip_addr" gateway="PRI_osn_ip_addr" ip_assign_scheme=%s dns1=%s dns2=%s",
            ifname,
            mon->mon_type == OVSDB_UPDATE_MODIFY,
            is->is_enabled,
            is->is_network,
            FMT_osn_ip_addr(is->is_ipaddr),
            FMT_osn_ip_addr(is->is_netmask),
            FMT_osn_ip_addr(is->is_gateway),
            is->is_ip_assign_scheme,
            FMT_osn_ip_addr(is->is_dns1),
            FMT_osn_ip_addr(is->is_dns2));

    reflink_signal(&is->is_reflink);
}

/**
 * Wifi_Master_State OVSDB monitor function
 */
void callback_Wifi_Master_State(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Master_State *old,
        struct schema_Wifi_Master_State *new)
{
    struct wano_inet_state *is;
    const char *ifname;

    ifname = (mon->mon_type == OVSDB_UPDATE_DEL) ? old->if_name : new->if_name;

    is = wano_inet_state_get(ifname);
    if (is == NULL)
    {
        LOG(ERR, "inet_state: %s: Error acquiring inet_state object (Wifi_Master_State monitor).", ifname);
        return;
    }

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            LOG(DEBUG, "inet_state: %s: Wifi_Master_State NEW", ifname);
            reflink_ref(&is->is_reflink, 1);
            break;

        case OVSDB_UPDATE_MODIFY:
            break;

        case OVSDB_UPDATE_DEL:
            LOG(DEBUG, "inet_state: %s: Wifi_Master_State DEL", ifname);
            /* Dereference inet_state and return */
            is->is_master_state_valid = false;
            reflink_ref(&is->is_reflink, -1);
            return;

        default:
            LOG(ERR, "inet_state: Wifi_Master_State monitor update error.");
            return;
    }

    is->is_master_state_valid = true;
    is->is_port_state = strcmp(new->port_state, "active") == 0;

    if (new->inet_addr[0] == '\0' ||
            strcmp(new->inet_addr, "0.0.0.0") == 0 ||
            !osn_ip_addr_from_str(&is->is_ipaddr, new->inet_addr))
    {
        is->is_ipaddr = OSN_IP_ADDR_INIT;
    }

    LOG(DEBUG, "inet_state: Wifi_Master_State ADD/MOD[%d] ifname=%s,",
            mon->mon_type == OVSDB_UPDATE_MODIFY,
            is->is_ifname);

    reflink_signal(&is->is_reflink);
}

void wano_inet_state_event_reflink_fn(reflink_t *ref, reflink_t *sender)
{
    wano_inet_state_event_t *self = CONTAINER_OF(ref, wano_inet_state_event_t, ise_inet_state_reflink);

    if (sender == NULL)
    {
        LOG(DEBUG, "inet_state_event: Reached 0 count: %s",
                self->ise_inet_state->is_ifname);
        return;
    }

    /*
     * Propagate events only when we get a valid Wifi_Inet_State and a valid
     * Wifi_Master_State update.
     *
     * Always use the async event since we want to avoid re-entrancy issues
     * with reflinks.
     */
    if (self->ise_inet_state->is_inet_state_valid &&
            self->ise_inet_state->is_master_state_valid)
    {
        wano_inet_state_event_refresh(self);
    }
}

/**
 * Register to Wifi_Inet_State update events
 */
bool wano_inet_state_event_init(
        wano_inet_state_event_t *self,
        const char *ifname,
        wano_inet_state_event_fn_t *fn)
{
    memset(self, 0, sizeof(*self));

    self->ise_event_fn = fn;

    self->ise_inet_state = wano_inet_state_get(ifname);
    if (self->ise_inet_state == NULL)
    {
        LOG(ERR, "inet_state: %s: Error acquiring inet_state object.", ifname);
        return false;
    }

    reflink_init(&self->ise_inet_state_reflink, "wifi_inet_state_event.ie_inet_state_reflink");
    reflink_set_fn(&self->ise_inet_state_reflink, wano_inet_state_event_reflink_fn);
    reflink_connect(&self->ise_inet_state_reflink, &self->ise_inet_state->is_reflink);

    ev_async_init(&self->ise_async, wano_inet_state_event_async_fn);
    ev_async_start(EV_DEFAULT, &self->ise_async);

    self->ise_init = true;

    wano_inet_state_event_refresh(self);

    return true;
}

void wano_inet_state_event_fini(wano_inet_state_event_t *self)
{
    if (!self->ise_init)
    {
        return;
    }

    self->ise_init = false;

    ev_async_stop(EV_DEFAULT, &self->ise_async);
    reflink_disconnect(&self->ise_inet_state_reflink, &self->ise_inet_state->is_reflink);
    reflink_fini(&self->ise_inet_state_reflink);
}

/*
 * Force a state refresh
 */
void wano_inet_state_event_refresh(wano_inet_state_event_t *self)
{
    ev_async_send(EV_DEFAULT, &self->ise_async);
}

void wano_inet_state_event_async_fn(struct ev_loop *loop, ev_async *w, int revent)
{
    (void)loop;
    (void)revent;

    wano_inet_state_event_t *self = CONTAINER_OF(w, wano_inet_state_event_t, ise_async);

    self->ise_event_fn(self, self->ise_inet_state);
}
