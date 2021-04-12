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

#include "memutil.h"
#include "log.h"
#include "util.h"

#include "inet.h"
#include "inet_base.h"
#include "inet_lte.h"
#include "osn_lte.h"

/**
 * New-type constructor
 */
inet_t *inet_lte_new(const char *ifname)
{
    int rc;

    inet_lte_t *self = NULL;

    self = CALLOC(1, sizeof(*self));
    if (self == NULL) goto error;

    rc = inet_lte_init(self, ifname);
    if (!rc)
    {
        LOG(ERR, "inet_lte: %s: Failed to initialize interface instance.", ifname);
        goto error;
    }

    return (inet_t *)self;

 error:
    FREE(self);
    return NULL;
}

bool inet_lte_init(inet_lte_t *self, const char *ifname)
{
    int rc;

    if (osn_lte_new(ifname) == NULL)
    {
        LOG(ERR, "inet_lte: %s: Failed to instantiate class, osn_lte_new() failed.", ifname);
        return false;
    }

    rc = inet_eth_init(&self->eth, ifname);
    if (!rc)
    {
        LOG(ERR, "inet_lte: %s: Failed to instantiate class, inet_eth_init() failed.", ifname);
        return false;
    }

    return true;
}
