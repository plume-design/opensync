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

#include <stdlib.h>

#include "module.h"
#include "ff_provider.h"

/*
 * This provider checks if there is a variable of name "flag_name" present/defined in the environment.
 * If the variable is defined then flag is considered ENABLED. If it's not defined
 * then flag is considered NOT ENABLED.
 */

static bool is_flag_enabled(ff_provider_t *provider, const char *flag_name)
{
    (void)provider;
    return (getenv(flag_name) != NULL);
}

static ff_provider_t g_this_provider = {
    .name = "ff_provider_env",
    .is_flag_enabled_fn = is_flag_enabled,
};

static void ff_provider_env_init(void *data)
{
    ff_provider_t *provider = data;
    ff_provider_register(provider);
}

static void ff_provider_env_fini(void *data)
{
    (void)(data);
}

MODULE_DATA(ff_provider_env, ff_provider_env_init, ff_provider_env_fini, &g_this_provider)
