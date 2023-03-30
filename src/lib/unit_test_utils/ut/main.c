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


#include "net_header_parse.h"
#include "os.h"
#include "unit_test_utils.h"
#include "unity.h"

#include "pcap.c"

/*
 * Since this UT is testing a UT specific component, most of the
 * testing needs to be performed "manually".
 */
extern char g_parent_tmp_folder[PATH_MAX];
extern char g_pcap_tmp_folder[PATH_MAX];
extern bool g_keep_tmp_folder;
extern void (*g_tearDown)(void);
extern void (*g_setUp)(void);
extern char *g_test_name;
extern char *g_ut_name;

const char *ut_name = "unit_test_utils";

bool test_setUp_flag = false;

void
new_setUp(void)
{
    test_setUp_flag = true;
}

void
new_tearDown(void)
{
    test_setUp_flag = false;
}

void
test_default_init(void)
{
    struct stat s;

    TEST_ASSERT_NULL(g_setUp);
    TEST_ASSERT_NULL(g_tearDown);
    TEST_ASSERT_EQUAL_STRING(ut_name, g_ut_name);

    /* Verify the parent folder was created */
    TEST_ASSERT_NOT_EQUAL('\0', g_parent_tmp_folder[0]);
    stat(g_parent_tmp_folder, &s);
    /* Is this a folder */
    TEST_ASSERT_TRUE(S_ISDIR(s.st_mode));
    /* Do we own it */
    TEST_ASSERT_EQUAL(getuid(), s.st_uid);
    /* Are access rights correct */
    TEST_ASSERT_EQUAL(S_IRWXU, s.st_mode & 00777);
}

void
test_no_setup(void)
{
    TEST_ASSERT_NULL(g_setUp);
    TEST_ASSERT_NULL(g_tearDown);
    TEST_ASSERT_FALSE(test_setUp_flag);
}

void
test_with_setup(void)
{
    TEST_ASSERT_EQUAL_PTR(new_setUp, g_setUp);
    TEST_ASSERT_EQUAL_PTR(new_tearDown, g_tearDown);
    TEST_ASSERT_TRUE(test_setUp_flag);
}

void
test_create_pcap(void)
{
    struct net_header_parser parser;
    char filename[PATH_MAX+32];
    char *temp_pcap_folder;
    int ret;

    ut_prepare_pcap(__func__);
    UT_CREATE_PCAP_PAYLOAD(pkt46, &parser);

    /* The filename needs to line up with the array name in 'pcap.c' */
    temp_pcap_folder = STRDUP(g_pcap_tmp_folder);
    snprintf(filename, sizeof(filename), "%s/%s.txtpcap", g_pcap_tmp_folder, "pkt46");

    /* First make sure we keep the folder */
    ut_keep_temp_folder(true);
    ut_cleanup_pcap();
    /* Make sure file is still present */
    ret = access(filename, F_OK);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, ret, "File does not exist");

    /* Now actually delete the file and folder */
    ut_keep_temp_folder(false);
    ut_cleanup_pcap();
    /* Make sure file is gone */
    ret = access(filename, F_OK);
    TEST_ASSERT_EQUAL_INT(-1, ret);
    /* Make sure folder is gone */
    ret = access(temp_pcap_folder, F_OK);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    /* Check for crashes (nothing to test, just making sure it doesn't bomb) */
    ut_cleanup_pcap();

    /* Final cleanup */
    FREE(temp_pcap_folder);
}

int
main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    ut_init(ut_name, NULL, NULL);
    RUN_TEST(test_default_init);

    RUN_TEST(test_no_setup);

    ut_setUp_tearDown("with_setup", new_setUp, new_tearDown);
    RUN_TEST(test_with_setup);
    ut_setUp_tearDown(NULL, NULL, NULL);
    RUN_TEST(test_no_setup);

    RUN_TEST(test_create_pcap);

    return ut_fini();
}
