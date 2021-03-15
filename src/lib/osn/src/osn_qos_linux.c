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

#include "lnx_qos.h"

struct osn_qos
{
    lnx_qos_t   oq_lnx;
};

osn_qos_t* osn_qos_new(const char *ifname)
{
    osn_qos_t *self = calloc(1, sizeof(*self));

    if (!lnx_qos_init(&self->oq_lnx, ifname))
    {
        free(self);
        return NULL;
    }

    return self;
}

void osn_qos_del(osn_qos_t *self)
{
    lnx_qos_fini(&self->oq_lnx);
    free(self);
}

bool osn_qos_apply(osn_qos_t *self)
{
    return lnx_qos_apply(&self->oq_lnx);
}

bool osn_qos_begin(osn_qos_t *self, struct osn_qos_other_config *other_config)
{
    return lnx_qos_begin(&self->oq_lnx, other_config);
}

bool osn_qos_end(osn_qos_t *self)
{
    return lnx_qos_end(&self->oq_lnx);
}

bool osn_qos_queue_begin(
        osn_qos_t *self,
        int priority,
        int bandwidth,
        const char *tag,
        const struct osn_qos_other_config *other_config,
        struct osn_qos_queue_status *qqs)
{
    return lnx_qos_queue_begin(
            &self->oq_lnx,
            priority,
            bandwidth,
            tag,
            other_config,
            qqs);
}

bool osn_qos_queue_end(osn_qos_t *self)
{
    return lnx_qos_queue_end(&self->oq_lnx);
}
