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

#include <schema.h>
#include <target.h>
#include "wm2_dummy.h"

bool
wm2_target_radio_init(const struct target_radio_ops *ops)
{
    if (wm2_dummy_target_desired())
        return wm2_dummy_target_radio_init(ops);

    return target_radio_init(ops);
}

bool
wm2_target_radio_config_init2(void)
{
    if (wm2_dummy_target_desired())
        return wm2_dummy_target_radio_config_init2();

    return target_radio_config_init2();
}

bool
wm2_target_radio_config_need_reset(void)
{
    if (wm2_dummy_target_desired())
        return wm2_dummy_target_radio_config_need_reset();

    return target_radio_config_need_reset();
}

bool
wm2_target_radio_config_set2(const struct schema_Wifi_Radio_Config *rconf,
                             const struct schema_Wifi_Radio_Config_flags *rchanged)
{
    if (wm2_dummy_target_desired())
        return wm2_dummy_target_radio_config_set2(rconf, rchanged);

    return target_radio_config_set2(rconf, rchanged);
}

bool
wm2_target_vif_config_set2(const struct schema_Wifi_VIF_Config *vconf,
                           const struct schema_Wifi_Radio_Config *rconf,
                           const struct schema_Wifi_Credential_Config *cconfs,
                           const struct schema_Wifi_VIF_Config_flags *vchanged,
                           int num_cconfs)
{
    if (wm2_dummy_target_desired())
        return wm2_dummy_target_vif_config_set2(vconf, rconf, cconfs, vchanged, num_cconfs);

    return target_vif_config_set2(vconf, rconf, cconfs, vchanged, num_cconfs);
}

bool
wm2_target_dpp_supported(void)
{
    if (wm2_dummy_target_desired())
        return wm2_dummy_target_dpp_supported();

    return target_dpp_supported();
}

bool
wm2_target_dpp_config_set(const struct schema_DPP_Config *config)
{
    if (wm2_dummy_target_desired())
        return wm2_dummy_target_dpp_config_set(config);

    return target_dpp_config_set(config);
}

bool
wm2_target_dpp_key_get(struct target_dpp_key *key)
{
    if (wm2_dummy_target_desired())
        return wm2_dummy_target_dpp_key_get(key);

    return target_dpp_key_get(key);
}
