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

#include <dirent.h>
#include <ev.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "const.h"
#include "reboot_flags.h"
#include "target.h"
#include "unity.h"

void setUp(void)
{
    target_log_open("no_reboot", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    LOGI("Creating /tmp/.no_reboot");
    cmd_log_check_safe("mkdir -p /tmp/.no_reboot");
}

void tearDown(void)
{
    LOGI("Removing /tmp/.no_reboot");
    cmd_log_check_safe("rm -rf /tmp/.no_reboot");
}

static bool file_exists(char *filename)
{
    if (access(filename, F_OK) == 0) return true;
    return false;
}

static bool directory_is_empty(char *path)
{
    DIR *dir = opendir(path);
    if (dir == NULL) return false;

    struct dirent *entry;
    size_t file_counter = 0;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_REG) file_counter++;
    }
    bool retval;
    if (file_counter == 0)
        retval = true;
    else
        retval = false;
    closedir(dir);
    return retval;
}

/* Creates two different files, then deletes one of them */
void test_set_two_clear_one()
{
    no_reboot_set("module_ut1");
    no_reboot_set("module_ut2");
    no_reboot_clear("module_ut1");
    TEST_ASSERT_TRUE(file_exists("/tmp/.no_reboot/module_ut2"));
    TEST_ASSERT_TRUE(!file_exists("/tmp/.no_reboot/module_ut1"));
    TEST_ASSERT_TRUE(!directory_is_empty("/tmp/.no_reboot"));
}

/* Creates two different files, then deletes all */
void test_set_two_clear_all()
{
    no_reboot_set("module_ut1");
    no_reboot_set("module_ut2");
    no_reboot_clear_all();
    TEST_ASSERT_TRUE(!file_exists("/tmp/.no_reboot/module_ut2"));
    TEST_ASSERT_TRUE(!file_exists("/tmp/.no_reboot/module_ut1"));
    TEST_ASSERT_TRUE(directory_is_empty("/tmp/.no_reboot"));
}

/* no_reboot_set with the same argument twice
   The file gets created with the first call and is renewed at
   the second (ls would show new timestamp).
   To delete the file, no_reboot_clear only once is enough */
void test_set_twice_clear_once()
{
    no_reboot_set("module_ut1");
    TEST_ASSERT_TRUE(file_exists("/tmp/.no_reboot/module_ut1"));
    no_reboot_set("module_ut1");
    TEST_ASSERT_TRUE(file_exists("/tmp/.no_reboot/module_ut1"));
    no_reboot_clear("module_ut1");
    TEST_ASSERT_TRUE(!file_exists("/tmp/.no_reboot/module_ut1"));
    TEST_ASSERT_TRUE(directory_is_empty("/tmp/.no_reboot"));
}

/* no_reboot_clear with the same argument twice
   The file gets deleted with the first call
   It makes no harm trying to delete again what has already been deleted */
void test_set_once_clear_twice()
{
    no_reboot_set("module_ut1");
    TEST_ASSERT_TRUE(file_exists("/tmp/.no_reboot/module_ut1"));
    no_reboot_clear("module_ut1");
    TEST_ASSERT_TRUE(!file_exists("/tmp/.no_reboot/module_ut1"));
    no_reboot_clear("module_ut1");
    TEST_ASSERT_TRUE(!file_exists("/tmp/.no_reboot/module_ut1"));
    TEST_ASSERT_TRUE(directory_is_empty("/tmp/.no_reboot"));
}

/* no_reboot_dump_modules when empty */
void test_dump_modules_empty()
{
    TEST_ASSERT_TRUE(no_reboot_dump_modules());
}

/* no_reboot_dump_modules after /tmp/.no_reboot has been deleted */
void test_dump_modules_non_existing_dir()
{
    cmd_log_check_safe("rm -rf /tmp/.no_reboot");
    TEST_ASSERT_FALSE(no_reboot_dump_modules());
}

int main(int argc, char *argv[])
{
    UnityBegin("test_no_reboot");
    RUN_TEST(test_set_two_clear_one);
    RUN_TEST(test_set_two_clear_all);
    RUN_TEST(test_set_twice_clear_once);
    RUN_TEST(test_set_once_clear_twice);
    RUN_TEST(test_dump_modules_empty);
    RUN_TEST(test_dump_modules_non_existing_dir);
    return UNITY_END();
}
