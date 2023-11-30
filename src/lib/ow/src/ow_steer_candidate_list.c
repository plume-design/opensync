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

#include <assert.h>
#include <stdlib.h>
#include <memutil.h>
#include <const.h>
#include <util.h>
#include <log.h>
#include <module.h>
#include <osw_ut.h>
#include <osw_types.h>
#include <osw_bss_map.h>
#include <osw_diag.h>
#include "ow_steer_candidate_assessor.h"
#include "ow_steer_candidate_list.h"

struct ow_steer_candidate {
    struct osw_hwaddr bssid;
    enum ow_steer_candidate_preference preference;
    struct osw_channel channel;
    unsigned int metric;
    char *reason;
};

struct ow_steer_candidate_list {
    struct ow_steer_candidate *set;
    size_t set_size;
};

static int
ow_steer_candidate_metric_cmp(const struct ow_steer_candidate *a,
                              const struct ow_steer_candidate *b)
{
    if (a->metric < b->metric)
        return 1;
    else if (a->metric > b->metric)
        return -1;
    else
        return 0;
}

static int
ow_steer_candidate_list_qsort_cmp(const void *a,
                                  const void *b)
{
    const struct ow_steer_candidate *x = a;
    const struct ow_steer_candidate *y = b;
    int d;

    d = ow_steer_candidate_metric_cmp(x, y);
    if (d != 0) return d;

    d = osw_hwaddr_cmp(&x->bssid, &y->bssid);
    if (d != 0) return d;

    return 0;
}

static void
ow_steer_candidate_list_candidate_init(struct ow_steer_candidate *candidate,
                                       const struct osw_hwaddr *bssid,
                                       const struct osw_channel *channel)
{
    assert(candidate != NULL);
    assert(channel != NULL);

    memset(candidate, 0, sizeof(*candidate));
    memcpy(&candidate->bssid, bssid, sizeof(candidate->bssid));
    memcpy(&candidate->channel, channel, sizeof(candidate->channel));
}

const char*
ow_steer_candidate_preference_to_cstr(enum ow_steer_candidate_preference preference)
{
    switch(preference) {
        case OW_STEER_CANDIDATE_PREFERENCE_NONE:
            return "none";
        case OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE:
            return "available";
        case OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED:
            return "hard-blocked";
        case OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED:
            return "soft-blocked";
        case OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE:
            return "out-of-scope";
    }

    return "(unknown preference)";
}

struct ow_steer_candidate_list*
ow_steer_candidate_list_new(void)
{
    return CALLOC(1, sizeof(struct ow_steer_candidate_list));
}

void
ow_steer_candidate_list_free(struct ow_steer_candidate_list *candidate_list)
{
    if (candidate_list == NULL)
        return;

    ow_steer_candidate_list_clear(candidate_list);
    FREE(candidate_list->set);
    FREE(candidate_list);
}

static void
ow_steer_candidate_clear(struct ow_steer_candidate *c)
{
    FREE(c->reason);
    c->preference = OW_STEER_CANDIDATE_PREFERENCE_NONE;
    c->metric = 0;
    c->reason = NULL;
}

static void
ow_steer_candidate_clone(struct ow_steer_candidate *c)
{
    c->reason = c->reason ? STRDUP(c->reason) : NULL;
}

struct ow_steer_candidate_list*
ow_steer_candidate_list_copy(const struct ow_steer_candidate_list *candidate_list)
{
    assert(candidate_list != NULL);

    struct ow_steer_candidate_list *copy = CALLOC(1, sizeof(*copy));

    if (candidate_list->set_size == 0)
        return copy;

    copy->set = MEMNDUP(candidate_list->set, candidate_list->set_size * sizeof(struct ow_steer_candidate));
    copy->set_size = candidate_list->set_size;
    size_t i;
    for (i = 0; i < copy->set_size; i++) {
        ow_steer_candidate_clone(&copy->set[i]);
    }

    return copy;
}

static bool
ow_steer_candidate_reason_is_equal(const char *a,
                                   const char *b)
{
    return ((a == NULL) && (b == NULL))
        || ((a != NULL) && (b != NULL) && (strcmp(a, b) == 0));
}

static bool
ow_steer_candidate_is_equal(const struct ow_steer_candidate *a,
                            const struct ow_steer_candidate *b)
{
    return osw_hwaddr_is_equal(&a->bssid, &b->bssid)
        && osw_channel_is_equal(&a->channel, &b->channel)
        && (a->metric == b->metric)
        && (a->preference == b->preference)
        && ow_steer_candidate_reason_is_equal(a->reason, b->reason);
}

bool
ow_steer_candidate_list_cmp(const struct ow_steer_candidate_list *candidate_list_a,
                            const struct ow_steer_candidate_list *candidate_list_b)
{
    assert(candidate_list_a != NULL);
    assert(candidate_list_b != NULL);

    if (candidate_list_a->set_size != candidate_list_b->set_size)
        return false;

    size_t i;
    for (i = 0; i < candidate_list_a->set_size; i++) {
        const struct ow_steer_candidate *a = &candidate_list_a->set[i];
        const struct ow_steer_candidate *b = &candidate_list_b->set[i];
        if (ow_steer_candidate_is_equal(a, b) == false) return false;
    }

    return true;
}

void
ow_steer_candidate_list_bss_set(struct ow_steer_candidate_list *candidate_list,
                                const struct osw_hwaddr *bssid,
                                const struct osw_channel *channel)
{
    assert(candidate_list != NULL);
    assert(bssid != NULL);

    size_t i = 0;
    for (i = 0; i < candidate_list->set_size; i++)
        if (osw_hwaddr_cmp(&candidate_list->set[i].bssid, bssid) == 0)
            break;

    if (i == candidate_list->set_size) {
        candidate_list->set_size++;
        candidate_list->set = REALLOC(candidate_list->set, sizeof(struct ow_steer_candidate) * candidate_list->set_size);
    }

    ow_steer_candidate_list_candidate_init(&candidate_list->set[i], bssid, channel);
}

void
ow_steer_candidate_list_bss_unset(struct ow_steer_candidate_list *candidate_list,
                                  const struct osw_hwaddr *bssid)
{
    assert(candidate_list != NULL);
    assert(bssid != NULL);

    struct ow_steer_candidate *new_set;
    struct ow_steer_candidate *found = ow_steer_candidate_list_lookup(candidate_list, bssid);
    size_t new_set_size;
    size_t new_i = 0;
    size_t i = 0;

    if (found == NULL) return;

    new_set_size = candidate_list->set_size - 1;
    new_set = CALLOC(new_set_size, sizeof(struct ow_steer_candidate));

    for (i = 0; i < candidate_list->set_size; i++) {
        if (osw_hwaddr_cmp(&candidate_list->set[i].bssid, bssid) == 0)
            continue;

        new_set[new_i] = candidate_list->set[i];
        new_i++;
    }

    ow_steer_candidate_clear(found);
    FREE(candidate_list->set);
    candidate_list->set = new_set;
    candidate_list->set_size = new_set_size;
}

void
ow_steer_candidate_list_clear(struct ow_steer_candidate_list *candidate_list)
{
    assert(candidate_list != NULL);

    size_t i = 0;
    for (i = 0; i < candidate_list->set_size; i++) {
        ow_steer_candidate_clear(&candidate_list->set[i]);
    }
}

struct ow_steer_candidate*
ow_steer_candidate_list_lookup(struct ow_steer_candidate_list *candidate_list,
                               const struct osw_hwaddr *bssid)
{
    assert(candidate_list != NULL);

    size_t i = 0;
    for (i = 0; i < candidate_list->set_size; i++)
        if (osw_hwaddr_cmp(&candidate_list->set[i].bssid, bssid) == 0)
            return &candidate_list->set[i];

    return NULL;
}

const struct ow_steer_candidate*
ow_steer_candidate_list_const_lookup(const struct ow_steer_candidate_list *candidate_list,
                                     const struct osw_hwaddr *bssid)
{
    assert(candidate_list != NULL);

    size_t i = 0;
    for (i = 0; i < candidate_list->set_size; i++)
        if (osw_hwaddr_cmp(&candidate_list->set[i].bssid, bssid) == 0)
            return &candidate_list->set[i];

    return NULL;
}

struct ow_steer_candidate*
ow_steer_candidate_list_get(struct ow_steer_candidate_list *candidate_list,
                            size_t index)
{
    assert(candidate_list != NULL);
    assert(index < candidate_list->set_size);
    return candidate_list->set + index;
}

const struct ow_steer_candidate*
ow_steer_candidate_list_const_get(const struct ow_steer_candidate_list *candidate_list,
                                  size_t index)
{
    assert(candidate_list != NULL);
    assert(index < candidate_list->set_size);
    return candidate_list->set + index;
}

size_t
ow_steer_candidate_list_get_length(const struct ow_steer_candidate_list *candidate_list)
{
    assert(candidate_list != NULL);
    return candidate_list->set_size;
}

void
ow_steer_candidate_list_sigusr1_dump(osw_diag_pipe_t *pipe,
                                     struct ow_steer_candidate_list *candidate_list)
{
    assert(candidate_list != NULL);

    size_t i = 0;
    for (i = 0; i < candidate_list->set_size; i++) {
        osw_diag_pipe_writef(pipe, "ow: steer:      candidate:");
        osw_diag_pipe_writef(pipe, "ow: steer:        bssid: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&candidate_list->set[i].bssid));
        osw_diag_pipe_writef(pipe, "ow: steer:        preference: %s (%s)",
                             ow_steer_candidate_preference_to_cstr(candidate_list->set[i].preference),
                             ow_steer_candidate_get_reason(&candidate_list->set[i]));
    }
}

void
ow_steer_candidate_list_sort(struct ow_steer_candidate_list *candidate_list,
                             struct ow_steer_candidate_assessor *candidate_assessor)
{
    assert(candidate_list != NULL);

    if (candidate_assessor != NULL) {
        ow_steer_candidate_assessor_assess(candidate_assessor, candidate_list);
    }

    qsort(candidate_list->set, candidate_list->set_size, sizeof (*candidate_list->set), ow_steer_candidate_list_qsort_cmp);
}

const struct osw_hwaddr*
ow_steer_candidate_get_bssid(const struct ow_steer_candidate *candidate)
{
    assert(candidate != NULL);
    return &candidate->bssid;
}

enum ow_steer_candidate_preference
ow_steer_candidate_get_preference(const struct ow_steer_candidate *candidate)
{
    assert(candidate != NULL);
    return candidate->preference;
}

static void
ow_steer_candidate_set_reason(struct ow_steer_candidate *candidate,
                              const char *reason)
{
    FREE(candidate->reason);
    candidate->reason = STRDUP(reason);
}

void
ow_steer_candidate_set_preference(struct ow_steer_candidate *candidate,
                                  const char *reason,
                                  enum ow_steer_candidate_preference preference)
{
    assert(candidate != NULL);

    if (candidate->preference != OW_STEER_CANDIDATE_PREFERENCE_NONE) {
        LOGW("ow: steer: candidate: cannot override "OSW_HWADDR_FMT" preference to %s with reason %s, because %s already set it to %s",
             OSW_HWADDR_ARG(&candidate->bssid),
             candidate->reason,
             ow_steer_candidate_preference_to_cstr(candidate->preference),
             reason,
             ow_steer_candidate_preference_to_cstr(preference));
        return;
    }

    ow_steer_candidate_set_reason(candidate, reason);
    candidate->preference = preference;
}

void
ow_steer_candidate_inc_metric(struct ow_steer_candidate *candidate,
                              unsigned int value)
{
    assert(candidate != NULL);
    candidate->metric += value;
}

void
ow_steer_candidate_set_metric(struct ow_steer_candidate *candidate,
                              unsigned int value)
{
    assert(candidate != NULL);
    candidate->metric = value;
}

unsigned int
ow_steer_candidate_get_metric(const struct ow_steer_candidate *candidate)
{
    assert(candidate != NULL);
    return candidate->metric;
}

const struct osw_channel*
ow_steer_candidate_get_channel(const struct ow_steer_candidate *candidate)
{
    assert(candidate != NULL);
    return &candidate->channel;
}

const char *
ow_steer_candidate_get_reason(const struct ow_steer_candidate *candidate)
{
    assert(candidate != NULL);
    return candidate->reason ?: "(null)";
}

#include "ow_steer_candidate_list_ut.c"
