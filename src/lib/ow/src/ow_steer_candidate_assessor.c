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

#include <stdint.h>
#include <osa_assert.h>
#include <memutil.h>
#include <osw_types.h>
#include <util.h>
#include <log.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_candidate_assessor.h"
#include "ow_steer_candidate_assessor_i.h"

#define LOG_PREFIX(fmt, ...) \
    "ow: steer: " fmt, ##__VA_ARGS__
#define LOG_WITH_PREFIX(assessor, fmt, ...)                         \
    LOG_PREFIX(                                                     \
        "%s" fmt,                                                   \
        ((assessor) != NULL ? ((assessor)->log_prefix ?: "") : ""), \
        ##__VA_ARGS__)

struct ow_steer_candidate_assessor*
ow_steer_candidate_assessor_create(const struct osw_hwaddr *sta_addr,
                                   const struct ow_steer_candidate_assessor_ops *ops,
                                   const char *log_prefix,
                                   void *priv)
{
    ASSERT(sta_addr != NULL, "");
    ASSERT(ops != NULL, "");

    struct ow_steer_candidate_assessor *assessor = CALLOC(1, sizeof(*assessor));

    memcpy(&assessor->sta_addr, sta_addr, sizeof(assessor->sta_addr));
    memcpy(&assessor->ops, ops, sizeof(assessor->ops));

    assessor->log_prefix = strfmt("%sassessor: ", log_prefix);
    assessor->priv = priv;

    return assessor;
}

bool
ow_steer_candidate_assessor_assess(struct ow_steer_candidate_assessor *assessor,
                                   struct ow_steer_candidate_list *candidate_list)
{
    ASSERT(assessor != NULL, "");
    ASSERT(candidate_list != NULL, "");

    ASSERT(assessor->ops.assess_fn != NULL, "");
    const bool result = assessor->ops.assess_fn(assessor, candidate_list);

    size_t i;
    for (i = 0; i < ow_steer_candidate_list_get_length(candidate_list); i++) {
        const struct ow_steer_candidate *candidate = ow_steer_candidate_list_get(candidate_list, i);
        const struct osw_hwaddr *bssid = ow_steer_candidate_get_bssid(candidate);
        const unsigned int metric = ow_steer_candidate_get_metric(candidate);
        LOGT(LOG_WITH_PREFIX(assessor, "candidate: "OSW_HWADDR_FMT": metric: %u", OSW_HWADDR_ARG(bssid), metric));
    }

    return result;
}

void
ow_steer_candidate_assessor_free(struct ow_steer_candidate_assessor *assessor)
{
    if (assessor != NULL)
    {
        FREE(assessor->log_prefix);
        if (assessor->ops.free_priv_fn != NULL)
            assessor->ops.free_priv_fn(assessor);
    }
    FREE(assessor);
}

const struct osw_hwaddr*
ow_steer_candidate_assessor_get_sta_addr(const struct ow_steer_candidate_assessor *assessor)
{
    ASSERT(assessor != NULL, "");
    return &assessor->sta_addr;
}
