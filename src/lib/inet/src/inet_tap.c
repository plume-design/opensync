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

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "inet.h"
#include "inet_base.h"
#include "inet_tap.h"
#include "log.h"
#include "memutil.h"
#include "os.h"
#include "util.h"

static bool inet_tap_service_commit(inet_base_t *super, enum inet_base_services srv,
                                    bool enable);
bool inet_tap_dtor(inet_t *super);

bool inet_tap_fini(inet_tap_t *self)
{
    bool ret = true;

    TRACE();

    if (!inet_eth_fini(&self->eth))
    {
        LOG(ERR, "%s(): Error with parent destructor", __func__);
        ret = false;
    }

    /* remove the interface from linux */
    osn_tap_del(self->in_tap_osn);

    return ret;
}

bool inet_tap_init(inet_tap_t *self, const char *ifname)
{
    bool success;

    /* ititialize the parent class -- inet_eth */
    success = inet_eth_init(&self->eth, ifname);
    if (!success)
    {
        LOG(ERR, "%s: Failed to instantiate class for %s, inet_eth_init() failed .",
            __func__, ifname);
        return false;
    }

    self->base.in_service_commit_fn = inet_tap_service_commit;

    self->inet.in_dtor_fn = inet_tap_dtor;
    return true;
}

/**
 * New-type constructor
 */
inet_t *inet_tap_new(const char *ifname)
{
    int rc;

    TRACE();
    inet_tap_t *self = NULL;

    self = CALLOC(1, sizeof(*self));
    if (self == NULL) goto error;

    rc = inet_tap_init(self, ifname);
    if (!rc)
    {
        LOG(ERR, "inet_tap: %s: Failed to initialize interface instance.", ifname);
        goto error;
    }

    return (inet_t *)self;

error:
    FREE(self);
    return NULL;
}

/*
 * Destructor, called by inet_del()
 */
bool inet_tap_dtor(inet_t *super)
{

    inet_tap_t *self = CONTAINER_OF(super, inet_tap_t, inet);

    TRACE();
    return inet_tap_fini(self);
}

bool inet_tap_service_IF_CREATE(inet_tap_t *self, bool enable)
{
    /* enable flag is true when interface is created and false when
     * interface is removed from the config table */
    if (!enable) return true;

    self->in_tap_osn = osn_tap_new(self->inet.in_ifname);
    if (self->in_tap_osn == NULL)
    {
        LOGT("%s(): Error creating OSN TAP object for %s ", __func__,
             self->inet.in_ifname);
        return false;
    }


    osn_tap_apply(self->in_tap_osn);

    return true;
}

bool inet_tap_service_commit(inet_base_t *super, enum inet_base_services srv, bool enable)
{
    bool success = true;
    inet_tap_t *self = CONTAINER_OF(super, inet_tap_t, base);

    LOG(DEBUG, "inet_tap: %s: Service %s -> %s.", self->inet.in_ifname,
        inet_base_service_str(srv), enable ? "start" : "stop");

    switch (srv)
    {
    case INET_BASE_IF_CREATE:
        success = inet_tap_service_IF_CREATE(self, enable);
        break;

    default:
        break;
    }

    if (!success) return false;

    /* Delegate other things to the parent service */
    return inet_eth_service_commit(super, srv, enable);

    return true;
}
