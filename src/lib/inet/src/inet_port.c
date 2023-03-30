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

#include <errno.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "const.h"
#include "execsh.h"
#include "inet_base.h"
#include "inet_port.h"
#include "log.h"
#include "memutil.h"
#include "util.h"

bool inet_port_fini(inet_port_t *self)
{
    /* no action is required. */
    TRACE();
    return true;
}

/*
 * Destructor, called by inet_del()
 */
bool inet_port_dtor(inet_t *super)
{
    inet_port_t *self = CONTAINER_OF(super, inet_port_t, inet);

    LOGT("%s(): %s destructor called", __func__, self->in_port_ifname);
    return inet_port_fini(self);
}

/**
 * Initializer
 */
bool inet_port_init(inet_port_t *self, const char *ifname)
{
    int ret;

    TRACE();
    memset(self, 0, sizeof(*self));

    ret = strscpy(self->in_port_ifname, ifname, sizeof(self->in_port_ifname));
    if (ret < 0)
    {
        LOG(ERR, "%s(): Interface name %s too long.", __func__, ifname);
        return false;
    }

    LOGD("%s(): initializing port inet for %s", __func__, ifname);

    /* override inet destructor */
    self->inet.in_dtor_fn = inet_port_dtor;

    return true;
}

/*
 * ===========================================================================
 *  Public API
 * ===========================================================================
 */

/**
 * Default inet_port class constructor
 */
inet_t *inet_port_new(const char *ifname)
{
    inet_port_t *self;
    bool success;

    /* memory will be freed when inet_del() is called */
    self = CALLOC(1, sizeof(*self));
    if (self == NULL)
    {
        LOG(ERR, "%s(): Unable to allocate inet port object for %s", __func__, ifname);
        return NULL;
    }

    LOGT("%s(): initializing port inet for %s", __func__, ifname);
    success = inet_port_init(self, ifname);
    if (!success)
    {
        LOGE("%s(): port initialization failed for %s", __func__, ifname);
        FREE(self);
        return NULL;
    }

    return &self->inet;
}
