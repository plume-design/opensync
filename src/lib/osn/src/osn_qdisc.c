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

#include "osn_qdisc.h"
#include "lnx_qdisc.h"

#include "memutil.h"
#include "const.h"
#include "util.h"
#include "log.h"

struct osn_qdisc_cfg
{
    lnx_qdisc_cfg_t *oqc_lnx_qdisc;
};

osn_qdisc_cfg_t *osn_qdisc_cfg_new(const char *if_name)
{
    osn_qdisc_cfg_t *self = CALLOC(1, sizeof(osn_qdisc_cfg_t));

    self->oqc_lnx_qdisc = lnx_qdisc_cfg_new(if_name);
    if (self->oqc_lnx_qdisc == NULL)
    {
        FREE(self);
        return NULL;
    }
    return self;
}

bool osn_qdisc_cfg_add(osn_qdisc_cfg_t *self, const struct osn_qdisc_params *qdisc)
{
    return lnx_qdisc_cfg_add(self->oqc_lnx_qdisc, qdisc);
}

bool osn_qdisc_cfg_apply(osn_qdisc_cfg_t *self)
{
    return lnx_qdisc_cfg_apply(self->oqc_lnx_qdisc);
}

bool osn_qdisc_cfg_reset(osn_qdisc_cfg_t *self)
{
    return lnx_qdisc_cfg_reset(self->oqc_lnx_qdisc);
}

bool osn_qdisc_cfg_del(osn_qdisc_cfg_t *self)
{
    bool rv = true;

    rv &= lnx_qdisc_cfg_del(self->oqc_lnx_qdisc);
    FREE(self);

    return rv;
}

struct osn_qdisc_params *osn_qdisc_params_clone(const struct osn_qdisc_params *qdisc)
{
    struct osn_qdisc_params *qdisc_clone;

    if (qdisc == NULL) return NULL;

    qdisc_clone = CALLOC(1, sizeof(struct osn_qdisc_params));

    qdisc_clone->oq_id = qdisc->oq_id != NULL ? STRDUP(qdisc->oq_id) : NULL;
    qdisc_clone->oq_parent_id = qdisc->oq_parent_id != NULL ? STRDUP(qdisc->oq_parent_id) : NULL;
    qdisc_clone->oq_qdisc = qdisc->oq_qdisc != NULL ? STRDUP(qdisc->oq_qdisc) : NULL;
    qdisc_clone->oq_params = qdisc->oq_params != NULL ? STRDUP(qdisc->oq_params) : NULL;

    qdisc_clone->oq_is_class = qdisc->oq_is_class;

    return qdisc_clone;
}

void osn_qdisc_params_del(struct osn_qdisc_params *qdisc)
{
    FREE(qdisc->oq_id);
    FREE(qdisc->oq_parent_id);
    FREE(qdisc->oq_qdisc);
    FREE(qdisc->oq_params);

    FREE(qdisc);
}

/*
 * Convert struct osn_qdisc_params to string and write to @p buf, return @p buf.
 */
char *__FMT_osn_qdisc_params(char *buf, size_t sz, const struct osn_qdisc_params *qdisc)
{
    snprintf(
            buf,
            sz,
            "%s: %s, parent=%s, id=%s, params='%s'",
            qdisc->oq_is_class ? "class" : "qdisc",
            qdisc->oq_qdisc,
            qdisc->oq_parent_id,
            qdisc->oq_id,
            qdisc->oq_params);

    return buf;
}
