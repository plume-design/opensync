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

#define _GNU_SOURCE

/* std libc */
#include <assert.h>
#include <stdlib.h>

/* internal */
#include <log.h>
#include <ovsdb.h>
#include <target.h>
#include <unity.h>

/* unit */
#include <wm2.h>

static int test_i;

static struct params {
    int cchan;
    const char *cmode;
    int schan;
    const char *smode;
    int allowed[8];
    int nol[8];
    int rchan;
    const char *rmode;
} params[] = {
    { 100, "HT80", 104, "HT20", { 100, 104, 108, 112 }, {}, 100, "HT80" },
    { 100, "HT80", 104, "HT20", { 100, 104, 112 }, { 108 }, 100, "HT40" },
    { 100, "HT80", 104, "HT20", { 100, 104, 108 }, { 112 }, 100, "HT40" },
    { 100, "HT80", 104, "HT20", { 100, 108, 112 }, { 104 }, 100, "HT20" },
    { 100, "HT80", 120, "HT80", { 104, 108, 112, 116, 120, 124, 128, 132 }, { 100 }, 120, "HT80" },
    { 100, "HT80", 120, "HT40", { 104, 108, 112, 116, 120, 124, 128, 132 }, { 100 }, 120, "HT40" },
    { 100, "HT80", 120, "HT20", { 104, 108, 112, 116, 120, 124, 128, 132 }, { 100 }, 120, "HT20" },
    { 36, "HT160", 100, "HT20", { 36, 40, 44, 48, 56, 60, 64 }, { 52 }, 36, "HT80" },
    { 36, "HT160", 100, "HT20", { 36, 40, 44, 48, 52, 60, 64 }, { 56 }, 36, "HT80" },
    { 36, "HT160", 100, "HT20", { 36, 40, 44, 48, 52, 56, 64 }, { 60 }, 36, "HT80" },
    { 36, "HT160", 100, "HT20", { 36, 40, 44, 48, 52, 56, 60 }, { 64 }, 36, "HT80" },
    { 36, "HT160", 100, "HT20", { 36, 40, 44, 52, 56, 60, 64 }, { 48 }, 36, "HT40" },
    { 36, "HT160", 100, "HT20", { 36, 40, 48, 52, 56, 60, 64 }, { 44 }, 36, "HT40" },
    { 36, "HT160", 100, "HT20", { 36, 44, 48, 52, 56, 60, 64 }, { 40 }, 36, "HT20" },
    { 36, "HT160", 100, "HT20", { 40, 44, 48, 52, 56, 60, 64, 100 }, { 36 }, 100, "HT20" },
    { 36, "HT160", 100, "HT20", { 40, 44, 48, 52, 56, 60, 64 }, { 36, 100 }, 100, "HT20" },
    { 108, "HT80", 140, "HT80", { 132, 136, 140 }, { 108 }, 140, "HT20" },
    { 108, "HT80", 132, "HT80", { 132, 136, 140 }, { 108 }, 132, "HT40" },
    { 108, "HT80", 136, "HT80", { 132, 136, 140 }, { 108 }, 136, "HT40" },
    { 108, "HT80", 132, "HT20", { 132, 140 }, { 108 }, 132, "HT20" },
};

void
setUp(void)
{
}

void
tearDown(void)
{
}

static void
test_one(const struct params *p)
{
    struct schema_Wifi_Radio_Config rconf = {0};
    struct schema_Wifi_Radio_State rstate = {0};
    size_t i;

    SCHEMA_SET_INT(rconf.channel, p->cchan);
    SCHEMA_SET_STR(rconf.ht_mode, p->cmode);
    SCHEMA_SET_INT(rstate.channel, p->schan);
    SCHEMA_SET_STR(rstate.ht_mode, p->smode);

    for (i = 0; i < ARRAY_SIZE(p->allowed); i++) {
        if (p->allowed[i]) {
            SCHEMA_KEY_VAL_APPEND(rstate.channels,
                                  strfmta("%d", p->allowed[i]),
                                  "nop_finished");
        }
    }

    for (i = 0; i < ARRAY_SIZE(p->nol); i++) {
        if (p->nol[i]) {
            SCHEMA_KEY_VAL_APPEND(rstate.channels,
                                  strfmta("%d", p->nol[i]),
                                  "nop_started");
        }
    }

    wm2_rconf_recalc_fixup_nop_channel(&rconf, &rstate);
    assert(rconf.channel == p->rchan);
    assert(strcmp(rconf.ht_mode, p->rmode) == 0);
}

static void
test_one_unity(void)
{
    test_one(&params[test_i]);
}

int
main(int argc, const char **argv)
{
    size_t i;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin("dfs nol test");
    for (i = 0; i < ARRAY_SIZE(params); i++) {
        test_i = i;
        RUN_TEST(test_one_unity);
    }

    return UNITY_END();
}
