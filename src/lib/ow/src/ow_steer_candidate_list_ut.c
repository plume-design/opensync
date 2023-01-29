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

#include <ev.h>
#include <module.h>
#include <osw_ut.h>
#include "ow_steer_candidate_assessor.h"
#include "ow_steer_candidate_assessor_i.h"

static bool
ow_steer_candidate_list_ut_create_sort_assess_cb(struct ow_steer_candidate_assessor *assessor,
                                                 struct ow_steer_candidate_list *candidate_list)
{
    const struct osw_hwaddr bssid_a = { .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, }, };
    const struct osw_hwaddr bssid_b = { .octet = { 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, }, };
    const struct osw_hwaddr bssid_c = { .octet = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, }, };
    const struct osw_hwaddr bssid_d = { .octet = { 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, }, };

    size_t i = 0;
    for (i = 0; i < candidate_list->set_size; i++) {
        struct ow_steer_candidate *candidate = &candidate_list->set[i];

        if (osw_hwaddr_cmp(ow_steer_candidate_get_bssid(candidate), &bssid_a) == 0)
            ow_steer_candidate_set_metric(candidate, 0);
        else if (osw_hwaddr_cmp(ow_steer_candidate_get_bssid(candidate), &bssid_b) == 0)
            ow_steer_candidate_set_metric(candidate, 2);
        else if (osw_hwaddr_cmp(ow_steer_candidate_get_bssid(candidate), &bssid_c) == 0)
            ow_steer_candidate_set_metric(candidate, 1);
        else if (osw_hwaddr_cmp(ow_steer_candidate_get_bssid(candidate), &bssid_d) == 0)
            ow_steer_candidate_set_metric(candidate, 1);
        else
            assert(false); /* unreachable */
    }

    return true;
}

static struct ow_steer_candidate_assessor*
ow_steer_candidate_list_ut_create_sort_assessor(const struct osw_hwaddr *sta_addr)
{
    const struct ow_steer_candidate_assessor_ops ops = {
        .assess_fn = ow_steer_candidate_list_ut_create_sort_assess_cb,
        .free_priv_fn = NULL,
    };

    return ow_steer_candidate_assessor_create("ow_steer_candidate_list_ut_create_sort_assessor", sta_addr, &ops, NULL);
}

static void
ow_steer_candidate_list_ut_lifecycle_cb(void *data)
{
    struct ow_steer_candidate_list *candidates_a = NULL;
    struct ow_steer_candidate_list *candidates_b = NULL;
    struct ow_steer_candidate *candidate = NULL;
    const struct osw_hwaddr bssid_a = { .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, }, };
    const struct osw_hwaddr bssid_b = { .octet = { 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, }, };
    const struct osw_hwaddr bssid_c = { .octet = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, }, };
    const struct osw_hwaddr bssid_d = { .octet = { 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, }, };
    const enum ow_steer_candidate_preference pref_a = OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED;
    const enum ow_steer_candidate_preference pref_b = OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED;
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };

    candidates_a = ow_steer_candidate_list_new();

    /* Fill set with A, B and D */
    ow_steer_candidate_list_bss_set(candidates_a, &bssid_a, &channel);
    ow_steer_candidate_list_bss_set(candidates_a, &bssid_b, &channel);
    ow_steer_candidate_list_bss_set(candidates_a, &bssid_d, &channel);

    assert(ow_steer_candidate_list_lookup(candidates_a, &bssid_a) != NULL);
    assert(ow_steer_candidate_list_lookup(candidates_a, &bssid_b) != NULL);
    assert(ow_steer_candidate_list_lookup(candidates_a, &bssid_d) != NULL);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_a)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_b)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_d)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Remove D */
    ow_steer_candidate_list_bss_unset(candidates_a, &bssid_d);

    assert(ow_steer_candidate_list_lookup(candidates_a, &bssid_a) != NULL);
    assert(ow_steer_candidate_list_lookup(candidates_a, &bssid_b) != NULL);
    assert(ow_steer_candidate_list_lookup(candidates_a, &bssid_d) == NULL);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_a)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_b)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Add C */
    ow_steer_candidate_list_bss_set(candidates_a, &bssid_c, &channel);

    assert(ow_steer_candidate_list_lookup(candidates_a, &bssid_a) != NULL);
    assert(ow_steer_candidate_list_lookup(candidates_a, &bssid_b) != NULL);
    assert(ow_steer_candidate_list_lookup(candidates_a, &bssid_c) != NULL);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_a)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_b)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_c)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Set C again */
    ow_steer_candidate_list_bss_set(candidates_a, &bssid_c, &channel);

    assert(ow_steer_candidate_list_lookup(candidates_a, &bssid_a) != NULL);
    assert(ow_steer_candidate_list_lookup(candidates_a, &bssid_b) != NULL);
    assert(ow_steer_candidate_list_lookup(candidates_a, &bssid_c) != NULL);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_a)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_b)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_c)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Update preferences, one by one */
    candidate = ow_steer_candidate_list_lookup(candidates_a, &bssid_a);
    ow_steer_candidate_set_preference(candidate, pref_a);

    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_a)) == pref_a);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_b)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_c)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    candidate = ow_steer_candidate_list_lookup(candidates_a, &bssid_c);
    ow_steer_candidate_set_preference(candidate, pref_b);

    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_a)) == pref_a);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_b)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_c)) == pref_b);

    candidate = ow_steer_candidate_list_lookup(candidates_a, &bssid_b);
    ow_steer_candidate_set_preference(candidate, pref_b);

    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_a)) == pref_a);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_b)) == pref_b);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_c)) == pref_b);

    /* Make a copy */
    candidates_b = ow_steer_candidate_list_copy(candidates_a);
    assert(ow_steer_candidate_list_cmp(candidates_a, candidates_b) == true);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_a)) == pref_a);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_b)) == pref_b);
    assert(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidates_a, &bssid_c)) == pref_b);

    /* Modify source and copy */
    ow_steer_candidate_list_bss_set(candidates_b, &bssid_d, &channel);
    assert(ow_steer_candidate_list_cmp(candidates_a, candidates_b) == false);

    ow_steer_candidate_list_bss_set(candidates_a, &bssid_d, &channel);
    assert(ow_steer_candidate_list_cmp(candidates_a, candidates_b) == true);

    /* Fini */
    ow_steer_candidate_list_free(candidates_a);
    ow_steer_candidate_list_free(candidates_b);
}

static void
ow_steer_candidate_list_ut_metric_cb(void *data)
{
    struct ow_steer_candidate_list *candidates = NULL;
    const struct osw_hwaddr sta_addr = { .octet = { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBc, }, };
    const struct osw_hwaddr bssid_a = { .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, }, };
    const struct osw_hwaddr bssid_b = { .octet = { 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, }, };
    const struct osw_hwaddr bssid_c = { .octet = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, }, };
    const struct osw_hwaddr bssid_d = { .octet = { 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, }, };
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    struct ow_steer_candidate_assessor *assessor = ow_steer_candidate_list_ut_create_sort_assessor(&sta_addr);

    candidates = ow_steer_candidate_list_new();

    /* Fill set with A, B, C and D */
    ow_steer_candidate_list_bss_set(candidates, &bssid_a, &channel);
    ow_steer_candidate_list_bss_set(candidates, &bssid_b, &channel);
    ow_steer_candidate_list_bss_set(candidates, &bssid_c, &channel);
    ow_steer_candidate_list_bss_set(candidates, &bssid_d, &channel);

    assert(ow_steer_candidate_get_metric(ow_steer_candidate_list_lookup(candidates, &bssid_a)) == 0);
    assert(ow_steer_candidate_get_metric(ow_steer_candidate_list_lookup(candidates, &bssid_b)) == 0);
    assert(ow_steer_candidate_get_metric(ow_steer_candidate_list_lookup(candidates, &bssid_c)) == 0);
    assert(ow_steer_candidate_get_metric(ow_steer_candidate_list_lookup(candidates, &bssid_d)) == 0);

    /* Sort metrics, expected order (high->low) B, C, D, A */ /* TODO Handle other possible order: B, D, C, A */
    ow_steer_candidate_list_sort(candidates, assessor);

    assert(ow_steer_candidate_get_metric(ow_steer_candidate_list_lookup(candidates, &bssid_a)) == 0);
    assert(ow_steer_candidate_get_metric(ow_steer_candidate_list_lookup(candidates, &bssid_b)) == 2);
    assert(ow_steer_candidate_get_metric(ow_steer_candidate_list_lookup(candidates, &bssid_c)) == 1);
    assert(ow_steer_candidate_get_metric(ow_steer_candidate_list_lookup(candidates, &bssid_d)) == 1);

    assert(ow_steer_candidate_list_get(candidates, 0) == ow_steer_candidate_list_lookup(candidates, &bssid_b));
    assert(ow_steer_candidate_list_get(candidates, 1) == ow_steer_candidate_list_lookup(candidates, &bssid_c));
    assert(ow_steer_candidate_list_get(candidates, 2) == ow_steer_candidate_list_lookup(candidates, &bssid_d));
    assert(ow_steer_candidate_list_get(candidates, 3) == ow_steer_candidate_list_lookup(candidates, &bssid_a));

    /* Fini */
    ow_steer_candidate_list_free(candidates);
    ow_steer_candidate_assessor_free(assessor);
}

static void
ow_steer_candidate_list_ut_module_init(void *data)
{
    osw_ut_register("ow_steer_candidate_list_ut_lifecycle", ow_steer_candidate_list_ut_lifecycle_cb, NULL);
    osw_ut_register("ow_steer_candidate_list_ut_metric", ow_steer_candidate_list_ut_metric_cb, NULL);
}

static void
ow_steer_candidate_list_ut_module_fini(void *data)
{
    /* nop */
}

MODULE(ow_steer_candidate_list_ut_module,
       ow_steer_candidate_list_ut_module_init,
       ow_steer_candidate_list_ut_module_fini);
