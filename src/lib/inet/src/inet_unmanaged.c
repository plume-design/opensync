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
#include <net/if.h>
#include <arpa/inet.h>
#include <errno.h>

#include <string.h>

#include "log.h"
#include "util.h"
#include "memutil.h"

#include "inet.h"
#include "inet_base.h"
#include "inet_unmanaged.h"

/**
 * New-type constructor
 */
inet_t *inet_unmanaged_new(const char *ifname)
{
    inet_unmanaged_t *self = NULL;

    self = MALLOC(sizeof(*self));

    if (!inet_unmanaged_init(self, ifname))
    {
        LOG(ERR, "inet_vif: %s: Failed to initialize interface instance.", ifname);
        goto error;
    }

    return (inet_t *)self;

 error:
    if (self != NULL) FREE(self);
    return NULL;
}

bool inet_unmanaged_init(inet_unmanaged_t *self, const char *ifname)
{
    if (!inet_eth_init(&self->eth, ifname))
    {
        LOG(ERR, "inet_vif: %s: Failed to instantiate class, inet_eth_init() failed.", ifname);
        return false;
    }

    self->base.in_service_commit_fn = inet_unmanaged_service_commit;
    self->inet.in_noflood_set_fn = NULL;

    return true;
}

bool inet_unmanaged_service_commit(inet_base_t *super, enum inet_base_services srv, bool enable)
{
    inet_unmanaged_t *self = (inet_unmanaged_t *)super;

    LOG(INFO, "inet_unmanaged: %s: Service %s -> %s.",
            self->inet.in_ifname,
            inet_base_service_str(srv),
            enable ? "start" : "stop");

    return true;
}
