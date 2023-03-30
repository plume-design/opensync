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
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "osn_route_rule.h"
#include "lnx_route_rule.h"
#include "memutil.h"
#include "log.h"

struct osn_route_rule
{
    lnx_route_rule_t    lr_rule;
};

osn_route_rule_t *osn_route_rule_new(void)
{
    osn_route_rule_t *self = CALLOC(1, sizeof(osn_route_rule_t));

    if (!lnx_route_rule_init(&self->lr_rule))
    {
        LOG(ERR, "osn_route_rule: Error initializing route rule object.");
        FREE(self);
        return NULL;
    }

    return self;
}

bool osn_route_rule_apply(osn_route_rule_t *self)
{
    return lnx_route_rule_apply(&self->lr_rule);
}

bool osn_route_rule_del(osn_route_rule_t *self)
{
    bool rv;

    rv = lnx_route_rule_fini(&self->lr_rule);
    FREE(self);

    return rv;
}

bool osn_route_rule_add(osn_route_rule_t *self, const osn_route_rule_cfg_t *route_rule_cfg)
{
    return lnx_route_rule_add(&self->lr_rule, route_rule_cfg);
}

bool osn_route_rule_remove(osn_route_rule_t *self, const osn_route_rule_cfg_t *route_rule_cfg)
{
    return lnx_route_rule_remove(&self->lr_rule, route_rule_cfg);
}
