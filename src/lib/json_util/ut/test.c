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

#include <jansson.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <string.h>

#include "unity.h"
#include "json_util.h"
#include "util.h"


#define BUFF_SZ (4 * 4096)
#define DIRDATA "data/"

typedef enum
{
    JSON_REGULAR,
    JSON_SPECIAL_CHAR1,
    JSON_SPECIAL_CHAR2,
    JSON_SPECIAL_CHAR3,
    JSON_BROKEN
} test_file_t;

static char buff[BUFF_SZ];

/* invoked before each test execution   */
void setUp (void)
{
    /* on each test start load the json buffer  */
#if 0
    /* make this non-blocking stdin read  */
    if ( 0 > fcntl(STDIN_FILENO, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK))
    {
        printf("Error setting O_NONBLOCK\n");
        exit(-1);
    }

    while (0 < read(STDIN_FILENO, pr++, 1))
    {
        /* read doesn't add '\0' - so we can read only BUFF_SZ - 1 char */
        if (pr - buff >= BUFF_SZ - 1)
        {
            printf("Buffer overflow prevented, stop reading STDIN\n");
            break;
        }
    }
#endif
}

/* invoked after each test execution    */
void tearDown (void)
{
}


bool load_buff(test_file_t ft)
{
    int fd;
    char fpath[PATH_MAX];

    char *pr = buff;
    bool res = false;

    memset(fpath, 0 , PATH_MAX);

    /* copy string and get the pointer to next free char    */
    STRSCPY(fpath, DIRDATA);

    switch(ft)
    {
        case JSON_REGULAR:
            STRSCAT(fpath, "regular.json");
            break;
        case JSON_SPECIAL_CHAR1:
            STRSCAT(fpath, "regular_special_char1.json");
             break;
        case JSON_SPECIAL_CHAR2:
            STRSCAT(fpath, "regular_special_char2.json");
            break;
        case JSON_SPECIAL_CHAR3:
            STRSCAT(fpath, "regular_special_char3.json");
            break;
        case JSON_BROKEN:
            STRSCAT(fpath, "broken.json");
            break;
    }

    fd = open(fpath, O_RDONLY);
    if (fd < 0)
    {
        printf("Error reading the file, %s\n", fpath);
        goto end;
    }

    /* read file into the buffer    */
    while (0 < read(fd, pr++, 1))
    {
        /* read doesn't add '\0' - so we can read only BUFF_SZ - 1 char */
        if (pr - buff >= BUFF_SZ - 1)
        {
            printf("Buffer overflow, file too big \n");
            goto end;
        }
    }

    if (--pr > buff)
    {
        res = true;
    }
    else
    {
        printf("File seems to be empty\n");
    }

end:
    if (fd >= 0)
        close(fd);
    return res;
}


/*
 * test json_split library function
 */
void testjsplit_regularjson(void)
{
    /* TEST_ASSERT_TRUE_MESSAGE(load_buff(JSON_REGULAR), "Reading json string"); */
    STRSCPY(buff, "{ \"test\":\"plain string\"}");

    TEST_ASSERT_NOT_NULL(json_split(buff));
    TEST_ASSERT_NOT_EQUAL(json_split(buff), JSON_SPLIT_ERROR);
}


void testjsplit_specialcharjson1(void)
{
    STRSCPY(buff, "{ \"test\":\"special \bstring\"}");

    TEST_ASSERT_NOT_NULL(json_split(buff));
    TEST_ASSERT_NOT_EQUAL(json_split(buff), JSON_SPLIT_ERROR);
}

void testjsplit_specialcharjson2(void)
{
    STRSCPY(buff, "{ \"test\":\"special \\\\string\"}");

    TEST_ASSERT_NOT_NULL(json_split(buff));
    TEST_ASSERT_NOT_EQUAL(json_split(buff), JSON_SPLIT_ERROR);
}

void testjsplit_specialcharjson3(void)
{
    STRSCPY(buff, "{ \"test\":\"special \\\"string\"}");

    TEST_ASSERT_NOT_NULL(json_split(buff));
    TEST_ASSERT_NOT_EQUAL(json_split(buff), JSON_SPLIT_ERROR);
}

void testjgets_regularjson(void)
{
    size_t len;
    const char *gs;

    json_error_t err;
    json_t *js;

    js = json_loads("{ \"hello\": \"world\", \"integer\": 5, \"real\": 3.14}", 0, &err);
    TEST_ASSERT_NOT_NULL_MESSAGE(js, "Unable to parse test JSON: %s\n");

    printf("dump_static = %s\n", json_dumps_static(js, 0));

    /* Test with a short string */
    gs = json_dumps_static(js, 0);
    len = strlen(gs) - 1;

    TEST_ASSERT_FALSE_MESSAGE(json_gets(js, buff, len, 0), "JSON gets succeeded, but it should fail!?\n");
    TEST_ASSERT_TRUE_MESSAGE(json_gets(js, buff, sizeof(buff), 0), "JSON gets failed!?\n");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(strlen(gs), strlen(buff), "Size mismatch");

}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;


    UnityBegin("json_split ");

    RUN_TEST(testjsplit_regularjson);
    RUN_TEST(testjsplit_specialcharjson1);
    RUN_TEST(testjsplit_specialcharjson2);
    RUN_TEST(testjsplit_specialcharjson3);
    RUN_TEST(testjgets_regularjson);

    return UNITY_END();
}
