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
#include <string.h>

#include <ev.h>

#include "ds_dlist.h"
#include "inet_routes6.h"
#include "log.h"
#include "memutil.h"
#include "osn_inet.h"

#ifndef INET_ROUTES6_APPLY_DELAY
#define INET_ROUTES6_APPLY_DELAY 0.25
#endif

// route element for list collection
typedef struct route_elem
{
    osn_route6_config_t route;
    ds_dlist_node_t node;

} route_elem_t;

struct inet_routes6
{
    osn_route6_cfg_t *osn_rc;  // OSN route configurator
    bool enabled;
    ev_timer apply_timer;  // routes apply delay timer
    ds_dlist_t routes;
};

static inline void prv_route_remove(inet_routes6_t *self, route_elem_t *re)
{
    ds_dlist_remove(&self->routes, re);
    FREE(re);
}

static bool prv_route_delete(inet_routes6_t *self, route_elem_t *re, bool remove)
{
    LOG(INFO,
        "inet_routes6: %s: deleting route to %s",
        osn_route6_cfg_name(self->osn_rc),
        FMT_osn_ip6_addr(re->route.dest));

    bool rv = osn_route6_remove(self->osn_rc, &re->route);
    if (remove)
    {
        prv_route_remove(self, re);
    }
    return rv;
}

static bool prv_route_insert(inet_routes6_t *self, const osn_route6_config_t *route)
{
    route_elem_t *re = (route_elem_t *)MALLOC(sizeof(*re));

    re->route = *route;
    ds_dlist_insert_tail(&self->routes, re);
    return true;
}

static bool prv_route_add(inet_routes6_t *self, const osn_route6_config_t *route, bool insert)
{
    LOG(INFO, "inet_routes6: %s: adding route to %s", osn_route6_cfg_name(self->osn_rc), FMT_osn_ip6_addr(route->dest));

    bool rv = osn_route6_add(self->osn_rc, route);
    if (rv && insert)
    {
        rv = prv_route_insert(self, route);
    }
    return rv;
}

static route_elem_t *prv_find_route(ds_dlist_t *routes, const osn_route6_config_t *pattern)
{
    route_elem_t *re;
    for (re = ds_dlist_head(routes); re != NULL; re = ds_dlist_next(routes, re))
    {
        if (0 == memcmp(&re->route, pattern, sizeof(*pattern))) break;
    }
    return re;
}

bool inet_routes6_add(inet_routes6_t *self, const osn_route6_config_t *route)
{
    if (self->enabled)
    {
        return prv_route_add(self, route, true);
    }
    else
    {
        return prv_route_insert(self, route);
    }
}

bool inet_routes6_remove(inet_routes6_t *self, const osn_route6_config_t *route)
{
    route_elem_t *re = prv_find_route(&self->routes, route);
    if (NULL == re) return false;

    if (self->enabled)
    {
        return prv_route_delete(self, re, true);
    }
    else
    {
        return prv_route_remove(self, re), true;
    }
}

static bool prv_apply_all_routes(inet_routes6_t *self, bool enable)
{
    bool rv = true;
    route_elem_t *re;
    for (re = ds_dlist_head(&self->routes); re != NULL; re = ds_dlist_next(&self->routes, re))
    {
        if (enable)
        {
            rv = rv & prv_route_add(self, &re->route, false);
        }
        else
        {
            /* do not propagate error, it is OK that kernel can be
             * faster with flushing dead routes */
            (void)prv_route_delete(self, re, false);
        }
    }
    return rv;
}

bool inet_routes6_enable(inet_routes6_t *self, bool enable)
{
    if (self->enabled == enable) return true;

    self->enabled = enable;

    return prv_apply_all_routes(self, enable);
}

bool inet_routes6_is_enabled(const inet_routes6_t *self)
{
    return self->enabled;
}

bool inet_routes6_reapply(inet_routes6_t *self)
{
    if (!self->enabled) return false;

    ev_timer_again(EV_DEFAULT, &self->apply_timer);
    return true;
}

static void eh_on_apply_time_expired(struct ev_loop *loop, ev_timer *w, int revents)
{
    inet_routes6_t *self = CONTAINER_OF(w, struct inet_routes6, apply_timer);

    if (self->enabled)
    {
        LOG(INFO, "inet_routes6: %s: reapplying static routes", osn_route6_cfg_name(self->osn_rc));

        /* Adding may fail if interface is down or route already added,
         * but this is not a problem */
        (void)prv_apply_all_routes(self, true);
    }
    ev_timer_stop(EV_DEFAULT, w);
}

inet_routes6_t *inet_routes6_new(const char *if_name)
{
    osn_route6_cfg_t *rcfg = osn_route6_cfg_new(if_name);
    if (NULL == rcfg) return NULL;

    inet_routes6_t *self = (inet_routes6_t *)MALLOC(sizeof(*self));

    self->osn_rc = rcfg;
    self->enabled = false;
    ev_timer_init(&self->apply_timer, &eh_on_apply_time_expired, INET_ROUTES6_APPLY_DELAY, INET_ROUTES6_APPLY_DELAY);
    ds_dlist_init(&self->routes, route_elem_t, node);
    return self;
}

void inet_routes6_del(inet_routes6_t *self)
{
    route_elem_t *re;
    for (re = ds_dlist_head(&self->routes); re != NULL;)
    {
        route_elem_t *re_temp = re;
        re = ds_dlist_next(&self->routes, re);

        if (self->enabled)
            prv_route_delete(self, re_temp, true);
        else
            prv_route_remove(self, re_temp);
    }

    osn_route6_cfg_del(self->osn_rc);
    ev_timer_stop(EV_DEFAULT, &self->apply_timer);
    FREE(self);
}
