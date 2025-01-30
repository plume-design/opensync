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

#include <libgen.h>

#include "target.h"
#include "log.h"
#include "unity.h"

#include "test_gatekeeper_plugin.h"
#include "unit_test_utils.h"

char *test_name = "test_gatekeeper_plugin";
char *g_certs_file = "./data/cacert.pem";

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    bool ret;

    ut_init(test_name, NULL, NULL);
    ut_setUp_tearDown(test_name, NULL, NULL);

    /*
     * This is a requirement: Do NOT proceed if the file is missing.
     * File presence will not be tested any further.
     */
    if (chdir(dirname(argv[0])) != 0)
    {
        LOGW("chdir(\"%s\") failed", argv[0]);
        return ut_fini();
    }

    ret = access(g_certs_file, F_OK);
    if (ret != 0)
    {
        LOGW("In %s requires %s", test_name, g_certs_file);
        return ut_fini();
    }

    run_test_fsm_gk_fct();
    run_test_fsm_gk();

    return ut_fini();
}
