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

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include "log.h"
#include "ltem_mgr.h"
#include "osn_lte_esim.h"

int
ltem_update_esim(ltem_mgr_t *mgr)
{
    const char *activation_code;
    lte_config_info_t *lte_config_info;
    lte_state_info_t *lte_state_info;
    const char *esim_profile;
    int res;

    lte_config_info = mgr->lte_config_info;
    if (!lte_config_info) return -1;
    if (!lte_config_info->esim_activation_code[0]) return -1;

    lte_state_info = mgr->lte_state_info;
    if (!lte_state_info) return -1;

    activation_code = (const char *)lte_config_info->esim_activation_code;

    lte_state_info->esim_download_in_progress = true;
    ltem_ovsdb_update_lte_state(mgr);
    res = osn_lte_esim_download_profile(activation_code);
    if (res)
    {
        LOGI("%s: esim download failed for [%s]", __func__, activation_code);
        return -1;
    }

    lte_state_info->esim_download_in_progress = false;
    lte_state_info->esim_download_complete = true;
    ltem_ovsdb_update_lte_state(mgr);

    esim_profile = osn_lte_esim_get_profiles();
    if (esim_profile == NULL)
    {
        LOGI("%s: Unable to find any esim profiles", __func__);
        return -1;
    }

    res = osn_lte_esim_enable_profile(esim_profile);
    if (res)
    {
        LOGI("%s: enable profile [%s], failed", __func__, esim_profile);
        return -1;
    }

    STRSCPY(lte_state_info->esim_active_profile, esim_profile);
    ltem_ovsdb_update_lte_state(mgr);

    return 0;
}
