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
#include "memutil.h"

#include "linux/lnx_tap.h"

#include "osn_tap.h"

struct osn_tap
{
    lnx_tap_t  ot_tap;
};

osn_tap_t *osn_tap_new(const char *ifname)
{
    bool success;

    osn_tap_t *self = CALLOC(1, sizeof(osn_tap_t));
    success =  lnx_tap_init(&self->ot_tap, ifname);
    if (!success)
    {
        LOG(ERR, "%s(): Error initializing the TAP object for %s", __func__, ifname);
        FREE(self);
        return NULL;
    }
    return self;
}

bool osn_tap_del(osn_tap_t *self)
{
    bool ret;

    /* remove the interface from linux */
    ret = lnx_tap_fini(&self->ot_tap);
    if (ret == false)
    {
        LOG(WARN, "%s(): Error destroying %s TAP object.", __func__, self->ot_tap.lt_ifname);
    }

    FREE(self);
    return ret;
}

bool osn_tap_apply(osn_tap_t *self)
{
    return lnx_tap_apply(&self->ot_tap);
}
