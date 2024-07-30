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

#include "const.h"
#include "ff_provider.h"
#include "log.h"
#include "module.h"
#include "util.h"

#include <string.h>
#include <unistd.h>

/*
 * This provider checks if there is a file of name "flag_name" present in the filesystem.
 * If the file exists then flag is considered ENABLED. If it's not exist
 * then flag is considered NOT ENABLED.
 */

#define LOG_PREFIX "[FF:ACCESS] "
#define FF_PROVIDER_ACCESS_MODULE_BASE_NAME ff_provider_access

static bool is_flag_enabled(ff_provider_t *provider, const char *flag_name)
{
    (void)provider;
    char ff_path[C_MAXPATH_LEN];

    /* Do not allow traveling over file system */
    if (strchr(flag_name, '/') != NULL)
    {
        LOGW(LOG_PREFIX "flag_name uses forbidden character");
        return false;
    }

    snprintf(ff_path, sizeof(ff_path), "%s/%s", CONFIG_FF_ACCESS_PATH, flag_name);
    if (access(ff_path, F_OK) == 0)
    {
        return true;
    }

    return false;
}

static ff_provider_t g_this_provider = {
    .name = "ff_provider_access",
    .is_flag_enabled_fn = is_flag_enabled,
};

static void ff_provider_access_init(void *data)
{
    ff_provider_t *provider = data;
    ff_provider_register(provider);
}

static void ff_provider_access_fini(void *data)
{
    (void)(data);
}

MODULE_DATA(ff_provider_access, ff_provider_access_init, ff_provider_access_fini, &g_this_provider)
