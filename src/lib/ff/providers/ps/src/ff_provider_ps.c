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

#include "ff_provider_ps.h"

#include <string.h>
#include <stdbool.h>

#include "ff_provider.h"
#include "log.h"
#include "memutil.h"
#include "module.h"
#include "osp_ps.h"

#define LOG_PREFIX "[FF:PS] "
#define FF_PROVIDER_PS_MODULE_BASE_NAME ff_provider_ps

typedef struct module_data
{
    const char *ps_storage_name;
    const char *ps_storage_key;
    ff_provider_ps_t ff_provider_ps;
    ff_provider_t ff_provider;
} module_data_t;

static module_data_t *ff_provider_ps2data(ff_provider_ps_t *provider_ps)
{
    return CONTAINER_OF(provider_ps, module_data_t, ff_provider_ps);
}

static module_data_t *ff_provider2data(ff_provider_t *provider)
{
    return CONTAINER_OF(provider, module_data_t, ff_provider);
}

static bool tokenize_and_search_for_flag(const char *flag_name, char *list_of_flags)
{
    const char delim[] = ",";
    char *save_ptr = NULL;
    char *flag = NULL;

    flag = strtok_r(list_of_flags, delim, &save_ptr);
    do {
        if (strcmp(flag, flag_name) == 0) {
            return true;
        }
    } while ((flag = strtok_r(NULL, delim, &save_ptr)) != NULL);

    return false;
}

static bool set_flags(ff_provider_ps_t *provider_ps, char *flags)
{
    osp_ps_t *ps = NULL;
    bool ret = true;
    size_t value_len = strlen(flags);
    const char *ps_storage_name = ff_provider_ps2data(provider_ps)->ps_storage_name;
    const char *ps_storage_key = ff_provider_ps2data(provider_ps)->ps_storage_key;

    ps = osp_ps_open(ps_storage_name, OSP_PS_RDWR);
    if (ps == NULL)
    {
        LOGE(LOG_PREFIX "Error opening %s persistent store.", ps_storage_name);
        ret = false;
    }

    if (osp_ps_set(ps, ps_storage_key, flags, value_len) != (ssize_t)value_len)
    {
        LOGE(LOG_PREFIX "Error saving persistent %s key.", ps_storage_key);
        ret = false;
    }

exit:
    if (ps != NULL) osp_ps_close(ps);
    return ret;
}

static char* get_flags(ff_provider_ps_t *provider_ps)
{
    osp_ps_t *ps = NULL;
    char *list_of_flags = NULL;
    ssize_t value_size;
    const char *ps_storage_name = ff_provider_ps2data(provider_ps)->ps_storage_name;
    const char *ps_storage_key = ff_provider_ps2data(provider_ps)->ps_storage_key;

    ps = osp_ps_open(ps_storage_name, OSP_PS_RDWR);
    if (ps == NULL)
    {
        LOGE(LOG_PREFIX "Error opening %s persistent store.", ps_storage_name);
        goto exit;
    }

    value_size = osp_ps_get(ps, ps_storage_key, NULL, 0);
    if (value_size < 0)
    {
        LOGE(LOG_PREFIX "Error fetching %s key size.", ps_storage_key);
        goto exit;
    }
    else if (value_size == 0)
    {
        LOGD(LOG_PREFIX "Read 0 bytes for %s from persistent storage. The record does not exist yet.",
                ps_storage_key);
        goto exit;
    }

    list_of_flags = CALLOC(1, value_size + 1);
    if (osp_ps_get(ps, ps_storage_key, list_of_flags, value_size) != value_size)
    {
        LOGE(LOG_PREFIX "Error retrieving persistent %s key.", ps_storage_key);
        goto exit;
    }

exit:
    if (ps != NULL) osp_ps_close(ps);
    return list_of_flags;
}

static void free_flags_data(char **flags_data)
{
    FREE(*flags_data);
}

static bool ps_is_flag_enabled(ff_provider_ps_t *provider_ps, const char *flag_name)
{
    char *list_of_flags = get_flags(provider_ps);
    const bool ret = tokenize_and_search_for_flag(flag_name, list_of_flags);
    free_flags_data(&list_of_flags);
    return ret;
}

static bool is_flag_enabled(ff_provider_t *provider, const char *flag_name)
{
    return ps_is_flag_enabled(&ff_provider2data(provider)->ff_provider_ps, flag_name);
}

static module_data_t g_m_data = {
    .ps_storage_name = "ps_storage_ff",
    .ps_storage_key = "feature_flags",
    .ff_provider_ps = {
        .set_flags_fn = set_flags,
        .get_flags_fn = get_flags,
        .free_flags_data_fn = free_flags_data,
        .is_flag_enabled_fn = ps_is_flag_enabled,
    },
    .ff_provider = {
        .name = FF_PROVIDER_PS_MODULE_BASE_NAME_STR,
        .is_flag_enabled_fn = is_flag_enabled,
    },
};

static void ff_provider_ps_init(void *data)
{
    module_data_t *mod_data = data;
    ff_provider_register(&mod_data->ff_provider);
}

static void ff_provider_ps_fini(void *data)
{
    (void)(data);
}

MODULE_DATA(FF_PROVIDER_PS_MODULE_BASE_NAME, ff_provider_ps_init, ff_provider_ps_fini, &g_m_data)

ff_provider_ps_t *ff_provider_ps_get(void)
{
    return &g_m_data.ff_provider_ps;
}
