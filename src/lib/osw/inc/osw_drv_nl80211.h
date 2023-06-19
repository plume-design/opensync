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

#ifndef OSW_DRV_NL80211_H_INCLUDED
#define OSW_DRV_NL80211_H_INCLUDED

#include <nl.h>
#include <osw_drv.h>

struct osw_drv_nl80211_ops;
struct osw_drv_nl80211_hook;

typedef void
osw_drv_nl80211_hook_fix_phy_state_fn_t(struct osw_drv_nl80211_hook *hook,
                                        const char *phy_name,
                                        struct osw_drv_phy_state *state,
                                        void *priv);

typedef void
osw_drv_nl80211_hook_fix_vif_state_fn_t(struct osw_drv_nl80211_hook *hook,
                                        const char *phy_name,
                                        const char *vif_name,
                                        struct osw_drv_vif_state *state,
                                        void *priv);

typedef void
osw_drv_nl80211_hook_pre_request_config_fn_t(struct osw_drv_nl80211_hook *hook,
                                             struct osw_drv_conf *drv_conf,
                                             void *priv);

typedef void
osw_drv_nl80211_hook_pre_request_stats_fn_t(struct osw_drv_nl80211_hook *hook,
                                            unsigned int stats_mask,
                                            void *priv);

typedef void
osw_drv_nl80211_hook_get_vif_list_fn_t(struct osw_drv_nl80211_hook *hook,
                                       const char *phy_name,
                                       osw_drv_report_vif_fn_t *report_vif_fn,
                                       void *fn_priv,
                                       void *priv);

typedef void
osw_drv_nl80211_hook_get_vif_state_fn_t(struct osw_drv_nl80211_hook *hook,
                                        const char *phy_name,
                                        const char *vif_name,
                                        struct osw_drv_vif_state *state,
                                        void *priv);

struct osw_drv_nl80211_hook_ops {
    osw_drv_nl80211_hook_fix_phy_state_fn_t *fix_phy_state_fn;
    osw_drv_nl80211_hook_fix_vif_state_fn_t *fix_vif_state_fn;
    osw_drv_nl80211_hook_pre_request_config_fn_t *pre_request_config_fn;
    osw_drv_nl80211_hook_pre_request_stats_fn_t *pre_request_stats_fn;
    osw_drv_nl80211_hook_get_vif_list_fn_t *get_vif_list_fn;
    osw_drv_nl80211_hook_get_vif_state_fn_t *get_vif_state_fn;
};

typedef struct nl_80211 *
osw_drv_nl80211_get_nl_80211_fn_t(struct osw_drv_nl80211_ops *ops);

typedef struct osw_drv_nl80211_hook *
osw_drv_nl80211_add_hook_ops_fn_t(struct osw_drv_nl80211_ops *ops,
                                  const struct osw_drv_nl80211_hook_ops *hook_ops,
                                  void *priv);

typedef void
osw_drv_nl80211_del_hook_fn_t(struct osw_drv_nl80211_ops *ops,
                              struct osw_drv_nl80211_hook *hook);

struct osw_drv_nl80211_ops {
    osw_drv_nl80211_get_nl_80211_fn_t *get_nl_80211_fn;
    osw_drv_nl80211_add_hook_ops_fn_t *add_hook_ops_fn;
    osw_drv_nl80211_del_hook_fn_t *del_hook_fn;
};

#endif /* OSW_DRV_NL80211_H_INCLUDED */
