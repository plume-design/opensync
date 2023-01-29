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

#ifndef OSW_HOSTAP_H_INCLUDED
#define OSW_HOSTAP_H_INCLUDED

#include <osw_drv.h>
#include <osw_hostap_conf.h>

struct osw_hostap;
struct osw_hostap_bss;

struct osw_hostap_bss_sta {
    struct osw_hwaddr addr;
    int key_id;
    /* FIXME: pairwise, akm, pmf, .. */
};

typedef void
osw_hostap_ops_bss_event_fn_t(const char *msg,
                              size_t msg_len,
                              void *priv);

typedef void
osw_hostap_ops_bss_changed_fn_t(void *priv);

typedef void
osw_hostap_ops_bss_config_applied_fn_t(void *priv);

typedef void
osw_hostap_ops_bss_sta_connected_fn_t(const struct osw_hostap_bss_sta *sta,
                                      void *priv);

typedef void
osw_hostap_ops_bss_sta_changed_fn_t(const struct osw_hostap_bss_sta *sta,
                                    void *priv);

typedef void
osw_hostap_ops_bss_sta_disconnected_fn_t(const struct osw_hostap_bss_sta *sta,
                                         void *priv);

struct osw_hostap_bss_ops {
    /* Called whenever an unsolicited message appears on the
     * control interface for given bss. Expect things like
     * CTRL-EVENT-CONNECTED, AP-DISABLED, etc.
     */
    osw_hostap_ops_bss_event_fn_t *event_fn;

    /* Called whenever something happens that might've
     * invalidated the state of the BSS. For example a
     * configuration task has finished, or the control
     * socket was closed/opened.
     */
    osw_hostap_ops_bss_changed_fn_t *bss_changed_fn;

    /* Called whenever configuration task completed.
     * Configurations can be started with
     * osw_hostap_set_conf() and any out-of-sync BSSes will
     * get reconfigured. Others that are already in-sync may
     * not generate any subsequent events though.
     */
    osw_hostap_ops_bss_config_applied_fn_t *config_applied_fn;

    osw_hostap_ops_bss_sta_connected_fn_t *sta_connected_fn;
    osw_hostap_ops_bss_sta_changed_fn_t *sta_changed_fn;
    osw_hostap_ops_bss_sta_disconnected_fn_t *sta_disconnected_fn;
};

struct osw_hostap_hook;

typedef void
osw_hostap_ops_ap_conf_mutate_fn_t(struct osw_hostap_hook *hook,
                                   const char *phy_name,
                                   const char *vif_name,
                                   struct osw_drv_conf *drv_conf,
                                   struct osw_hostap_conf_ap_config *hapd_conf,
                                   void *priv);

struct osw_hostap_hook_ops {
    osw_hostap_ops_ap_conf_mutate_fn_t *ap_conf_mutate_fn;
};

struct osw_hostap_hook *
osw_hostap_hook_alloc(struct osw_hostap *hostap,
                      const struct osw_hostap_hook_ops *ops,
                      void *ops_priv);

void
osw_hostap_hook_free(struct osw_hostap_hook *hook);

struct osw_hostap_bss *
osw_hostap_bss_alloc(struct osw_hostap *hostap,
                     const char *phy_name,
                     const char *vif_name,
                     const struct osw_hostap_bss_ops *ops,
                     void *ops_priv);

void
osw_hostap_bss_free(struct osw_hostap_bss *bss);

void
osw_hostap_bss_fill_state(struct osw_hostap_bss *bss,
                          struct osw_drv_vif_state *state);

struct rq_task *
osw_hostap_bss_prep_state_task(struct osw_hostap_bss *bss);


struct rq_task *
osw_hostap_set_conf(struct osw_hostap *hostap,
                    struct osw_drv_conf *conf);

struct rq_task *
osw_hostap_prep_config_task(struct osw_hostap *hostap);

#endif /* OSW_HOSTAP_H_INCLUDED */
