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

#ifndef LNX_MCAST_H_INCLUDED
#define LNX_MCAST_H_INCLUDED

#include <net/if.h>

#include "daemon.h"
#include "evx.h"

#include "osn_igmp.h"
#include "osn_mld.h"

typedef struct lnx_igmp lnx_igmp_t;
typedef struct lnx_mld lnx_mld_t;

struct lnx_igmp
{
    daemon_t                    daemon;
    bool                        initialized;
    ev_debounce                 apply_debounce;
    enum osn_igmp_version       version;

    bool                        snooping_enabled;
    char                        snooping_bridge[IFNAMSIZ];
    bool                        snooping_bridge_up;
    char                        static_mrouter[IFNAMSIZ];
    bool                        static_mrouter_up;
    char                      **mcast_exceptions;
    int                         mcast_exceptions_len;
    enum osn_mcast_unknown_grp  unknown_group;
    int                         robustness_value;
    int                         max_groups;
    int                         aging_time;
    bool                        fast_leave_enable;

    bool                        proxy_enabled;
    char                        proxy_upstream_if[IFNAMSIZ];
    bool                        proxy_upstream_if_up;
    char                        proxy_downstream_if[IFNAMSIZ];
    bool                        proxy_downstream_if_up;
};

struct lnx_mld
{
    daemon_t                    daemon;
    bool                        initialized;
    ev_debounce                 apply_debounce;
    enum osn_mld_version        version;

    bool                        snooping_enabled;
    char                        snooping_bridge[IFNAMSIZ];
    bool                        snooping_bridge_up;
    char                        static_mrouter[IFNAMSIZ];
    bool                        static_mrouter_up;
    char                      **mcast_exceptions;
    int                         mcast_exceptions_len;
    enum osn_mcast_unknown_grp  unknown_group;
    int                         robustness_value;
    int                         max_groups;
    int                         aging_time;
    bool                        fast_leave_enable;

    bool                        proxy_enabled;
    char                        proxy_upstream_if[IFNAMSIZ];
    bool                        proxy_upstream_if_up;
    char                        proxy_downstream_if[IFNAMSIZ];
    bool                        proxy_downstream_if_up;
};

typedef struct
{
    bool                        initialized;
    ev_debounce                 apply_debounce;
    int                         apply_retry;
    bool                        igmp_initialized;
    lnx_igmp_t                  igmp;
    bool                        mld_initialized;
    lnx_mld_t                   mld;
    char                        snooping_bridge[IFNAMSIZ];
    char                        static_mrouter[IFNAMSIZ];
} lnx_mcast_bridge;

lnx_igmp_t *lnx_mcast_bridge_igmp_init();
lnx_igmp_t *lnx_igmp_new();
bool lnx_igmp_del(lnx_igmp_t *self);
bool lnx_igmp_snooping_set(
        lnx_igmp_t *self,
        struct osn_igmp_snooping_config *config);
bool lnx_igmp_proxy_set(
        lnx_igmp_t *self,
        struct osn_igmp_proxy_config *config);
bool lnx_igmp_querier_set(
        lnx_igmp_t *self,
        struct osn_igmp_querier_config *config);
bool lnx_igmp_other_config_set(
        lnx_igmp_t *self,
        const struct osn_mcast_other_config *other_config);
bool lnx_igmp_update_iface_status(
        lnx_igmp_t *self,
        char *ifname,
        bool enable);
bool lnx_igmp_apply(lnx_igmp_t *self);
bool lnx_igmp_write_section(lnx_igmp_t *self, FILE *f);

lnx_mld_t *lnx_mcast_bridge_mld_init();
lnx_mld_t *lnx_mld_new();
bool lnx_mld_del(lnx_mld_t *self);
bool lnx_mld_snooping_set(
        lnx_mld_t *self,
        struct osn_mld_snooping_config *config);
bool lnx_mld_proxy_set(
        lnx_mld_t *self,
        struct osn_mld_proxy_config *config);
bool lnx_mld_querier_set(
        lnx_mld_t *self,
        struct osn_mld_querier_config *config);
bool lnx_mld_other_config_set(
        lnx_mld_t *self,
        const struct osn_mcast_other_config *other_config);
bool lnx_mld_update_iface_status(
        lnx_mld_t *self,
        char *ifname,
        bool enable);
bool lnx_mld_apply(lnx_mld_t *self);
bool lnx_mld_write_section(lnx_mld_t *self, FILE *f);

bool lnx_mcast_apply();
char *lnx_mcast_other_config_get_string(
        const struct osn_mcast_other_config *other_config,
        const char *key);
long lnx_mcast_other_config_get_long(
        const struct osn_mcast_other_config *other_config,
        const char *key);
bool lnx_mcast_free_string_array(char **arr, int len);

#endif /* LNXOSN_MCQCA_H_INCLUDED */
