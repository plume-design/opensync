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

#ifndef OW_STEER_CANDIDATE_ASSESSOR_I_H
#define OW_STEER_CANDIDATE_ASSESSOR_I_H

struct ow_steer_candidate_assessor;

typedef bool
ow_steer_candidate_assessor_assess_fn_t(struct ow_steer_candidate_assessor *assessor,
                                        struct ow_steer_candidate_list *candidate_list);

typedef void
ow_steer_candidate_assessor_free_priv_fn_t(struct ow_steer_candidate_assessor *assessor);

struct ow_steer_candidate_assessor_ops {
    ow_steer_candidate_assessor_assess_fn_t *assess_fn;
    ow_steer_candidate_assessor_free_priv_fn_t *free_priv_fn;
};

struct ow_steer_candidate_assessor {
    struct osw_hwaddr sta_addr;
    const char *log_prefix;
    struct ow_steer_candidate_assessor_ops ops;
    void *priv;
};

struct ow_steer_candidate_assessor*
ow_steer_candidate_assessor_create(const struct osw_hwaddr *sta_addr,
                                   const struct ow_steer_candidate_assessor_ops *ops,
                                   const char *log_prefix,
                                   void *priv);

#endif /* OW_STEER_CANDIDATE_ASSESSOR_I_H */
