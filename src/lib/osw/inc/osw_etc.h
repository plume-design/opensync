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

#ifndef OSW_ETC_H_INCLUDED
#define OSW_ETC_H_INCLUDED

#include <osw_module.h>

#define OSW_DIR_SUFFIX     "opensync/osw"
#define RUN_DIR            "/var/run"
#define ETC_DIR            CONFIG_INSTALL_PREFIX "/etc"
#define FULL_RUN_DIR       RUN_DIR "/" OSW_DIR_SUFFIX
#define FULL_ETC_DIR       ETC_DIR "/" OSW_DIR_SUFFIX
#define OSW_ETC_CONFIG_DIR FULL_ETC_DIR ":" FULL_RUN_DIR

struct osw_etc_data;
typedef struct osw_etc_data osw_etc_data_t;

const char *osw_etc_get_value(osw_etc_data_t *m, const char *key);
void osw_etc_export_sh_script(osw_etc_data_t *m);

static inline const char *osw_etc_get(const char *key)
{
    return osw_etc_get_value(OSW_MODULE_LOAD_ONCE(osw_etc), key);
}

static inline void osw_etc_export_sh(void)
{
    return osw_etc_export_sh_script(OSW_MODULE_LOAD_ONCE(osw_etc));
}

#endif /* OSW_ETC_H_INCLUDED */
