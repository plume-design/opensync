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
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>

#include "ovsdb.h"
#include "ovsdb_table.h"

#define MAX_PSK_LEN 64
#define MAX_PSK_CNT 1024
static char psks[MAX_PSK_CNT][MAX_PSK_LEN+1];
static int psk_count;
static ovsdb_table_t table_Wifi_VIF_Config;
static ovsdb_table_t table_Wifi_Credential_Config;
static ovsdb_table_t table_IPSec_Config;

static bool append_psks_if_unique(const char *newstr)
{
    int i;
    for (i=psk_count; i>=0; i--)
    {
        if (!strcmp(psks[i], newstr)) return 0;
    }
    assert(psk_count < MAX_PSK_CNT);
    assert(STRSCPY(psks[psk_count], newstr) > 0);
    psk_count++;
    return 1;
}

static int strlencmp(const void *str1, const void *str2)
{
    const size_t str1_len = strlen(str1);
    const size_t str2_len = strlen(str2);
    return -((str1_len > str2_len) - (str1_len < str2_len));
}

static bool is_psk_key(const char *key)
{
    const char *pass_key = "key";
    const char *pass_key_prefix = "key-";
    if (!strncmp(key, pass_key_prefix, strlen(pass_key_prefix)) || !strcmp(key, pass_key))
        return true;
    return false;
}

static void fetch_ovsdb_psks(void)
{
    int n = 0;
    struct schema_Wifi_VIF_Config *buf = ovsdb_table_select_where(&table_Wifi_VIF_Config, NULL, &n);
    if (n == 0) printf("No PSKs found in Wifi_VIF_Config\n"); 
    int record;
    for (record=0; record<n; record++)
    {
        int keynum;
        for (keynum=0; keynum < buf[record].wpa_psks_len; keynum++)
        {
            if(is_psk_key(buf[record].wpa_psks_keys[keynum]))
                append_psks_if_unique(buf[record].wpa_psks[keynum]);
        }
        for (keynum=0; keynum < buf[record].security_len; keynum++)
        {
            if(is_psk_key(buf[record].security_keys[keynum]))
                append_psks_if_unique(buf[record].security[keynum]);
        }
    }
    free(buf);

    n = 0;
    struct schema_Wifi_Credential_Config *buf2 = ovsdb_table_select_where(&table_Wifi_Credential_Config, NULL, &n);
    if (n == 0) printf("No PSKs found in Wifi_Credential_Config\n");
    for (record=0; record<n; record++)
    {
        int keynum;
        for (keynum=0; keynum < buf2[record].security_len; keynum++)
        {
            if(is_psk_key(buf2[record].security_keys[keynum]))
                append_psks_if_unique(buf2[record].security[keynum]);
        }
    }
    free(buf2);

    n = 0;
    struct schema_IPSec_Config *buf3 = ovsdb_table_select_where(&table_IPSec_Config, NULL, &n);
    if (n == 0) printf("No PSKs found in IPSec_Config\n");
    for (record=0; record<n; record++)
    {
        if (buf3[record].psk_exists)
        {
            append_psks_if_unique(buf3[record].psk);
        }
    }
    free(buf3);

    qsort(psks, psk_count, MAX_PSK_LEN+1, strlencmp);
}

static void print_usage(void)
{
    fprintf(stderr, 
         "Pskmask password masking utility\n"
         "This tool fetches PreShared Keys from OVSDB and\n"
         "masks every occurence inside of provided files.\n"
         "If file is a symlink, it gets replaced by masked\n"
         "copy of the file symlink was pointing to.\n"
         "Usage: pskmask [-h] [-o OBFUSCSTR] [--] <FILE>...\n"
         "Options:\n"
         "  -h                       Shows this help message\n"
         "  -o OBFUSCSTR             String that will replace passwords.\n"
         "                           If not provided  \"********\" will be used\n"
         "  [--] <FILE>...           Files to be masked. \"--\" is optional,\n"
         "                           but it's use is highly recommended due to\n"
         "                           possible dashes in filenames.\n"
         "Examples:\n"
         "  pskmask -o _MASKED_ -- passfile1 passfile2 passfile3\n"
         "  pskmask -- somefile\n"
         "Useful combinations:\n"
         "find somefolder -type f | xargs pskmask --\n"
         "find somefolder -type f | xargs pskmask -o _MASKED_ --\n"
         "\n");
}

int mask_file(const char *fpath, const char *obfstr)
{
    const char *tmp_extension = ".tmp";
    char *tmp_fpath = malloc(strlen(fpath)+strlen(tmp_extension)+1);
    strcpy(tmp_fpath, fpath);
    strcat(tmp_fpath, tmp_extension);

    printf("Masking file: %s ... ", fpath);

    int match_count = 0;
    int i;
    for (i=0; i<psk_count; i++)
    {
        size_t psk_len = strlen(psks[i]);

        FILE *curr_file;
        curr_file = fopen(fpath, "rb");
        assert(curr_file != NULL);

        struct stat curr_file_stat;
        assert(stat(fpath, &curr_file_stat) == 0);
        if (curr_file_stat.st_size == 0)
        {
            assert(!fclose(curr_file));
            break;
        }

        char *curr_file_map = mmap(NULL, curr_file_stat.st_size, PROT_READ, MAP_SHARED, fileno(curr_file), 0);
        assert(curr_file_map);

        FILE *tmp_file;
        tmp_file = fopen(tmp_fpath, "wb");
        assert(tmp_file != NULL);

        char *cursor = curr_file_map;
        char *psk_ptr;
        while ((psk_ptr = strstr(cursor, psks[i])) != NULL)
        {
            match_count++;
            fwrite(cursor, sizeof(char), psk_ptr-cursor, tmp_file);
            fputs(obfstr, tmp_file);
            cursor = psk_ptr + psk_len;
        }
        fwrite(cursor, sizeof(char), curr_file_map + curr_file_stat.st_size - cursor, tmp_file);

        assert(!munmap(curr_file_map, curr_file_stat.st_size));
        assert(!fclose(curr_file));
        assert(!fclose(tmp_file));
        assert(!remove(fpath));
        assert(!rename(tmp_fpath, fpath));
    }

    free(tmp_fpath);

    printf("Replaced entries: %d\n", match_count);

    return 0;
}


int main(int argc, char **argv)
{
    char *obfstr = "********";

    if(argc == 1)
    {
        print_usage();
        return EXIT_FAILURE;
    }

    signed char opt;
    while ((opt = getopt(argc, argv, "ho:")) != -1) 
    {
        switch (opt)
        {
            case 'o':
                obfstr = optarg;
                break;
            case 'h':
                print_usage();
                return EXIT_SUCCESS;
            default:
                print_usage();
                return EXIT_FAILURE;
        }
    }

    OVSDB_TABLE_INIT(Wifi_VIF_Config, if_name);
    OVSDB_TABLE_INIT(Wifi_Credential_Config, _uuid);
    OVSDB_TABLE_INIT(IPSec_Config, tunnel_name);

    /* Part 1 - gather psks */
    fetch_ovsdb_psks();

    /* Part 2 - wipe psks from files*/
    int i;
    for (i=optind; i<argc; i++)
    {
        if(argv[i][0] == 0) continue;

        struct stat sb;
        if (stat(argv[i], &sb) == 0)
        {
            if(S_ISDIR(sb.st_mode))
            {
                fprintf(stderr, "%s is a directory, skipping...\n", argv[i]);
                continue;
            }
            else
            {
                mask_file(argv[i], obfstr);
            }
        }
        else
        {
            fprintf(stderr, "File type check error. Check if file exists.\n");
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
