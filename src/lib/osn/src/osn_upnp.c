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

/**
 * @file osn_upnp.c
 * @brief This module is an adapter pattern for conversion of osn upnp
 * interface oriented design (defined in osn_upnp.h) to simpler upnp
 * service oriented design (defined in osn_upnp_server.h)
 * 
 */

#include <stdlib.h>

#include "osn_upnp.h"
#include "linux/upnp_server.h"

#include "const.h"
#include "log.h"
#include "util.h"
#include "memutil.h"

static struct upnp_servers
{
    bool init_done;
    int ifc_count;
    upnp_server_t *servers[UPNP_ID_COUNT];
} g_srv = { .init_done = false, .ifc_count = 0 };

struct osn_upnp
{
    char if_name[C_IFNAME_LEN]; 
    upnp_server_t *server;
    enum osn_upnp_mode mode_p; // preset mode (requested)
    enum osn_upnp_mode mode_a; // active mode (applied)
};

typedef osn_upnp_t upnp_ifc_t;


static bool upnp_servers_init()
{
    if (g_srv.init_done) return true;

    size_t n;
    for (n = 0; n < ARRAY_SIZE(g_srv.servers); ++n)
    {
        upnp_server_t *ptr = upnp_server_new((enum upnp_srv_id)n);
        if (ptr == NULL)
        {
            LOG(ERR, "upnp: Error initializing UPNP service - out of mem.");
            return false;
        }
        g_srv.servers[n] = ptr;
    }
    g_srv.init_done = true;
    return true;
}

static void upnp_servers_fini()
{
    if (g_srv.init_done && 0 == g_srv.ifc_count)
    {
        size_t n;
        for (n = 0; n < ARRAY_SIZE(g_srv.servers); ++n)
        {
            upnp_server_del(g_srv.servers[n]);
            g_srv.servers[n] = NULL;
        }
        g_srv.init_done = false;
    }
}

osn_upnp_t* osn_upnp_new(const char *ifname)
{
    if (!upnp_servers_init()) return NULL;

    upnp_ifc_t *self;
    self = (upnp_ifc_t *)MALLOC(sizeof(*self));

    STRSCPY_WARN(self->if_name, ifname);
    self->server = NULL;
    self->mode_p = UPNP_MODE_NONE;
    self->mode_a = UPNP_MODE_NONE;

    g_srv.ifc_count++;
    return self;
}

bool osn_upnp_del(osn_upnp_t *self)
{
    if (self == NULL) return false;

    (void)osn_upnp_stop(self);
    FREE(self);
    g_srv.ifc_count--;
    upnp_servers_fini();
    return true;
}

static bool prepare_start(osn_upnp_t *self)
{
    bool rv = false;
    upnp_server_t *srv = NULL;

    switch(self->mode_p)
    {
        case UPNP_MODE_NONE:
            self->server = NULL;
            self->mode_a = UPNP_MODE_NONE;
            return false;

        case UPNP_MODE_INTERNAL:
            srv = g_srv.servers[UPNP_ID_WAN];
            rv = upnp_server_attach_internal(srv, self->if_name);
            break;

        case UPNP_MODE_EXTERNAL:
            srv = g_srv.servers[UPNP_ID_WAN];
            rv = upnp_server_attach_external(srv, self->if_name);
            break;

        case UPNP_MODE_INTERNAL_IPTV:
            srv = g_srv.servers[UPNP_ID_IPTV];
            rv = upnp_server_attach_internal(srv, self->if_name);
            break;

        case UPNP_MODE_EXTERNAL_IPTV:
            srv = g_srv.servers[UPNP_ID_IPTV];
            rv = upnp_server_attach_external(srv, self->if_name);
            break;
    }

    // update real active status when attachment successful
    if (rv)
    {
        self->mode_a = self->mode_p;
        self->server = srv;
    }
    return rv;
}

bool osn_upnp_set(osn_upnp_t *self, enum osn_upnp_mode mode)
{
    self->mode_p = mode;
    return true;
}

bool osn_upnp_get(osn_upnp_t *self, enum osn_upnp_mode *mode)
{
    // return requested and accepted mode
    *mode = (self->server != NULL) ? self->mode_a : UPNP_MODE_NONE;
    return true;
}

bool osn_upnp_apply(osn_upnp_t *self)
{
    if(self->mode_a == self->mode_p) return true;

    (void)osn_upnp_start(self);

    return true;
}

bool osn_upnp_start(osn_upnp_t *self)
{
    (void)osn_upnp_stop(self);

    if (!prepare_start(self)) return false;

    /* TODO: This is special case, what to return for start requested by
     * first interface (it cannot succeed, because server requires pair of
     * interfaces) and only start requested by second interface will activate
     * the server. 
     * Current usage in inet_base_upnp_commit() requires 'true' to return
     * otherwise upnp error log will be generated, this is why 'true' is
     * returned always */
    (void)upnp_server_start(self->server);
    return true;
}

bool osn_upnp_stop(osn_upnp_t *self)
{
    if (self->server == NULL) return true;
    
    if (!upnp_server_stop(self->server)) return false;

    upnp_server_detach(self->server, self->if_name);
    self->server = NULL;
    self->mode_a = UPNP_MODE_NONE;
    return true;
}
