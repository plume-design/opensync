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

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "log.h"
#include "memutil.h"
#include "net_header_parse.h"
#include "os.h"
#include "target.h"
#include "unity.h"

char g_parent_tmp_folder[PATH_MAX] = { 0 };
char g_pcap_tmp_folder[PATH_MAX+128] = { 0 };
bool g_keep_tmp_folder = false;
void (*g_tearDown)(void) = NULL;
void (*g_setUp)(void) = NULL;
void (*g_ut_exit)(void) = NULL;
char *g_test_name = NULL;
char *g_ut_name = NULL;

/* A bunch of local functions to help with the exposed API */

/*
 * @brief recursively creates folders
 * This emulates `mkdir -p`
 */
static int
mkdir_rec(char *path)
{
    char *p = NULL;
    size_t len;

    len = strlen(path);
    if (path[len - 1] == '/')
    {
        path[len - 1] = 0;
    }
    for (p = path + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = 0;
            mkdir(path, S_IRWXU);
            *p = '/';
        }
    }
    mkdir(path, S_IRWXU);

    return 0;
}

/*
 * @brief recursively deletes folders
 * This emulates `rm -rf`
 */
static void
rmtree_rec(const char *path)
{
    struct stat stat_entry;
    struct stat stat_path;
    struct dirent *entry;
    size_t full_path_len;
    char *full_path;
    size_t path_len;
    DIR *dir;
    int rc;

    stat(path, &stat_path);

    if (!S_ISDIR(stat_path.st_mode))
    {
        LOGD("%s: Is not directory %s", __func__, path);
        return;
    }

    dir = opendir(path);
    if (dir == NULL)
    {
        LOGD("%s: Can't open directory: %s", __func__, path);
        return;
    }

    // the length of the path
    path_len = strlen(path);

    /* Go through all the entries in the directory */
    while ((entry = readdir(dir)) != NULL)
    {
        /* skip entries "." and ".." */
        if (!strcmp(entry->d_name, "."))
            continue;
        if (!strcmp(entry->d_name, ".."))
            continue;

        /* Create full path of this entry */
        full_path_len = path_len + 1 + strlen(entry->d_name) + 1;
        full_path = CALLOC(full_path_len, sizeof(char));
        snprintf(full_path, full_path_len, "%s/%s", path, entry->d_name);

        /* stat for the entry */
        stat(full_path, &stat_entry);

        /* recursively remove any nested directory */
        if (S_ISDIR(stat_entry.st_mode) != 0)
        {
            rmtree_rec(full_path);
            continue;
        }

        /* Remove the file */
        rc = unlink(full_path);
        if (rc != 0)
        {
            LOGD("%s: cannot delete file %s (%s)", __func__,
                 full_path, strerror(errno));
        }

        FREE(full_path);
    }

    /* Finally remove the directory */
    rc = rmdir(path);
    if (rc != 0)
    {
        LOGD("%s: cannot delete folder %s (%s)", __func__,
             path, strerror(errno));
    }

    closedir(dir);
}

/*
 * @brief Creates a "mostly unique" folder that will be used by the
 *        unit test to store temporary files.
 */
static bool
create_parent_tmp_folder(void)
{
    char *login;
    uid_t uid;
    int ret;

    if (*g_parent_tmp_folder != '\0') return true;

    /* create the folder using the user id executing the process */
    login = getlogin();
    if (login == NULL)
    {
        uid = getuid();
        snprintf(g_parent_tmp_folder, sizeof(g_parent_tmp_folder), "/tmp/USER_%u/%s", uid, g_ut_name);
    }
    else
    {
        snprintf(g_parent_tmp_folder, sizeof(g_parent_tmp_folder), "/tmp/%s/%s", login, g_ut_name);
    }

    ret = mkdir_rec(g_parent_tmp_folder);
    if (ret != 0)
    {
        MEMZERO(g_parent_tmp_folder);
        return false;
    }

    return true;
}

/**
 * @brief Converts a bytes array in a hex dump file wireshark can import.
 *
 * Dumps the array in a file that can then be imported by wireshark.
 * The file can also be translated to a pcap file using the text2pcap command.
 * Useful to visualize the packet content.
 */
static void
create_hex_dump(const char *fname, const uint8_t *buf, size_t len)
{
    int line_number = 0;
    bool new_line = true;
    size_t i;
    FILE *f;

    f = fopen(fname, "w+");

    if (f == NULL) return;

    for (i = 0; i < len; i++)
    {
        new_line = (i == 0 ? true : ((i % 8) == 0));
        if (new_line)
        {
            if (line_number) fprintf(f, "\n");
            fprintf(f, "%06x", line_number);
            line_number += 8;
        }
        fprintf(f, " %02x", buf[i]);
    }
    fprintf(f, "\n");
    fclose(f);

    LOGD("%s: Created %s", __func__, fname);
}

/**
 * @brief Dumps a packet in memory to a file.
 *
 * Dumps the packet content in /<TEMP_FOLDER>/<pkt name>.txtpcap
 * for wireshark consumption and sets g_parser data fields.
 * @params pkt the C structure containing an exported packet capture
 */
void
ut_create_pcap_payload(const char *pkt_name, const uint8_t pkt[], size_t len, struct net_header_parser *parser)
{
    char fname[PATH_MAX+256];

    if (*g_pcap_tmp_folder == '\0') return;

    snprintf(fname, sizeof(fname),
             "%s/%s.txtpcap", g_pcap_tmp_folder, pkt_name);
    create_hex_dump(fname, pkt, len);
    parser->packet_len = len;
    parser->caplen = len;
    parser->data = (uint8_t *)pkt;
}

void
ut_prepare_pcap(char *test_name)
{
    bool rc;

    if (*g_parent_tmp_folder == '\0')
    {
        rc = create_parent_tmp_folder();
        if (!rc) return;
    }

    snprintf(g_pcap_tmp_folder, sizeof(g_pcap_tmp_folder), "%s/%s", g_parent_tmp_folder, test_name);
    mkdir(g_pcap_tmp_folder, S_IRWXU);
}

void
ut_cleanup_pcap(void)
{
    if (*g_pcap_tmp_folder == '\0') return;

    /* This performs 'rm -rf' on the whole folder. */
    if (!g_keep_tmp_folder)
    {
        rmtree_rec(g_pcap_tmp_folder);
        MEMZERO(g_pcap_tmp_folder);
    }

}

/*
 * Global setUp and tearDown
 *   No need to implement these any longer if they are empty !
 */
void
setUp(void)
{
    if (g_setUp) g_setUp();
}

void
tearDown(void)
{
    if (g_tearDown) g_tearDown();
}

void
ut_keep_temp_folder(bool f)
{
    g_keep_tmp_folder = f;
}

void
ut_setUp_tearDown(const char *test_name, void(*setup)(void), void(*teardown)(void))
{
    g_setUp = setup;
    g_tearDown = teardown;
    FREE(g_test_name);
    g_test_name = NULL;
    if (test_name) g_test_name = STRDUP(test_name);
}

int
ut_fini(void)
{
    if (g_ut_name == NULL) return UNITY_END();

    if (g_ut_exit)
    {
        g_ut_exit();
        g_ut_exit = NULL;
    }

    /* Perform cleanup */
    FREE(g_ut_name);
    g_ut_name = NULL;

    /* Clean the temp folder */
    if (!g_keep_tmp_folder && *g_parent_tmp_folder != '\0')
    {
        /* This performs 'rm -rf' on the whole folder. */
        rmtree_rec(g_parent_tmp_folder);
        MEMZERO(g_parent_tmp_folder);
    }

    return UNITY_END();
}

void
ut_init(const char *ut_name, void (*global_ut_init)(void), void (*global_ut_exit)(void))
{
    if (ut_name)
    {
        g_ut_name = STRDUP(ut_name);
    }
    else
    {
        g_ut_name = STRDUP("MISSING_TEST_NAME");
    }

    if (global_ut_init) global_ut_init();
    if (global_ut_exit) g_ut_exit = global_ut_exit;

    create_parent_tmp_folder();

    /* Set the logs to stdout */
    target_log_open(g_ut_name, LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(g_ut_name);
}


#define TEST_BUF_LEN 1024

bool
unit_test_check_ovs(void)
{
    char buf[TEST_BUF_LEN];
    size_t len;
    int rc;

    /* Start testing the presence of ovsh */
    memset(buf, 0, sizeof(buf));
    rc = cmd_buf("which ovsh", buf, (size_t)TEST_BUF_LEN);
    len = strlen(buf);
    if (len == 0)
    {
        LOGI("%s: No ovsh found", __func__);
        return false;
    }
    LOGI("%s: ovsh found at %s", __func__, buf);

    /* Check the output of ovsh */
    rc = system("ovsh i Node_Config key:=foo module:=foo value:=foo");
    if (rc != 0)
    {
        LOGI("%s: ovsh command failed", __func__);
        return false;
    }
    LOGI("%s: ovsh command succeeded", __func__);

    /* Clean up */
    rc = system("ovsh d Node_Config -w key==foo");
    return true;
}

