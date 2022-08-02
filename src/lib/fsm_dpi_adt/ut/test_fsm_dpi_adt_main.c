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

#include "dns_cache.h"
#include "log.h"
#include "target.h"
#include "unity.h"
#include "unity_internals.h"
#include "unit_test_utils.h"

char *test_name = "fsm_dpi_adt_plugin_tests";

/**
 * @brief called by the Unity framework before every single test
 */
void
fsm_dpi_adt_setUp(void)
{
    struct dns_cache_settings cache_init;

    cache_init.dns_cache_source = MODULE_DNS_PARSE;
    cache_init.service_provider = IP2ACTION_WP_SVC;
    dns_cache_init(&cache_init);
}

/**
 * @brief called by the Unity framework after every single test
 */
void
fsm_dpi_adt_tearDown(void)
{
    dns_cache_cleanup_mgr();
}


extern void run_test_adt(void);

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(test_name, NULL, NULL);

    ut_setUp_tearDown(test_name, fsm_dpi_adt_setUp, fsm_dpi_adt_tearDown);

    run_test_adt();

    return ut_fini();
}
