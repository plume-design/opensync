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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdlib.h>

#include "log.h"
#include "os.h"
#include "memutil.h"
#include "lte_info.h"
#include "ltem_mgr.h"
#include "ltem_lte_ut.h"
#include "target.h"
#include "unity.h"

static ltem_mgr_t ltem_mgr;

const char *test_name = "ltem_tests";

void
setUp(void)
{
    return;
}

void
tearDown(void)
{
    return;
}

ltem_mgr_t *
ltem_get_mgr(void)
{
    return &ltem_mgr;
}

bool
ltem_ut_mgr_init(struct ev_loop *loop)
{
    lte_config_info_t *lte_config;
    lte_state_info_t *lte_state;
    lte_route_info_t *lte_route;

    ltem_mgr_t *mgr = ltem_get_mgr();

    mgr->loop = loop;

    lte_config = CALLOC(1, sizeof(lte_config_info_t));
    if (lte_config == NULL) return false;
    lte_state = CALLOC(1, sizeof(lte_state_info_t));
    if (lte_state == NULL) return false;
    lte_route = CALLOC(1, sizeof(lte_route_info_t));
    if (lte_route == NULL) return false;

    mgr->lte_config_info = lte_config;
    mgr->lte_state_info = lte_state;
    mgr->lte_route = lte_route;

    lte_set_mqtt_topic();

    return true;
}

int
ut_system(const char *cmd)
{
    return 0;
}

void
ltem_setup_ut_handlers(ltem_mgr_t *mgr)
{
    ltem_handlers_t *handlers;

    handlers = &mgr->handlers;

    handlers->ltem_mgr_init = ltem_ut_mgr_init;
    handlers->system_call = ut_system;
    handlers->lte_modem_open = lte_ut_modem_open;
    handlers->lte_modem_write = lte_ut_modem_write;
    handlers->lte_modem_read = lte_ut_modem_read;
    handlers->lte_modem_close = lte_ut_modem_close;
    handlers->lte_run_microcom_cmd = lte_ut_run_microcom_cmd;
}

void
test_ltem_build_mqtt_report(void)
{
    int ret;
    time_t now = time(NULL);

    ret = ltem_build_mqtt_report(now);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

int
main(int argc, char **argv)
{
    ltem_mgr_t *mgr;
    struct ev_loop *loop = EV_DEFAULT;
    bool rc;

    UnityBegin(test_name);

    ltem_set_lte_state(LTEM_LTE_STATE_INIT);

    mgr = ltem_get_mgr();
    memset(mgr, 0, sizeof(ltem_mgr_t));

    ltem_setup_ut_handlers(mgr);
    rc = mgr->handlers.ltem_mgr_init(loop);
    if (!rc) return -1;

    RUN_TEST(test_ltem_build_mqtt_report);

    FREE(mgr->lte_config_info);
    FREE(mgr->lte_state_info);
    FREE(mgr->lte_route);

    return UNITY_END();
}


