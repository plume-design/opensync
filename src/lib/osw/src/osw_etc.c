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

#include <const.h>
#include <errno.h>
#include <limits.h>
#include <log.h>
#include <memutil.h>
#include <osw_module.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <util.h>

#include <kv_parser.h>
#include <osw_etc.h>

#define LOG_PREFIX(fmt, ...) "osw: etc: " fmt, ##__VA_ARGS__

struct osw_etc_data
{
    kv_parser_t *kv_parser;
};

static void osw_etc_init(osw_etc_data_t *m)
{
    m->kv_parser = kv_parser_new();
}

static void osw_etc_walk_cb(void *priv, const char *key, const char *value)
{
    LOGI(LOG_PREFIX("loaded: %s='%s'", key, value));
}

static void osw_etc_attach(osw_etc_data_t *m)
{
    kv_parser_set_dir(m->kv_parser, getenv("OSW_ETC_CONFIG_DIR") ?: OSW_ETC_CONFIG_DIR);
    kv_parser_populate(m->kv_parser);
    kv_parser_walk(m->kv_parser, osw_etc_walk_cb, NULL);
}

const char *osw_etc_get_value(osw_etc_data_t *m, const char *key)
{
    if (m == NULL) return NULL;
    const char *value = kv_parser_get_value(m->kv_parser, key);
    if (value != NULL) return value;

    return getenv(key);
}

/*
 * List of valid characters for the shell variable name
 */
static char osw_valid_var_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                    "abcdefghijklmnopqrstuvwxyz"
                                    "0123456789"
                                    "_";

static bool osw_filter_out_sh_key(const char *key)
{
    return strspn(key, osw_valid_var_chars) != strlen(key) ? true : false;
}

static void osw_etc_export_sh_cb(void *priv, const char *key, const char *value)
{
    size_t value_len = strlen(value);
    size_t i;

    /* FIXME: handle keys that contains invalid shell signs e.g. '-' */
    if (osw_filter_out_sh_key(key)) printf("# ");
    printf("export %s='", key);
    for (i = 0; i < value_len; i++)
    {
        if (value[i] == '\'')
            printf("'\"'\"'");
        else
            printf("%c", value[i]);
    }
    printf("'\n");
}

void osw_etc_export_sh_script(osw_etc_data_t *m)
{
    kv_parser_walk(m->kv_parser, osw_etc_export_sh_cb, NULL);
}

OSW_MODULE(osw_etc)
{
    static struct osw_etc_data m;
    osw_etc_init(&m);
    osw_etc_attach(&m);
    return &m;
}
