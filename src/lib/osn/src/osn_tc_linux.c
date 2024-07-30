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

#include "log.h"
#include "memutil.h"

#include "osn_tc.h"
#include "lnx_tc.h"


struct osn_tc
{
    lnx_tc_t   ot_lnx;
};

osn_tc_t* osn_tc_new(const char *ifname)
{
    osn_tc_t *self = CALLOC(1, sizeof(*self));

    if (!lnx_tc_init(&self->ot_lnx, ifname))
    {
        FREE(self);
        return NULL;
    }

    return self;
}

void osn_tc_del(osn_tc_t *self)
{
    lnx_tc_fini(&self->ot_lnx);
    FREE(self);
}

void osn_tc_set_reset_egress(osn_tc_t *self, bool reset)
{
    return lnx_tc_set_reset_egress(&self->ot_lnx, reset);
}

bool osn_tc_apply(osn_tc_t *self)
{
    return lnx_tc_apply(&self->ot_lnx);
}

bool osn_tc_begin(osn_tc_t *self)
{
    return lnx_tc_begin(&self->ot_lnx);
}

bool osn_tc_end(osn_tc_t *self)
{
    return lnx_tc_end(&self->ot_lnx);
}

bool osn_tc_filter_begin(
        osn_tc_t *self,
        int   priority,
        bool  ingress,
        char  *match,
        char  *action)
{
    return lnx_tc_filter_begin(
            &self->ot_lnx,
            ingress,
            priority,
            match,
            action);
}

bool osn_tc_filter_end(osn_tc_t *self)
{
    return lnx_tc_filter_end(&self->ot_lnx);
}
