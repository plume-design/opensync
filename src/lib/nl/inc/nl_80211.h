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

#ifndef NL_80211_H_INCLUDED
#define NL_80211_H_INCLUDED

/**
 * @file nl_80211.h
 * @brief nl80211 helper
 *
 * There is a couple of things a typical nl80211 consumer
 * shouldn't need to worry about - like submitting initial
 * commands to resolve generic netlink nl80211 service,
 * install mcast memberships, or figure out if wiphy-split
 * feature is supported.
 *
 * It also provides a couple of convenience calls for nl_msg
 * assembly as well as phy/vif/sta identifier mappings.
 */

#include <os_types.h>
#include <netlink/attr.h>

struct nl_80211;
struct nl_80211_subscription;

struct nl_80211_phy {
    const char *name;
    uint32_t wiphy;
};

struct nl_80211_vif {
    const char *name;
    uint32_t ifindex;
    uint32_t wiphy;
};

struct nl_80211_sta {
    os_macaddr_t addr;
    uint32_t ifindex;
};

typedef void
nl_80211_ready_fn_t(struct nl_80211 *nl_80211,
                    void *priv);

typedef void
nl_80211_phy_each_fn_t(const struct nl_80211_phy *phy,
                       void *priv);

typedef void
nl_80211_vif_each_fn_t(const struct nl_80211_vif *vif,
                       void *priv);

typedef void
nl_80211_sta_each_fn_t(const struct nl_80211_sta *sta,
                       void *priv);

struct nl_80211 *
nl_80211_alloc(void);

void
nl_80211_free(struct nl_80211 *nl_80211);

bool
nl_80211_is_ready(const struct nl_80211 *nl_80211);

void
nl_80211_stop(struct nl_80211 *nl_80211);

void
nl_80211_start(struct nl_80211 *nl_80211);

void
nl_80211_set_conn(struct nl_80211 *nl_80211,
                  struct nl_conn *conn);

struct nl_conn *
nl_80211_get_conn(struct nl_80211 *nl_80211);

void
nl_80211_set_ready_fn(struct nl_80211 *nl_80211,
                      nl_80211_ready_fn_t *fn,
                      void *priv);

void
nl_80211_put_cmd(struct nl_80211 *nl_80211,
                 struct nl_msg *msg,
                 int flags,
                 uint8_t cmd);

void
nl_80211_put_wiphy(struct nl_80211 *nl_80211,
                   struct nl_msg *msg);

const struct nl_80211_phy *
nl_80211_phy_by_wiphy(struct nl_80211 *nl_80211,
                      uint32_t wiphy);

const struct nl_80211_phy *
nl_80211_phy_by_name(struct nl_80211 *nl_80211,
                     const char *phy_name);

const struct nl_80211_phy *
nl_80211_phy_by_ifindex(struct nl_80211 *nl_80211,
                        uint32_t ifindex);

const struct nl_80211_phy *
nl_80211_phy_by_nla(struct nl_80211 *nl_80211,
                    struct nlattr *tb[]);

const struct nl_80211_phy *
nl_80211_phy_by_nla(struct nl_80211 *nl_80211,
                    struct nlattr *tb[]);

const struct nl_80211_vif *
nl_80211_vif_by_ifindex(struct nl_80211 *nl_80211,
                        uint32_t ifindex);

const struct nl_80211_vif *
nl_80211_vif_by_name(struct nl_80211 *nl_80211,
                     const char *vif_name);

const struct nl_80211_vif *
nl_80211_vif_by_nla(struct nl_80211 *nl_80211,
                    struct nlattr *tb[]);

const struct nl_80211_sta *
nl_80211_sta_by_link(struct nl_80211 *nl_80211,
                     uint32_t ifindex,
                     const os_macaddr_t *addr);

const struct nl_80211_vif *
nl_80211_vif_by_nla(struct nl_80211 *nl_80211,
                    struct nlattr *tb[]);

const struct nl_80211_sta *
nl_80211_sta_by_link_name(struct nl_80211 *nl_80211,
                          const char *vif_name,
                          const os_macaddr_t *addr);

void
nl_80211_phy_each(struct nl_80211 *nl_80211,
                  nl_80211_phy_each_fn_t *fn,
                  void *fn_priv);

void
nl_80211_vif_each(struct nl_80211 *nl_80211,
                  const uint32_t *wiphy, /* NULL to report all vifs */
                  nl_80211_vif_each_fn_t *fn,
                  void *fn_priv);

void
nl_80211_sta_each(struct nl_80211 *nl_80211,
                  const uint32_t ifindex,
                  nl_80211_sta_each_fn_t *fn,
                  void *fn_priv);

struct nl_msg *
nl_80211_alloc_get_phy(struct nl_80211 *nl_80211,
                       uint32_t wiphy);

struct nl_msg *
nl_80211_alloc_get_interface(struct nl_80211 *nl_80211,
                             uint32_t ifindex);

struct nl_msg *
nl_80211_alloc_get_sta(struct nl_80211 *nl_80211,
                       uint32_t ifindex,
                       const void *mac);

struct nl_msg *
nl_80211_alloc_dump_scan(struct nl_80211 *nl_80211,
                         uint32_t ifindex);

typedef void
nl_80211_sub_phy_added_fn_t(const struct nl_80211_phy *info,
                            void *priv);

typedef void
nl_80211_sub_phy_renamed_fn_t(const struct nl_80211_phy *info,
                              const char *old_name,
                              const char *new_name,
                              void *priv);

typedef void
nl_80211_sub_phy_removed_fn_t(const struct nl_80211_phy *info,
                              void *priv);

typedef void
nl_80211_sub_vif_added_fn_t(const struct nl_80211_vif *info,
                            void *priv);

typedef void
nl_80211_sub_vif_renamed_fn_t(const struct nl_80211_vif *info,
                              const char *old_name,
                              const char *new_name,
                              void *priv);

typedef void
nl_80211_sub_vif_removed_fn_t(const struct nl_80211_vif *info,
                              void *priv);

typedef void
nl_80211_sub_sta_added_fn_t(const struct nl_80211_sta *info,
                            void *priv);

typedef void
nl_80211_sub_sta_removed_fn_t(const struct nl_80211_sta *info,
                              void *priv);

struct nl_80211_sub_ops {
    nl_80211_sub_phy_added_fn_t *phy_added_fn;
    nl_80211_sub_phy_renamed_fn_t *phy_renamed_fn;
    nl_80211_sub_phy_removed_fn_t *phy_removed_fn;

    nl_80211_sub_vif_added_fn_t *vif_added_fn;
    nl_80211_sub_vif_renamed_fn_t *vif_renamed_fn;
    nl_80211_sub_vif_removed_fn_t *vif_removed_fn;

    nl_80211_sub_sta_added_fn_t *sta_added_fn;
    nl_80211_sub_sta_removed_fn_t *sta_removed_fn;

    size_t priv_phy_size;
    size_t priv_vif_size;
    size_t priv_sta_size;
};

struct nl_80211_sub;

struct nl_80211_sub *
nl_80211_alloc_sub(struct nl_80211 *nl_80211,
                   const struct nl_80211_sub_ops *ops,
                   void *priv);

void *
nl_80211_sub_phy_get_priv(struct nl_80211_sub *sub,
                          const struct nl_80211_phy *info);

void *
nl_80211_sub_vif_get_priv(struct nl_80211_sub *sub,
                          const struct nl_80211_vif *info);

void *
nl_80211_sub_sta_get_priv(struct nl_80211_sub *sub,
                          const struct nl_80211_sta *info);

void
nl_80211_sub_free(struct nl_80211_sub *sub);

#endif /* NL_80211_H_INCLUDED */
