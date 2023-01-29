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

#ifndef OSW_DRV_TARGET_H_INCLUDED
#define OSW_DRV_TARGET_H_INCLUDED

struct osw_drv_target;

struct osw_drv_target *
osw_drv_target_get_ref(void);

void
osw_drv_target_vif_set_exists(struct osw_drv_target *target,
                              const char *vif_name,
                              bool exists);

void
osw_drv_target_vif_set_phy(struct osw_drv_target *target,
                           const char *vif_name,
                           const char *phy_name);

void
osw_drv_target_vif_set_mode(struct osw_drv_target *target,
                            const char *vif_name,
                            const char *mode);

void
osw_drv_target_vif_set_idx(struct osw_drv_target *target,
                           const char *vif_name,
                           int vif_radio_idx);

void
osw_drv_target_phy_set_hw_type(struct osw_drv_target *target,
                               const char *phy_name,
                               const char *hw_type);

void
osw_drv_target_phy_set_hw_config(struct osw_drv_target *target,
                                 const char *phy_name,
                                 const char **hw_config_keys,
                                 const char **hw_config_values,
                                 size_t n_hw_config);

void
osw_drv_target_bsal_vap_register(struct osw_drv_target *target,
                                 const char *vif_name);

void
osw_drv_target_bsal_vap_unregister(struct osw_drv_target *target,
                                   const char *vif_name);

void
osw_drv_target_bsal_ue_register(struct osw_drv_target *target,
                                const struct osw_hwaddr *mac_addr);

void
osw_drv_target_bsal_ue_unregister(struct osw_drv_target *target,
                                  const struct osw_hwaddr *mac_addr);

#endif /* OSW_DRV_TARGET_H_INCLUDED */
