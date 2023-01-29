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

#ifndef OW_DFS_FP_H_INCLUDED
#define OW_DFS_FP_H_INCLUDED

#include <osw_types.h>

enum ow_dfs_backup_phy_state {
    OW_DFS_BACKUP_PHY_REMOVED,
    OW_DFS_BACKUP_PHY_CONFIGURED,
    OW_DFS_BACKUP_PHY_LATCHED,
};

typedef void ow_dfs_backup_notify_fn_t(const char *phy_name,
                                       const char *link_vif_name,
                                       enum ow_dfs_backup_phy_state state,
                                       const struct osw_hwaddr *bssid,
                                       const struct osw_channel *channel,
                                       void *priv);

struct ow_dfs_backup;
struct ow_dfs_backup_phy;
struct ow_dfs_backup_notify;

struct ow_dfs_backup_phy *
ow_dfs_backup_get_phy(struct ow_dfs_backup *b,
                      const char *phy_name);

void
ow_dfs_backup_phy_set_bssid(struct ow_dfs_backup_phy *phy,
                            const struct osw_hwaddr *bssid);

void
ow_dfs_backup_phy_set_channel(struct ow_dfs_backup_phy *phy,
                              const struct osw_channel *channel);

void
ow_dfs_backup_phy_reset(struct ow_dfs_backup_phy *phy);

void
ow_dfs_backup_unlatch_vif(struct ow_dfs_backup *b,
                          const char *vif_name);

struct ow_dfs_backup_notify *
ow_dfs_backup_add_notify(struct ow_dfs_backup *b,
                         const char *name,
                         ow_dfs_backup_notify_fn_t *fn,
                         void *fn_priv);

void
ow_dfs_backup_del_notify(struct ow_dfs_backup_notify *n);

#endif /* OW_DFS_FP_H_INCLUDED */
