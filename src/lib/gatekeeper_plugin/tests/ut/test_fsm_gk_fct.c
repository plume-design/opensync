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

#include <stdbool.h>

#include "gatekeeper.h"
#include "gatekeeper_cache.h"
#include "fsm_policy.h"
#include "unity.h"
#include "memutil.h"

void
test_gatekeeper_get_mgr(void)
{
    struct fsm_gk_mgr *mgr;
    struct fsm_gk_mgr *mgr2;

    mgr = gatekeeper_get_mgr();
    TEST_ASSERT_FALSE(mgr->initialized);

    mgr2 = gatekeeper_get_mgr();
    TEST_ASSERT_EQUAL_PTR(mgr, mgr2);
    TEST_ASSERT_FALSE(mgr2->initialized);
}

void
test_gatekeeper_init(void)
{
    struct fsm_gk_mgr *mgr;

    TEST_ASSERT_TRUE(gatekeeper_init(NULL));

    mgr = gatekeeper_get_mgr();
    TEST_ASSERT_TRUE(mgr->initialized);
    TEST_ASSERT_EQUAL_UINT32(0, mgr->req_ids.req_fqdn_id);
    TEST_ASSERT_EQUAL_UINT32(0, mgr->req_ids.req_ipv4_id);
    TEST_ASSERT_EQUAL_UINT32(0, mgr->req_ids.req_ipv6_id);
    TEST_ASSERT_EQUAL_UINT32(0, mgr->req_ids.req_ipv4_tuple_id);
    TEST_ASSERT_EQUAL_UINT32(0, mgr->req_ids.req_ipv6_tuple_id);
    TEST_ASSERT_EQUAL_UINT32(0, mgr->req_ids.req_app_id);
    TEST_ASSERT_EQUAL_UINT32(0, mgr->req_ids.req_https_sni_id);
    TEST_ASSERT_EQUAL_UINT32(0, mgr->req_ids.req_http_host_id);
    TEST_ASSERT_EQUAL_UINT32(0, mgr->req_ids.req_http_url_id);

    TEST_ASSERT_TRUE(gatekeeper_init(NULL));

    mgr->initialized = false;
}

void
gk_populate_redirect_entry(struct gk_attr_cache_interface *entry,
                           struct fsm_policy_req *req);

void 
test_gk_populate_redirect_entry(void)
{
    struct gk_attr_cache_interface entry;
    struct fsm_policy_req req;

    /* Set up the req struct. */
    memset(&entry, 0, sizeof(entry));

    req.fqdn_req = CALLOC(1, sizeof(*req.fqdn_req));
    strcpy(req.fqdn_req->redirects[0], "1.2.3.4");
    strcpy(req.fqdn_req->redirects[1], "6.7.8.9");
    req.reply.rd_ttl = 123;

    /* No redirect. */
    req.reply.redirect = false;
    gk_populate_redirect_entry(&entry, &req);
    TEST_ASSERT_NULL(entry.fqdn_redirect);
    
    /* Redirect. */
    req.reply.redirect = true;
    gk_populate_redirect_entry(&entry, &req);
    TEST_ASSERT_NOT_NULL(entry.fqdn_redirect);
    TEST_ASSERT_TRUE(entry.fqdn_redirect->redirect);
    TEST_ASSERT_EQUAL_INT(entry.fqdn_redirect->redirect_ttl, 123);
    TEST_ASSERT_EQUAL_STRING("1.2.3.4", entry.fqdn_redirect->redirect_ips[0]);
    TEST_ASSERT_EQUAL_STRING("6.7.8.9", entry.fqdn_redirect->redirect_ips[1]);

    FREE(entry.fqdn_redirect);

    /* Cleanup locally allocated stuff */
    FREE(req.fqdn_req);
}

void
test_fsm_gk_fct(void)
{
    RUN_TEST(test_gatekeeper_get_mgr);
    RUN_TEST(test_gatekeeper_init);
    RUN_TEST(test_gk_populate_redirect_entry);
}
