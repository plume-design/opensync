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
#include <util.h>
#include <osw_module.h>
#include <osw_state.h>

#define LOG_PREFIX "ow: csa xphy: ovsdb: "

struct ow_csa_xphy_ovsdb {
    struct osw_state_observer state_obs;
};

static void
ow_csa_xphy_ovsdb_cb(struct osw_state_observer *obs,
                     const struct osw_state_vif_info *vif,
                     const struct osw_state_phy_info *to_phy,
                     const struct osw_channel *to_chan)
{
    if (vif->drv_state->vif_type != OSW_VIF_STA) return;

    const struct osw_drv_vif_state_sta_link *link = &vif->drv_state->u.sta.link;
    const bool connected = (link->status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTED);
    const struct osw_hwaddr *bssid = connected ? &link->bssid : NULL;
    struct osw_hwaddr_str bssid_strbuf;
    const char *bssid_str = "";
    const char *bssid_str_test = getenv("OW_XPHY_CSA_OVSDB_BSSID_TEST");
    if (bssid != NULL) {
        bssid_str = osw_hwaddr2str(bssid, &bssid_strbuf);
    }
    if (bssid_str_test != NULL) {
        bssid_str = bssid_str_test;
    }

    char chan_str[32] = {0};
    const int freq = to_chan->control_freq_mhz;
    const int chan = osw_freq_to_chan(freq);
    snprintf(chan_str, sizeof(chan_str), "%d", chan);

    const char *phy_name = to_phy->phy_name;
    const char *output = strexa("/usr/opensync/bin/parentchange.sh",
                                phy_name,
                                bssid_str,
                                chan_str);

    const char *vif_name = vif->vif_name;
    const struct osw_hwaddr bssid0 = {0};

    LOGN(LOG_PREFIX"parent change: from %s to %s @ "OSW_HWADDR_FMT" (arg %s) on "OSW_CHANNEL_FMT" (arg %s): output = '%s'",
         vif_name,
         phy_name,
         OSW_HWADDR_ARG(bssid ?: &bssid0),
         bssid_str,
         OSW_CHANNEL_ARG(to_chan),
         chan_str,
         output ?: "");
}

static void
mod_init(struct ow_csa_xphy_ovsdb *m)
{
    const struct osw_state_observer obs = {
        .name = __FILE__,
        .vif_csa_to_phy_fn = ow_csa_xphy_ovsdb_cb,
    };
    m->state_obs = obs;
}

static void
mod_attach(struct ow_csa_xphy_ovsdb *m)
{
    OSW_MODULE_LOAD(osw_state);
    osw_state_register_observer(&m->state_obs);
}

OSW_MODULE(ow_xphy_csa_ovsdb)
{
    static struct ow_csa_xphy_ovsdb m;
    mod_init(&m);
    mod_attach(&m);
    return &m;
}
