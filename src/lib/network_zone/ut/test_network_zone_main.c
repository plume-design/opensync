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

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "network_zone.h"
#include "ovsdb.h"
#include "ovsdb_utils.h"
#include "log.h"
#include "memutil.h"
#include "target.h"
#include "unity.h"

#include "test_network_zone.h"
#include "network_zone_internals.h"
#include "unit_test_utils.h"

const char *test_name = "network_zone_tests";


static struct nz_test_mgr network_zone_ovsdb_test_mgr;

struct nz_test_mgr *
nz_get_test_mgr(void)
{
    return &network_zone_ovsdb_test_mgr;
};


static void
network_zone_ovsdb_test_setup(void)
{
    struct nz_test_mgr *mgr;

    mgr = nz_get_test_mgr();

    mgr->has_ovsdb = unit_test_check_ovs();
    if (mgr->has_ovsdb == false)
    {
        LOGI("%s: no ovsdb available", __func__);
        goto no_ovsdb;
    }

    /* Proceed to settings and connecting to the ovsdb server */
    mgr->loop = EV_DEFAULT;
    mgr->g_timeout = 1.0;

    if (!ovsdb_init_loop(mgr->loop, "UT_NETWORK_ZONE"))
    {
        LOGI("%s: failed to initialize ovsdb framework", __func__);

        goto no_ovsdb;
    }
    mgr->has_ovsdb = true;
    network_zone_ovsdb_monitor_tags();
    network_zone_init();

    return;

no_ovsdb:
    mgr->has_ovsdb = false;
    network_zone_init_manager();
}


static void
network_zone_test_global_init(void)
{
    network_zone_ovsdb_test_setup();
}


static void
network_zone_test_global_exit(void)
{
    struct nz_test_mgr *mgr;

    mgr = nz_get_test_mgr();

    nz_tests_clean_all_ovsdb_entries();

    if (!ovsdb_stop_loop(mgr->loop))
    {
        LOGE("%s: Failed to stop OVSDB", __func__);
    }

    ev_loop_destroy(mgr->loop);

    return;
}


static void
network_zone_setUp(void)
{
    return;
}


static void
network_zone_tearDown(void)
{
    return;
}


int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(test_name, network_zone_test_global_init, network_zone_test_global_exit);

    ut_setUp_tearDown(test_name, network_zone_setUp, network_zone_tearDown);

    run_network_zone_routines();
    run_network_zone_ovsdb();

    return ut_fini();
}
