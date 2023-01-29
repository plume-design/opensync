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

#ifndef OW_STEER_CANDIDATE_LIST_H
#define OW_STEER_CANDIDATE_LIST_H

struct ow_steer_candidate_list;
struct ow_steer_candidate;
struct ow_steer_candidate_assessor; /* extern */

enum ow_steer_candidate_preference {
    OW_STEER_CANDIDATE_PREFERENCE_NONE = 0,
    OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE,
    OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED, /* Kicked if connected and blocked on ACL */
    OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED, /* Only blocked on ACL */
    OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE, /* Not suitable for steering purposes, effectively ignored */
};

const char*
ow_steer_candidate_preference_to_cstr(enum ow_steer_candidate_preference preference);

struct ow_steer_candidate_list*
ow_steer_candidate_list_new(void);

void
ow_steer_candidate_list_free(struct ow_steer_candidate_list *candidate_list);

struct ow_steer_candidate_list*
ow_steer_candidate_list_copy(const struct ow_steer_candidate_list *candidate_list);

bool
ow_steer_candidate_list_cmp(const struct ow_steer_candidate_list *candidate_list_a,
                            const struct ow_steer_candidate_list *candidate_list_b);

void
ow_steer_candidate_list_bss_set(struct ow_steer_candidate_list *candidate_list,
                                const struct osw_hwaddr *bssid,
                                const struct osw_channel *channel);

void
ow_steer_candidate_list_bss_unset(struct ow_steer_candidate_list *candidate_list,
                                  const struct osw_hwaddr *bssid);

void
ow_steer_candidate_list_clear(struct ow_steer_candidate_list *candidate_list);

struct ow_steer_candidate*
ow_steer_candidate_list_lookup(struct ow_steer_candidate_list *candidate_list,
                               const struct osw_hwaddr *bssid);

const struct ow_steer_candidate*
ow_steer_candidate_list_const_lookup(const struct ow_steer_candidate_list *candidate_list,
                                     const struct osw_hwaddr *bssid);

struct ow_steer_candidate*
ow_steer_candidate_list_get(struct ow_steer_candidate_list *candidate_list,
                            size_t index);

const struct ow_steer_candidate*
ow_steer_candidate_list_const_get(const struct ow_steer_candidate_list *candidate_list,
                                  size_t index);

size_t
ow_steer_candidate_list_get_length(const struct ow_steer_candidate_list *candidate_list);

void
ow_steer_candidate_list_sigusr1_dump(struct ow_steer_candidate_list *candidate_list);

void
ow_steer_candidate_list_sort(struct ow_steer_candidate_list *candidate_list,
                             struct ow_steer_candidate_assessor *candidate_assessor);

const struct osw_hwaddr*
ow_steer_candidate_get_bssid(const struct ow_steer_candidate *candidate);

enum ow_steer_candidate_preference
ow_steer_candidate_get_preference(const struct ow_steer_candidate *candidate);

void
ow_steer_candidate_set_preference(struct ow_steer_candidate *candidate,
                                   enum ow_steer_candidate_preference preference);

void
ow_steer_candidate_inc_metric(struct ow_steer_candidate *candidate,
                              unsigned int value);

void
ow_steer_candidate_set_metric(struct ow_steer_candidate *candidate,
                              unsigned int value);

unsigned int
ow_steer_candidate_get_metric(const struct ow_steer_candidate *candidate);

const struct osw_channel*
ow_steer_candidate_get_channel(const struct ow_steer_candidate *candidate);

#endif /* OW_STEER_CANDIDATE_LIST_H */
