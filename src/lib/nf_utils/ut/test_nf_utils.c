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

#include <errno.h>
#include <ev.h>

#include "log.h"
#include "nf_utils.h"
#include "nf_queue_internals.h"
#include "os.h"
#include "unit_test_utils.h"
#include "unity.h"

const char *ut_name = "nf_utils_tests";

static void
nf_utils_tests_init(void)
{
    bool ret;

    ret = nf_queue_init();
    if (!ret) LOGE("%s: nf_queue_init failed", __func__);
}


static void
nf_utils_setUp(void)
{
    return;
}

static void
nf_utils_tearDown(void)
{
    return;
}

static void
test_get_errs(void)
{
    struct nf_queue_context_errors *report;
    struct nfq_settings nfq_set;
    struct nfqueue_ctxt *nfq;

    MEMZERO(nfq_set);
    nfq_set.loop = EV_DEFAULT;
    nfq_set.queue_num = 10;

    nf_queue_open(&nfq_set);

    nfq = nfq_get_nfq_by_qnum(nfq_set.queue_num);
    TEST_ASSERT_NOT_NULL(nfq);

    nf_record_err(nfq, ENOMEM, "foo", 4);
    nf_record_err(nfq, ENOMEM, "foo2", 4);
    nf_record_err(nfq, ERANGE, "bar", 8);

    report = nfq_get_err_counters(nfq_set.queue_num);
    TEST_ASSERT_NOT_NULL(report);
    TEST_ASSERT_EQUAL_INT(2, report->count);
    TEST_ASSERT_EQUAL_UINT64(2, report->counters[0]->counter);
    TEST_ASSERT_EQUAL_UINT64(1, report->counters[1]->counter);

    nfq_log_err_counters(nfq_set.queue_num);

    FREE(report->counters);
    FREE(report);
}


int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(ut_name, nf_utils_tests_init, nf_queue_exit);
    ut_setUp_tearDown(ut_name, nf_utils_setUp, nf_utils_tearDown);

    RUN_TEST(test_get_errs);

    return ut_fini();
}
