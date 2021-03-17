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

#include "inet_routes.h"
#include "osn_inet.h"
#include "ds_dlist.h"
#include "log.h"

// route element for list collection
typedef struct route_elem
{
    osn_route4_t route;
    ds_dlist_node_t node;

} route_elem_t;

struct inet_routes
{
    osn_route4_cfg_t *osn_rc; // OSN route configurator
    bool enabled;
    ds_dlist_t routes;
};

static inline void prv_route_remove(inet_routes_t *self, route_elem_t *re)
{
    ds_dlist_remove(&self->routes, re);
    free(re);
}

static bool prv_route_delete(inet_routes_t *self, route_elem_t *re, bool remove)
{
    LOG(INFO, "inet_routes: %s: deleting route to %s", osn_route4_cfg_name(self->osn_rc), FMT_osn_ip_addr(re->route.dest));

    bool rv = osn_route_remove(self->osn_rc, &re->route);
    if (remove)
    {
        prv_route_remove(self, re);
    }
    return rv;
}

static bool prv_route_insert(inet_routes_t *self, const osn_route4_t *route)
{
    route_elem_t *re = (route_elem_t *)malloc(sizeof(*re));
    if (NULL == re) return false;

    re->route = *route;
    ds_dlist_insert_tail(&self->routes, re);
    return true;
}

static bool prv_route_add(inet_routes_t *self, const osn_route4_t *route, bool insert)
{
    LOG(INFO, "inet_routes: %s: adding route to %s", osn_route4_cfg_name(self->osn_rc), FMT_osn_ip_addr(route->dest));
    
    bool rv = osn_route_add(self->osn_rc, route);
    if (rv && insert)
    {
        rv = prv_route_insert(self, route);
    }
    return rv;
}

static route_elem_t* prv_find_route(ds_dlist_t *routes, const osn_route4_t *pattern)
{
    route_elem_t *re;
    for (re = ds_dlist_head(routes); re != NULL; re = ds_dlist_next(routes, re))
    {
        if (0 == memcmp(&re->route, pattern, sizeof(*pattern))) break;
    }
    return re;
}

inet_routes_t *inet_routes_new(const char * if_name)
{
    osn_route4_cfg_t *rcfg = osn_route4_cfg_new(if_name);
    if (NULL == rcfg) return NULL;

    inet_routes_t * self = (inet_routes_t *)malloc(sizeof(*self));
    if (NULL == self)
    {
        (void)osn_route4_cfg_del(rcfg);
        return NULL;
    }

    self->osn_rc = rcfg;
    self->enabled = false;
    ds_dlist_init(&self->routes, route_elem_t, node);
    return self;
}

void inet_routes_del(inet_routes_t * self)
{
    route_elem_t *re;
    for (re = ds_dlist_head(&self->routes); re != NULL; )
    {
        route_elem_t *re_temp = re;
        re = ds_dlist_next(&self->routes, re);

        if (self->enabled)
            prv_route_delete(self, re_temp, true);
        else
            prv_route_remove(self, re_temp);
    }

    osn_route4_cfg_del(self->osn_rc);
    free(self);
}

bool inet_routes_add(inet_routes_t *self, const osn_route4_t *route)
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

bool inet_routes_remove(inet_routes_t * self, const osn_route4_t *route)
{
    route_elem_t *re = prv_find_route(&self->routes, route);
    if (NULL == re) return false;

    if(self->enabled)
    {
        return prv_route_delete(self, re, true);
    }
    else
    {
        return prv_route_remove(self, re), true;
    }
}

bool inet_routes_enable(inet_routes_t * self, bool enable)
{
    if (self->enabled == enable) return true;

    self->enabled = enable;

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

bool inet_routes_is_enabled(const inet_routes_t * self)
{
    return self->enabled;
}

bool inet_routes_reapply(inet_routes_t *self, const osn_route4_t *route)
{
    if (!self->enabled) return false;

    route_elem_t *re = prv_find_route(&self->routes, route);
    if (NULL == re) return false;
    
    /* Adding may fail if interface is down or route already added, but this 
     * is not a problem */
    (void)prv_route_add(self, route, false);
    return true;
}
