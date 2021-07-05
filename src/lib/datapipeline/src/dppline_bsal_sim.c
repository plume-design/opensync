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

#define _GNU_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <mqueue.h>
#include <errno.h>

#include "os.h"
#include "util.h"
#include "dppline.h"
#include "dpp_bs_client.h"

#define QUEUE_MSG_SIZE 8192

static int g_dpp_out = -1;

static char*
event_type_to_str(dpp_bs_client_event_type_t event)
{
    switch (event) {
        case PROBE:
            return "PROBE";
        case CONNECT:
            return "CONNECT";
        case DISCONNECT:
            return "DISCONNECT";
        case BACKOFF:
            return "BACKOFF";
        case ACTIVITY:
            return "ACTIVITY";
        case OVERRUN:
            return "OVERRUN";
        case BAND_STEERING_ATTEMPT:
            return "BAND_STEERING_ATTEMPT";
        case CLIENT_STEERING_ATTEMPT:
            return "CLIENT_STEERING_ATTEMPT";
        case CLIENT_STEERING_STARTED:
            return "CLIENT_STEERING_STARTED";
        case CLIENT_STEERING_DISABLED:
            return "CLIENT_STEERING_DISABLED";
        case CLIENT_STEERING_EXPIRED:
            return "CLIENT_STEERING_EXPIRED";
        case CLIENT_STEERING_FAILED:
            return "CLIENT_STEERING_FAILED";
        case CLIENT_KICKED:
            return "CLIENT_KICKED";
        case AUTH_BLOCK:
            return "AUTH_BLOCK";
        case CLIENT_BS_BTM:
            return "CLIENT_BS_BTM";
        case CLIENT_STICKY_BTM:
            return "CLIENT_STICKY_BTM";
        case CLIENT_BTM:
            return "CLIENT_BTM";
        case CLIENT_CAPABILITIES:
            return "CLIENT_CAPABILITIES";
        case CLIENT_BS_BTM_RETRY:
            return "CLIENT_BS_BTM_RETRY";
        case CLIENT_STICKY_BTM_RETRY:
            return "CLIENT_STICKY_BTM_RETRY";
        case CLIENT_BTM_RETRY:
            return "CLIENT_BTM_RETRY";
        case CLIENT_RRM_BCN_RPT:
            return "CLIENT_RRM_BCN_RPT";
        case CLIENT_BS_KICK:
            return "CLIENT_BS_KICK";
        case CLIENT_STICKY_KICK:
            return "CLIENT_STICKY_KICK";
        case CLIENT_SPECULATIVE_KICK:
            return "CLIENT_SPECULATIVE_KICK";
        case CLIENT_DIRECTED_KICK:
            return "CLIENT_DIRECTED_KICK";
        case CLIENT_GHOST_DEVICE_KICK:
            return "CLIENT_GHOST_DEVICE_KICK";
        case CLIENT_BTM_STATUS:
            return "CLIENT_BTM_STATUS";
        default:
            return "NONE";
    }
}

static char*
disconnect_src_to_str(dpp_bs_client_disconnect_src_t src)
{
    switch (src) {
        case LOCAL:
            return "LOCAL";
        case REMOTE:
            return "REMOTE";
        default:
            return "NONE";
    }
}

static char*
disconnect_type_to_str(dpp_bs_client_disconnect_type_t type)
{
    switch (type) {
        case DISASSOC:
            return "DISASSOC";
        case DEAUTH:
            return "DEAUTH";
        default:
            return "NONE";
    }
}

bool
dpp_init(void)
{
    struct mq_attr attr;
    const char *prefix = getenv("BM_TEST_PREFIX");
    const char *queue_path;

    if (!prefix) return false;
    if (strlen(prefix) == 0) return false;

    LOGI("BM_TEST_PREFIX: %s", prefix);

    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = 1;
    attr.mq_msgsize = QUEUE_MSG_SIZE;

    queue_path = strfmta("%s_dpp_out", prefix);
    g_dpp_out = mq_open(queue_path, O_CREAT | O_WRONLY | O_NONBLOCK, 0644, &attr);
    if (g_dpp_out < 0) {
        LOGE("Failed to create queue: %s because: %s", queue_path, strerror(errno));
        exit(1);
    }

    return true;
}

bool
dpp_get_report(uint8_t *buff,
               size_t sz,
               uint32_t *packed_sz)
{
    /* BM should never reeach this code */
    exit(1);
}

bool dpp_put_bs_client(dpp_bs_client_report_data_t *rpt)
{
    dpp_bs_client_record_list_t *client_record;
    int ret;

    ds_dlist_foreach(&rpt->list, client_record)
    {
        const dpp_bs_client_record_t *client = &client_record->entry;
        unsigned int i;
        char *buffer = NULL;

        strgrow(&buffer, "mac="MAC_ADDRESS_FORMAT"\n", MAC_ADDRESS_PRINT(client->mac));
        strgrow(&buffer, "num_band_records=%d\n", client->num_band_records);

        for (i = 0; i < client->num_band_records; i++) {
            const dpp_bs_client_band_record_t *band = &client->band_record[i];
            unsigned int j;

            if (band->type == RADIO_TYPE_NONE)
                continue;

            strgrow(&buffer, "    type=%s\n", radio_get_name_from_type(band->type));
            strgrow(&buffer, "    ifname=%s\n", band->ifname);
            strgrow(&buffer, "    connected=%s\n", band->connected ? "true" : "false");
            strgrow(&buffer, "    rejects=%d\n", band->rejects);
            strgrow(&buffer, "    connects=%d\n", band->connects);
            strgrow(&buffer, "    disconnects=%d\n", band->disconnects);
            strgrow(&buffer, "    activity_changes=%d\n", band->activity_changes);
            strgrow(&buffer, "    steering_success_cnt=%d\n", band->steering_success_cnt);
            strgrow(&buffer, "    steering_fail_cnt=%d\n", band->steering_fail_cnt);
            strgrow(&buffer, "    steering_kick_cnt=%d\n", band->steering_kick_cnt);
            strgrow(&buffer, "    sticky_kick_cnt=%d\n", band->sticky_kick_cnt);
            strgrow(&buffer, "    probe_bcast_cnt=%d\n", band->probe_bcast_cnt);
            strgrow(&buffer, "    probe_bcast_blocked=%d\n", band->probe_bcast_blocked);
            strgrow(&buffer, "    probe_direct_cnt=%d\n", band->probe_direct_cnt);
            strgrow(&buffer, "    probe_direct_blocked=%d\n", band->probe_direct_blocked);
            strgrow(&buffer, "    num_event_records=%d\n", band->num_event_records);
            strgrow(&buffer, "    event_record=\n");

            if (band->num_event_records == 0)
                strgrow(&buffer, "        none\n\n"); 

            for (j = 0; j < band->num_event_records; j++) {
                const dpp_bs_client_event_record_t *event = &band->event_record[j];
                char assoc_ies_buffer[4096];
                
                if (bin2hex(event->assoc_ies, event->assoc_ies_len, assoc_ies_buffer, sizeof(assoc_ies_buffer)) < 0) {
                    LOGE("BSAL: Failed to compose DPP Client report: assoc_ies BIN -> HEX failed");
                    exit(1);
                }

                strgrow(&buffer, "        type=%s\n", event_type_to_str(event->type));
                strgrow(&buffer, "        rssi=%d\n", event->rssi);
                strgrow(&buffer, "        probe_bcast=%d\n", event->probe_bcast);
                strgrow(&buffer, "        probe_blocked=%d\n", event->probe_blocked);
                strgrow(&buffer, "        disconnect_src=%s\n", disconnect_src_to_str(event->disconnect_src));
                strgrow(&buffer, "        disconnect_type=%s\n", disconnect_type_to_str(event->disconnect_type));
                strgrow(&buffer, "        disconnect_reason=%d\n", event->disconnect_reason);
                strgrow(&buffer, "        backoff_enabled=%s\n", event->backoff_enabled ? "true" : "false");
                strgrow(&buffer, "        active=%s\n", event->active ? "true" : "false");
                strgrow(&buffer, "        rejected=%s\n", event->rejected ? "true" : "false");
                strgrow(&buffer, "        is_BTM_supported=%s\n", event->is_BTM_supported ? "true" : "false");
                strgrow(&buffer, "        is_RRM_supported=%s\n", event->is_RRM_supported ? "true" : "false");
                strgrow(&buffer, "        band_cap_2G=%s\n", event->band_cap_2G ? "true" : "false");
                strgrow(&buffer, "        band_cap_5G=%s\n", event->band_cap_5G ? "true" : "false");
                strgrow(&buffer, "        max_chwidth=%d\n", event->max_chwidth);
                strgrow(&buffer, "        max_streams=%d\n", event->max_streams);
                strgrow(&buffer, "        phy_mode=%d\n", event->phy_mode);
                strgrow(&buffer, "        max_MCS=%d\n", event->max_MCS);
                strgrow(&buffer, "        max_txpower=%d\n", event->max_txpower);
                strgrow(&buffer, "        is_static_smps=%s\n", event->is_static_smps ? "true" : "false");
                strgrow(&buffer, "        is_mu_mimo_supported=%s\n", event->is_mu_mimo_supported ? "true" : "false");
                strgrow(&buffer, "        rrm_caps_link_meas=%s\n", event->rrm_caps_link_meas ? "true" : "false");
                strgrow(&buffer, "        rrm_caps_neigh_rpt=%s\n", event->rrm_caps_neigh_rpt ? "true" : "false");
                strgrow(&buffer, "        rrm_caps_bcn_rpt_passive=%s\n", event->rrm_caps_bcn_rpt_passive ? "true" : "false");
                strgrow(&buffer, "        rrm_caps_bcn_rpt_active=%s\n", event->rrm_caps_bcn_rpt_active ? "true" : "false");
                strgrow(&buffer, "        rrm_caps_bcn_rpt_table=%s\n", event->rrm_caps_bcn_rpt_table ? "true" : "false");
                strgrow(&buffer, "        rrm_caps_lci_meas=%s\n", event->rrm_caps_lci_meas ? "true" : "false");
                strgrow(&buffer, "        rrm_caps_ftm_range_rpt=%s\n", event->rrm_caps_ftm_range_rpt ? "true" : "false");
                strgrow(&buffer, "        backoff_period=%d\n", event->backoff_period);
                strgrow(&buffer, "        assoc_ies=%s\n", assoc_ies_buffer);
                strgrow(&buffer, "        assoc_ies_len=%ld\n", event->assoc_ies_len);
                strgrow(&buffer, "        btm_status=%d\n\n", event->btm_status);
            }
        }

        ret = mq_send(g_dpp_out, buffer, strlen(buffer), 0);
        FREE(buffer);
        if (ret < 0) {
            LOGE("BSAL: Failed to send DPP Client report: %s", strerror(errno));
            exit(1);
        }

        LOGI("BSAL: Send DPP Client report");
    }

    return true;
}

int dpp_get_queue_elements(void)
{
    return 0;
}

bool dpp_put_rssi(dpp_rssi_report_data_t *rpt)
{
    LOGW("dpp: TODO rssi");
    return true;
}
