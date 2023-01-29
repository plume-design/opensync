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

#include "ff_lib.h"

#include <stdbool.h>

#include "ds.h"
#include "ff_provider.h"
#include "log.h"
#include "module.h"

#define LOG_PREFIX "[FF] "

static ds_list_t g_provider_list = DS_LIST_INIT(ff_provider_t, node);

static bool is_flag_enabled(const char *flag_name)
{
    ff_provider_t *p;
    bool ret = false;
    bool pret = false;

    ds_list_foreach(&g_provider_list, p)
    {
        pret = p->is_flag_enabled_fn(p, flag_name);
        LOGT(LOG_PREFIX "%s provider returns: %d", p->name, pret);
        ret |= pret;
    }
    return ret;
}

static void lib_init(void)
{
    static bool initialized = false;
    if (!initialized) {
        const char dynamic_providers[] = CONFIG_FF_DYNAMIC_PROVIDERS_PATH"/libff_provider_*";
        module_load_all(dynamic_providers);
        module_init();
        initialized = true;
    }
}

void ff_provider_register(ff_provider_t *provider)
{
    if (provider->name != NULL) {
        LOGT(LOG_PREFIX "Registering feature flag provider: %s", provider->name);
        ds_list_insert_tail(&g_provider_list, provider);
    } else {
        LOGE(LOG_PREFIX "Can't register feature flag provider with no name!");
    }
}

bool ff_is_flag_enabled(const char *flag_name)
{
    lib_init();
    return is_flag_enabled(flag_name);
}

