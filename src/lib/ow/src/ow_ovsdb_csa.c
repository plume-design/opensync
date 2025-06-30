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

#include <log.h>
#include <const.h>
#include <osw_module.h>
#include <osw_state.h>
#include <ovsdb_table.h>
#include <ovsdb_sync.h>
#include "ow_ovsdb.h"

/**
 * Purpose:
 *
 * This provides a legacy way of updating the local data
 * model of OVSDB to match the soon-to-be topology change by
 * inheriting the CSA channel from a parent (when acting as
 * a leaf).
 *
 * Reason:
 *
 * There's an ambiguity of what a leaf device should do if
 * it's implicitly moved to a new channel and then loses a
 * connection - should it stick to the Wifi_Radio_Config
 * implied channel (ie. move back to some other channel if
 * there was a STA-related CSA before), or should it stick
 * to the last STA interface channel. This policy has been
 * left undefined and therefore needs this behaviour in
 * order to provide sane default behavior. Without the
 * policy there's no other way to address this other than
 * implicitly influencing data model adapters by
 * reconfiguring the underlying configuration locally,
 * effectively overriding what a remote controller requested
 * before.
 *
 * Future:
 *
 * If the data model becomes able to express the
 * channel-stickiness policy explicitly, then this override
 * will become unnecessary since data model related modules
 * will be able to take care of this ambiguity in more
 * explicit way on their own.
 */

#define LOG_PREFIX "ow: ovsdb: csa: "

struct ow_ovsdb_csa {
    struct osw_state_observer state_obs;
    ovsdb_table_t table;
};

static json_t *
ow_ovsdb_csa_where_rconf(const char *phy_name,
                         const json_int_t chan)
{
    json_t *where = json_array();
    json_t *j_ifname = json_string(phy_name);
    json_t *j_chan = json_integer(chan);
    json_t *w_ifname = ovsdb_tran_cond_single_json(SCHEMA_COLUMN(Wifi_Radio_Config, if_name),
                                                   OFUNC_EQ,
                                                   j_ifname);
    json_t *w_channel = ovsdb_tran_cond_single_json(SCHEMA_COLUMN(Wifi_Radio_Config, channel),
                                                    OFUNC_NEQ,
                                                    j_chan);
    json_array_append(where, w_ifname);
    json_array_append(where, w_channel);
    return where;
}

static void
ow_ovsdb_csa_fix_rconf(struct ow_ovsdb_csa *m,
                       const char *phy_name,
                       const struct osw_channel *channel)
{
    struct schema_Wifi_Radio_Config current_rconf = {0};
    struct schema_Wifi_Radio_Config rconf = {0};
    const int freq = channel->control_freq_mhz;
    const int chan = osw_freq_to_chan(freq);
    const int center_freq = channel->center_freq0_mhz;
    const int center_chan = osw_freq_to_chan(center_freq);
    const char *ht_mode = ow_ovsdb_width_to_htmode(channel->width);

    WARN_ON(ht_mode == NULL);

    json_t *where = ow_ovsdb_csa_where_rconf(phy_name, chan);
    if (WARN_ON(where == NULL)) return;

    /* the `where` is used twice: for select and later for
     * update, so bump the refcount from 1 to 2.
     */
    json_incref(where);

    ovsdb_table_select_one_where(&m->table, where, &current_rconf);
    /* `where` has refcount=1 now */

    rconf._partial_update = true;
    SCHEMA_SET_STR(rconf.if_name, phy_name);
    SCHEMA_SET_INT(rconf.channel, chan);
    /* Controller can update the center_freq0_chan only on
     * select radios. Hence device needs to be careful where
     * it updates it too. If it were to update an entry
     * where controller doesn't manage this it would result
     * in mismatched channel when controller wants to update
     * the channel (without the center_freq0_chan)
     * subsequently. This is currently being controlled on
     * 6G radios, notably the 11be capable ones (where the
     * center chan location can be ambiguous), but might
     * change in the future.
     */
    if (current_rconf.center_freq0_chan_exists)
        SCHEMA_SET_INT(rconf.center_freq0_chan, center_chan);
    if (ht_mode != NULL)
        SCHEMA_SET_STR(rconf.ht_mode, ht_mode);

    const bool ok = ovsdb_table_update_where(&m->table, where, &rconf);
    /* `where` has refcount=0, it has been freed */

    if (ok == false) return;

    LOGN(LOG_PREFIX"%s: overriding channel: %d %s (%s center %d)",
         phy_name,
         chan,
         ht_mode ?: "unknown-width",
         rconf.center_freq0_chan_exists ? "with" : "without",
         center_chan);

    return;

}

static void
ow_ovsdb_csa_rx_cb(struct osw_state_observer *obs,
                   const struct osw_state_vif_info *vif,
                   const struct osw_channel *channel)
{
    struct ow_ovsdb_csa *m = container_of(obs, struct ow_ovsdb_csa, state_obs);
    const char *phy_name = vif->phy->phy_name;
    const struct osw_drv_vif_state_sta_link *link = &vif->drv_state->u.sta.link;
    const bool connected = (link->status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTED);
    const bool bw_mismatch = (link->channel.width != channel->width);

    /* This module isn't really expected to adjust
     * configured channel width, but it does make sense to
     * log attempts where CSA would imply a different one.
     */
    if (connected && bw_mismatch) {
        LOGN(LOG_PREFIX"%s: cowardly refusing to handle width change: "OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
            phy_name,
            OSW_CHANNEL_ARG(&link->channel),
            OSW_CHANNEL_ARG(channel));
    }

    ow_ovsdb_csa_fix_rconf(m, phy_name, channel);
}

static void
mod_init(struct ow_ovsdb_csa *m)
{
    const struct osw_state_observer obs = {
        .name = __FILE__,
        .vif_csa_rx_fn = ow_ovsdb_csa_rx_cb,
    };

    m->state_obs = obs;
    OVSDB_TABLE_VAR_INIT(&m->table, Wifi_Radio_Config, if_name);
}

static void
mod_attach(struct ow_ovsdb_csa *m)
{
    osw_state_register_observer(&m->state_obs);
}

OSW_MODULE(ow_ovsdb_csa)
{
    static struct ow_ovsdb_csa m;
    mod_init(&m);
    mod_attach(&m);
    return &m;
}
