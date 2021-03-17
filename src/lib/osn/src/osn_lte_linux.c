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

#include "linux/lnx_lte.h"

#include "osn_lte.h"

struct osn_lte
{
    lnx_lte_t  ov_lte;
};

osn_lte_t *osn_lte_new(const char *ifname)
{
    osn_lte_t *self = calloc(1, sizeof(osn_lte_t));

    if (self == NULL)
    {
        LOG(ERR, "osn_lte: %s: Error allocating the LTE object.", ifname);
        return NULL;
    }

    if (!lnx_lte_init(&self->ov_lte, ifname))
    {
        LOG(ERR, "osn_lte: %s: Error initializing the LTE object.", ifname);
        free(self);
        return NULL;
    }

    return self;
}

bool osn_lte_del(osn_lte_t *self)
{
    bool retval = true;

    if (!lnx_lte_fini(&self->ov_lte))
    {
        LOG(WARN, "osn_lte: %s: Error destroying the LTE object.", self->ov_lte.ll_ifname);
        retval = false;
    }

    free(self);

    return retval;
}
