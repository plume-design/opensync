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

#include "osw_ut.h"

OSW_UT(osw_token_ut_gen_seq)
{
    const struct osw_hwaddr sta_addr = { .octet = { 0xba, 0xdc, 0x0d, 0xeb, 0xad, 0xc0 } };
    const struct osw_ifname vif_name = { .buf = "home-ap-123" };
    struct osw_token_pool_reference *pool_ref = osw_token_pool_ref_get(&vif_name,
                                                                       &sta_addr);
    OSW_UT_EVAL(pool_ref != NULL);

    int token = osw_token_pool_fetch_token(pool_ref);
    OSW_UT_EVAL(token == 255);
    token = osw_token_pool_fetch_token(pool_ref);
    OSW_UT_EVAL(token == 254);
    token = osw_token_pool_fetch_token(pool_ref);
    OSW_UT_EVAL(token == 253);
}

OSW_UT(osw_token_ut_gen_free_seq)
{
    const struct osw_hwaddr sta_addr = { .octet = { 0xba, 0xba, 0xba, 0xbe, 0xbe, 0xbe } };
    const struct osw_ifname vif_name = { .buf = "some-ap" };
    struct osw_token_pool_reference *pool_ref = osw_token_pool_ref_get(&vif_name,
                                                                       &sta_addr);
    OSW_UT_EVAL(pool_ref != NULL);

    int token = osw_token_pool_fetch_token(pool_ref);
    OSW_UT_EVAL(token == 255);
    token = osw_token_pool_fetch_token(pool_ref);
    OSW_UT_EVAL(token == 254);
    osw_token_pool_free_token(pool_ref,
                              254);
    token = osw_token_pool_fetch_token(pool_ref);
    OSW_UT_EVAL(token == 253);
    token = osw_token_pool_fetch_token(pool_ref);
    OSW_UT_EVAL(token == 252);
}

OSW_UT(osw_token_ut_gen_wraparound_err_free)
{
    const struct osw_hwaddr sta_addr = { .octet = { 0xd0, 0xd0, 0xd0, 0xd0, 0xd0, 0x00 } };
    const struct osw_ifname vif_name = { .buf = "another-ap" };
    struct osw_token_pool_reference *pool_ref = osw_token_pool_ref_get(&vif_name,
                                                                       &sta_addr);
    OSW_UT_EVAL(pool_ref != NULL);

    int i;
    for(i = OSW_TOKEN_MAX; i>= OSW_TOKEN_MIN; i--) {
        int token = osw_token_pool_fetch_token(pool_ref);
        OSW_UT_EVAL(token == i);
    }

    /* token pool exhausted */
    int token = osw_token_pool_fetch_token(pool_ref);
    OSW_UT_EVAL(token == -1);

    osw_token_pool_free_token(pool_ref,
                              192);
    token = osw_token_pool_fetch_token(pool_ref);
    OSW_UT_EVAL(token == 192);
}

OSW_UT(osw_token_ut_gen_wraparound_free)
{
    const struct osw_hwaddr sta_addr = { .octet = { 0x13, 0x13, 0x13, 0x13, 0x13, 0x13 } };
    const struct osw_ifname vif_name = { .buf = "home-ap-4444" };
    struct osw_token_pool_reference *pool_ref = osw_token_pool_ref_get(&vif_name,
                                                                       &sta_addr);
    OSW_UT_EVAL(pool_ref != NULL);

    int i;
    for(i = OSW_TOKEN_MAX; i >= OSW_TOKEN_MIN; i--) {
        int token = osw_token_pool_fetch_token(pool_ref);
        OSW_UT_EVAL(token == i);
    }

    osw_token_pool_free_token(pool_ref,
                              250);
    int token = osw_token_pool_fetch_token(pool_ref);
    OSW_UT_EVAL(token == 250);

    /* token pool exhausted */
    token = osw_token_pool_fetch_token(pool_ref);
    OSW_UT_EVAL(token == -1);
}

OSW_UT(osw_token_ut_ref_drop)
{
    const struct osw_hwaddr sta_addr = { .octet = { 0x13, 0x13, 0x13, 0x13, 0x13, 0x13 } };
    const struct osw_ifname vif_name = { .buf = "home-ap-4444" };
    struct osw_token_pool_reference *ref1 = osw_token_pool_ref_get(&vif_name, &sta_addr);
    struct osw_token_pool_reference *ref2 = osw_token_pool_ref_get(&vif_name, &sta_addr);

    OSW_UT_EVAL(ref1 != NULL);
    OSW_UT_EVAL(ref2 != NULL);
    OSW_UT_EVAL(ref1->pool == ref2->pool);

    OSW_UT_EVAL(OSW_TOKEN_COUNT_ONE_BITS(ref2->pool->tokens) == 0);
    const int token = osw_token_pool_fetch_token(ref1);
    OSW_UT_EVAL(OSW_TOKEN_COUNT_ONE_BITS(ref2->pool->tokens) == 1);
    OSW_UT_EVAL(token == 255);
    osw_token_pool_ref_free(ref1);

    /* This frees the pool reference without freeing the
     * token that granted through the reference. This checks
     * if the token is implicitly freed.
     */

    OSW_UT_EVAL(OSW_TOKEN_COUNT_ONE_BITS(ref2->pool->tokens) == 0);
}
