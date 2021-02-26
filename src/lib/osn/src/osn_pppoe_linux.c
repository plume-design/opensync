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

#include "daemon.h"
#include "log.h"

#include "osn_pppoe.h"
#include "lnx_pppoe.h"

struct osn_pppoe
{
    lnx_pppoe_t             op_pppoe;
    void                   *op_data;
    osn_pppoe_status_fn_t  *op_status_fn;
};

static lnx_pppoe_status_fn_t osn_pppoe_status_fn;

osn_pppoe_t *osn_pppoe_new(const char *ifname)
{
    osn_pppoe_t *self;

    self = calloc(1, sizeof(osn_pppoe_t));
    if (self == NULL)
    {
        LOG(ERR, "osn_pppoe: %s: Error allocating PPPoE object.", ifname);
        return NULL;
    }

    if (!lnx_pppoe_init(&self->op_pppoe, ifname))
    {
        LOG(ERR, "osn_pppoe: %s: Error initializing PPPoE object.", ifname);
        free(self);
        return NULL;
    }

    return self;
}

bool osn_pppoe_del(osn_pppoe_t *self)
{
    bool retval = true;

    if (!lnx_pppoe_fini(&self->op_pppoe))
    {
        LOG(WARN, "osn_pppoe: %s: Error destroying PPPoE object.", self->op_pppoe.lp_ifname);
        retval = false;
    }

    free(self);

    return retval;
}

bool osn_pppoe_apply(osn_pppoe_t *self)
{
    return lnx_pppoe_apply(&self->op_pppoe);
}

void osn_pppoe_data_set(osn_pppoe_t *self, void *data)
{
    self->op_data = data;
}

void *osn_pppoe_data_get(osn_pppoe_t *self)
{
    return self->op_data;
}

bool osn_pppoe_parent_set(osn_pppoe_t *self, const char *parent_ifname)
{
    return lnx_pppoe_parent_set(&self->op_pppoe, parent_ifname);
}


bool osn_pppoe_secret_set(osn_pppoe_t *self, const char *username, const char *password)
{
    return lnx_pppoe_secret_set(&self->op_pppoe, username, password);
}

void osn_pppoe_status_fn(lnx_pppoe_t *lnx, struct osn_pppoe_status *status)
{
    osn_pppoe_t *self = CONTAINER_OF(lnx, osn_pppoe_t, op_pppoe);

    if (self->op_status_fn != NULL)
    {
        self->op_status_fn(self, status);
    }
}

void osn_pppoe_status_notify(osn_pppoe_t *self, osn_pppoe_status_fn_t *fn)
{
    self->op_status_fn = fn;

    lnx_pppoe_status_notify(&self->op_pppoe, (fn == NULL) ? NULL : osn_pppoe_status_fn);
}

