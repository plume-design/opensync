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

#ifndef FF_PROVIDERS_FF_PROVIDER_PS_H_INCLUDED
#define FF_PROVIDERS_FF_PROVIDER_PS_H_INCLUDED

#include <stdbool.h>

#define FF_PROVIDER_PS_MODULE_BASE_NAME_STR "ff_provider_ps"

typedef struct ff_provider_ps ff_provider_ps_t;

typedef bool (ff_provider_ps_set_flags_fn_t)(ff_provider_ps_t *provider_ps, char *flags);
typedef char* (ff_provider_ps_get_flags_fn_t)(ff_provider_ps_t *provider_ps);
typedef void (ff_provider_ps_free_flags_data_fn_t)(char **flags_data);
typedef bool (ff_provider_ps_is_flag_enabled_fn_t)(ff_provider_ps_t *provider_ps, const char *flag_name);

/**
 * Feature flag provider - persistent storage
 * This is a structure that can be obtained from ff_provider_ps module.
 * Structure contains operations that can be executed on persistent storage.
 */
struct ff_provider_ps
{
    /* Set flags in persistent storage to specified value */
    ff_provider_ps_set_flags_fn_t *set_flags_fn;

    /* Obtain flags from persistent storage; return value must be freed with free_flags_data_fn */
    ff_provider_ps_get_flags_fn_t *get_flags_fn;

    /* Frees data allocated by get_flags_fn */
    ff_provider_ps_free_flags_data_fn_t *free_flags_data_fn;

    /* Checks if given flag is set in persistent storage */
    ff_provider_ps_is_flag_enabled_fn_t *is_flag_enabled_fn;
};

ff_provider_ps_t *ff_provider_ps_get(void);

#endif /* FF_PROVIDERS_FF_PROVIDER_PS_H_INCLUDED */
