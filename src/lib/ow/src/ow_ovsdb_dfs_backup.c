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

#include <memutil.h>
#include <ds_dlist.h>
#include <ds_tree.h>
#include <const.h>
#include <log.h>

#include <ovsdb.h>
#include <ovsdb_cache.h>

#include <osw_ut.h>
#include <osw_types.h>
#include <osw_module.h>
#include "ow_dfs_backup.h"
#include "ow_ovsdb.h"

/*
 * Purpose:
 *
 * Perform legacy style parent change upon dfs connection
 * loss on a sta interface.
 *
 * Description:
 *
 * Historically the device would self-modify
 * Wifi_Radio_Config, Wifi_VIF_Config and Wifi_Inet_Config
 * tables accordingly upon dfs connection loss. This is less
 * than ideal long-term, but this does provide a smooth way
 * to transition away with the new codebase.
 *
 * Future:
 *
 * This module will hopefully be replaced by a mutator-based
 * parent override and an explicit unlatching commands from
 * the controller.
 *
 * Notes:
 *
 * This does 2 things actually, arguably unnecessarily. It
 * feeds ow_dfs_backup with ovsdb (data model) configuration
 * _and_ acts upon backup trigger. These could be done
 * separately, but given how tightly coupled these are it
 * would probably be problematic to consider them separate
 * anyway.
 */

#define LOG_PREFIX "ow: ovsdb: dfs_backup: "

struct ow_ovsdb_dfs_backup {
    struct ow_dfs_backup *ow_dfs_backup;
    struct ow_dfs_backup_notify *backup_notify;
    ovsdb_table_t table;
};

static void
parent_change(struct ow_ovsdb_dfs_backup *m,
              const char *phy_name,
              const char *link_vif_name,
              const struct osw_hwaddr *bssid,
              const struct osw_channel *channel)
{
    if (WARN_ON(phy_name == NULL)) return;
    if (WARN_ON(link_vif_name == NULL)) return;

    const struct osw_hwaddr bssid0 = {0};
    const struct osw_channel chan0 = {0};

    struct osw_hwaddr_str bssid_strbuf;
    const char *bssid_str = "";
    if (bssid != NULL) {
        bssid_str = osw_hwaddr2str(bssid, &bssid_strbuf);
    }

    char chan_str[32] = {0};
    if (channel != NULL) {
        const int freq = channel->control_freq_mhz;
        const int chan = osw_freq_to_chan(freq);
        snprintf(chan_str, sizeof(chan_str), "%d", chan);
    }

    const char *output = strexa("/usr/opensync/bin/parentchange.sh",
                                phy_name,
                                bssid_str,
                                chan_str);

    LOGN(LOG_PREFIX"parent change: from %s to %s @ "OSW_HWADDR_FMT" on "OSW_CHANNEL_FMT": output = '%s'",
         link_vif_name,
         phy_name,
         OSW_HWADDR_ARG(bssid ?: &bssid0),
         OSW_CHANNEL_ARG(channel ?: &chan0),
         output ?: "");

    ow_dfs_backup_unlatch_vif(m->ow_dfs_backup, link_vif_name);
}

static void
backup_cb(const char *phy_name,
          const char *link_vif_name,
          enum ow_dfs_backup_phy_state state,
          const struct osw_hwaddr *bssid,
          const struct osw_channel *channel,
          void *priv)
{
    struct ow_ovsdb_dfs_backup *m = priv;
    switch (state) {
        case OW_DFS_BACKUP_PHY_REMOVED:
            break;
        case OW_DFS_BACKUP_PHY_CONFIGURED:
            break;
        case OW_DFS_BACKUP_PHY_LATCHED:
            parent_change(m, phy_name, link_vif_name, bssid, channel);
            break;
    }
}

static void
rconf_cb(ovsdb_update_monitor_t *mon,
         struct schema_Wifi_Master_State *old,
         struct schema_Wifi_Master_State *rec,
         ovsdb_cache_row_t *row)
{
    ovsdb_table_t *table = mon->mon_data;
    struct ow_ovsdb_dfs_backup *m = container_of(table, struct ow_ovsdb_dfs_backup, table);
    const struct schema_Wifi_Radio_Config *rconf = (const void *)row->record;
    struct ow_dfs_backup_phy *phy = ow_dfs_backup_get_phy(m->ow_dfs_backup, rconf->if_name);

    ow_dfs_backup_phy_reset(phy);

    if (rconf->fallback_parents_len < 1) return;
    WARN_ON(rconf->fallback_parents_len > 1);

    const char *bssid_str = rconf->fallback_parents_keys[0];
    const int chan = rconf->fallback_parents[0];
    const enum osw_band band = rconf->freq_band[0] == '2' ? OSW_BAND_2GHZ
                             : rconf->freq_band[0] == '5' ? OSW_BAND_5GHZ
                             : rconf->freq_band[0] == '6' ? OSW_BAND_6GHZ
                             : OSW_BAND_UNDEFINED;
    const int freq_mhz = osw_chan_to_freq(band, chan);
    const enum osw_channel_width width = ow_ovsdb_htmode2width(rconf->ht_mode);
    const struct osw_channel c = {
        .control_freq_mhz = freq_mhz,
        .width = width,
    };

    struct osw_hwaddr bssid = {0};
    const bool bssid_bad = osw_hwaddr_from_cstr(bssid_str, &bssid) == false;
    const bool freq_bad = freq_mhz < 2000;

    if (WARN_ON(bssid_bad == true)) return;
    if (WARN_ON(freq_bad == true)) return;

    ow_dfs_backup_phy_set_bssid(phy, &bssid);
    ow_dfs_backup_phy_set_channel(phy, &c);
}

static void
connect_cb(void *priv)
{
    struct ow_ovsdb_dfs_backup *m = priv;
    ovsdb_cache_monitor(&m->table, (void *)rconf_cb, true);
}

static void
mod_init(struct ow_ovsdb_dfs_backup *m)
{
    OVSDB_TABLE_VAR_INIT(&m->table, Wifi_Radio_Config, if_name);
}

static void
mod_attach(struct ow_ovsdb_dfs_backup *m)
{
    m->backup_notify = ow_dfs_backup_add_notify(m->ow_dfs_backup,
                                                __FILE__,
                                                backup_cb,
                                                m);
    ovsdb_when_ready(connect_cb, m);
}

OSW_MODULE(ow_ovsdb_dfs_backup)
{
    static struct ow_ovsdb_dfs_backup m;
    m.ow_dfs_backup = OSW_MODULE_LOAD(ow_dfs_backup);
    mod_init(&m);
    mod_attach(&m);
    return &m;
}
