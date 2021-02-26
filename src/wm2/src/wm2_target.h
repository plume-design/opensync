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

#ifndef WM2_TARGET_H_INCLUDED
#define WM2_TARGET_H_INCLUDED

bool wm2_target_radio_init(const struct target_radio_ops *ops);
bool wm2_target_radio_config_init2(void);
bool wm2_target_radio_config_need_reset(void);
bool wm2_target_radio_config_set2(const struct schema_Wifi_Radio_Config *rconf,
                                  const struct schema_Wifi_Radio_Config_flags *rchanged);
bool wm2_target_vif_config_set2(const struct schema_Wifi_VIF_Config *vconf,
                                const struct schema_Wifi_Radio_Config *rconf,
                                const struct schema_Wifi_Credential_Config *cconfs,
                                const struct schema_Wifi_VIF_Config_flags *vchanged,
                                int num_cconfs);
bool wm2_target_dpp_supported(void);
bool wm2_target_dpp_config_set(const struct schema_DPP_Config *config);
bool wm2_target_dpp_key_get(struct target_dpp_key *key);

#endif /* WM2_TARGET_H_INCLUDED */
