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

#include "bm.h"
#include "util.h"
#include "bm_ieee80211.h"
#include "bm_util_opclass.h"

static void
bm_action_frame_radio_measurement_report(bm_client_t *client,
                                      const u8 *payload,
                                      size_t plen)
{
    const struct element *elem;
    struct rrm_measurement_report_element *report_element;
    struct rrm_measurement_beacon_report *beacon_report;
    const u8 *ies;
    u8 len;

    if (bm_client_should_ignore_beacon_measurement_reports(client))
        return;

    ies = payload + 1;
    len = plen - 1;

    for_each_element_id(elem, WLAN_EID_MEASURE_REPORT, ies, len) {
        report_element = (struct rrm_measurement_report_element *) elem;
        if (report_element->len < sizeof(*report_element)) {
            LOGI("%s skip rrm report element len %d", client->mac_addr, report_element->len);
            break;
        }

        switch (report_element->type) {
        case MEASURE_TYPE_BEACON:
            beacon_report = (struct rrm_measurement_beacon_report *) report_element->variable;
            if (report_element->len < sizeof(*report_element) + sizeof(*beacon_report) - 2) {
                LOGI("%s skip beacon report len %d", client->mac_addr, report_element->len);
                break;
            }

            /* Check if "channel" is a valid operating channel */
            if (!ieee80211_global_op_class_is_channel_supported(beacon_report->op_class, beacon_report->channel)) {
                LOGI("%s ignore beacon report due to op_class and channel mismatch: [%d/%d]", client->mac_addr,
                     beacon_report->op_class, beacon_report->channel);
                bm_client_ignore_beacon_measurement_reports(client);
                return;
            }

            /* See IEEE 802.11-2016; Table 9-154—RCPI values */
            if (beacon_report->rcpi < 1 || beacon_report->rcpi > 219) {
                LOGI("%s skip beacon report due to RCPI value being outside of usable/valid range: %d", client->mac_addr,
                     beacon_report->rcpi);

                if (beacon_report->rcpi == 255) {
                    /* 255 stands for "Measurement not available", just skip this single report */
                    continue;
                }

                bm_client_ignore_beacon_measurement_reports(client);
                return;
            }

            WARN_ON(!bm_client_set_rrm_neighbor(client, beacon_report->bssid,
                                                beacon_report->channel, beacon_report->rcpi,
                                                beacon_report->rsni));
            break;
        default:
            LOGI("unknown measure type %d", elem->data[4]);
        }
    }
}

static void
bm_action_frame_rrm(bm_client_t *client,
                    const struct ieee80211_mgmt *mgmt,
                    size_t len)
{
    u8 action;
    const u8 *payload;
    size_t plen;

    if (len < IEEE80211_HDRLEN + 2)
        return;

    payload = ((const u8 *) mgmt) + IEEE80211_HDRLEN + 1;
    action = *payload++;
    plen = len - IEEE80211_HDRLEN - 2;

    switch (action) {
    case WLAN_RRM_NEIGHBOR_REPORT_REQUEST:
        LOGI("%s receive WLAN_RRM_NEIGHBOR_REPORT_REQUEST", client->mac_addr);
        break;
    case WLAN_RRM_NEIGHBOR_REPORT_RESPONSE:
        LOGI("%s receive WLAN_RRM_NEIGHBOR_REPORT_RESPONSE", client->mac_addr);
        break;
    case WLAN_RRM_RADIO_MEASUREMENT_REPORT:
        LOGI("%s receive WLAN_RRM_RADIO_MEASUREMENT_REPORT", client->mac_addr);
        bm_action_frame_radio_measurement_report(client, payload, plen);
    default:
        break;
    }

}

static void
bm_ie_parse_neighbor_report(bm_client_t *client, struct neighbor_report_element *neighbor_element)
{
    bm_client_btm_retry_neigh_t *retry_neigh;
    int neigh_pref = -1;
    const struct element *subelem;
    const struct neighbor_preference_subelement *neigh_pref_sub;
    const unsigned int neigh_elem_len_w_hdr = neighbor_element->len + 2;
    const unsigned int neigh_elem_struct_size = sizeof(*neighbor_element);

    if (neigh_elem_len_w_hdr < neigh_elem_struct_size) {
        LOGI("Neighbor report element too small neigh_elem_len_w_hdr = %d neigh_elem_struct_size = %d",
             neigh_elem_len_w_hdr,
             neigh_elem_struct_size);
        return;
    }

    if (client->btm_retry_neighbors_len >= BTM_RETRY_MAX_NEIGHBORS) {
        LOGI("%s btm retry neighbor list full", client->mac_addr);
        return;
    }

    for_each_element_id(subelem,
                        WLAN_SEID_CANDIDATE_PREFERENCE,
                        neighbor_element->variable,
                        neigh_elem_len_w_hdr - neigh_elem_struct_size) {

        if (subelem->datalen > 0) {
            LOGT("Candidate Preference subelement present");
            neigh_pref_sub = (struct neighbor_preference_subelement *)subelem;
            neigh_pref = neigh_pref_sub->preference;
            break;
        }
    }

    LOGI("%s adding neighbor from btm reject bssid = "
         PRI_os_macaddr_lower_t " bssid_info = %02x%02x%02x%02x op_class = %hhu channel = %hhu phy_type = %hhu preference = %u",
         client->mac_addr,
         FMT_os_macaddr_pt((os_macaddr_t *)neighbor_element->bssid),
         neighbor_element->bssid_info[3],
         neighbor_element->bssid_info[2],
         neighbor_element->bssid_info[1],
         neighbor_element->bssid_info[0],
         neighbor_element->op_class,
         neighbor_element->channel,
         neighbor_element->phy_type,
         neigh_pref);

    retry_neigh = &client->btm_retry_neighbors[client->btm_retry_neighbors_len];
    retry_neigh->preference = neigh_pref;
    memcpy(&retry_neigh->bssid,
           neighbor_element->bssid,
           sizeof(retry_neigh->bssid));

    client->btm_retry_neighbors_len++;
}

static void
bm_client_btm_reject_parse_neighbor_reports(bm_client_t *client, const uint8_t *ies, size_t ies_len)
{
    const struct element *elem;

    LOGI("%s parsing neighbor reports from client's btm response", client->mac_addr);

    /* Remove entries from retry neighbors */
    client->btm_retry_neighbors_len = 0;

    for_each_element_id(elem, WLAN_EID_NEIGHBOR_REPORT, ies, ies_len) {
        LOGI("Parsing retry neighbor from btm reject");
        bm_ie_parse_neighbor_report(client, (struct neighbor_report_element *)elem);
    }
}

static void
bm_action_frame_bss_trans_mgmt_resp(bm_client_t *client,
                                    const u8 *payload,
                                    size_t plen)
{
    u8 status_code;
    u8 bss_termination_delay;
    bsal_event_t event;

    if (plen < 3) {
        LOGI("%s btm resp short", client->mac_addr);
        return;
    }

    status_code = payload[1];
    bss_termination_delay = payload[2];

    LOGI("%s btm resp status %u bss_termination_delay %u",
         client->mac_addr, status_code, bss_termination_delay);

    switch (status_code) {
    case WNM_BSS_TM_ACCEPT:
        if (plen < ETH_ALEN + 3) {
            LOGI("%s not place for target bssid", client->mac_addr);
            break;
        }

        LOGI("%s btm resp bssid "PRI(os_macaddr_t), client->mac_addr,
             FMT(os_macaddr_pt, (os_macaddr_t*) &payload[3]));
        break;
    case WNM_BSS_TM_REJECT_NO_SUITABLE_CANDIDATES:
        LOGI("%s btm resp no suitable candidates, cancel btm", client->mac_addr);
        bm_kick_cancel_btm_retry_task(client);
        break;
    case WNM_BSS_TM_REJECT_STA_CANDIDATE_LIST_PROVIDED:
        LOGI("%s btm resp reject, candidate list provided", client->mac_addr);
        bm_client_btm_reject_parse_neighbor_reports(client, &payload[3], plen-3);
        break;
    default:
        break;
    }

    if (!strlen(client->ifname))
        return;

    memset(&event, 0, sizeof(event));
    STRSCPY(event.ifname, client->ifname);
    event.type = BSAL_EVENT_BTM_STATUS;
    event.data.btm_status.status = status_code;
    bm_stats_add_event_to_report(client, &event, CLIENT_BTM_STATUS, false);
}

static void
bm_action_frame_bss_trans_mgmt_query(bm_client_t *client,
                                     const u8 *payload,
                                     size_t plen)
{
    bsal_btm_params_t btm_params;

    if (!client->connected) {
        LOGI("Client '%s': Not marked as connected. Can't process BTM yet", client->mac_addr);
        return;
    }

    memset(&btm_params, 0, sizeof(btm_params));
    btm_params.neigh = CALLOC(client->btm_max_neighbors, sizeof(*btm_params.neigh));
    btm_params.abridged = BTM_DEFAULT_ABRIDGED;
    btm_params.pref = BTM_DEFAULT_PREF;
    btm_params.valid_int = BTM_DEFAULT_VALID_INT;

    btm_params.inc_neigh = true;
    btm_params.inc_self = true;

    if (!bm_neighbor_build_btm_neighbor_list(client, &btm_params)) {
        LOGI("Client '%s': Unable to build neighbor list (query)", client->mac_addr);
        goto out;
    }

    if (target_bsal_bss_tm_request(client->ifname, client->macaddr.addr, &btm_params ) < 0) {
        LOGE("BSS Transition Request (query) failed for client %s", client->mac_addr);
    }

out:
    FREE(btm_params.neigh);
}

static void
bm_action_frame_wnm(bm_client_t *client,
                    const struct ieee80211_mgmt *mgmt,
                    size_t len)
{
    u8 action;
    const u8 *payload;
    size_t plen;

    if (len < IEEE80211_HDRLEN + 2)
        return;

    payload = ((const u8 *) mgmt) + IEEE80211_HDRLEN + 1;
    action = *payload++;
    plen = len - IEEE80211_HDRLEN - 2;

    switch (action) {
    case WNM_BSS_TRANS_MGMT_QUERY:
        LOGI("%s WNM_BSS_TRANS_MGMT_QUERY", client->mac_addr);
        bm_action_frame_bss_trans_mgmt_query(client, payload, plen);
        break;
    case WNM_BSS_TRANS_MGMT_RESP:
        LOGI("%s WNM_BSS_TRANS_MGMT_RESP", client->mac_addr);
        bm_action_frame_bss_trans_mgmt_resp(client, payload, plen);
        break;
    default:
        break;
    }
}

void
bm_event_action_frame(const char *ifname,
                      const uint8_t *data,
                      unsigned int data_len)
{
    bm_client_t *client;
    const struct ieee80211_mgmt *mgmt;

    if (data_len < sizeof(struct ieee80211_hdr)) {
        LOGI("%s: data_len %d less than ieee80211_hdr", ifname, data_len);
        return;
    }

    mgmt = (const struct ieee80211_mgmt *) data;
    LOGT("[%s]: " PRI(os_macaddr_t) " mgmt len=%u", ifname, FMT(os_macaddr_t, *(os_macaddr_t *)mgmt->sa), data_len);
    if (!(client = bm_client_find_by_macaddr(*(os_macaddr_t *)mgmt->sa))) {
        return;
    }

    LOGI("[%s]: %s BSAL_EVENT_ACTION_FRAME %d length %d", ifname, client->mac_addr, mgmt->u.action.category, data_len);
    switch (mgmt->u.action.category) {
    case WLAN_ACTION_RADIO_MEASUREMENT:
        bm_action_frame_rrm(client, mgmt, data_len);
        break;
    case WLAN_ACTION_WNM:
        bm_action_frame_wnm(client, mgmt, data_len);
        break;
    default:
        break;
    }
}

static void
bm_ie_supported_channels(bm_client_t *client, const uint8_t *ie, size_t len)
{
    const struct supp_chan *chan;
    unsigned int step;
    uint8_t channel;
    unsigned int j;
    unsigned int i;

    for (i = 0; i < len; i+=sizeof(struct supp_chan)) {
        chan = (struct supp_chan *) &ie[i];
        step = chan->first <= 14 ? 1 : 4;

        for (j = 0; j < chan->range; j++) {
            channel = chan->first + step * j;
            LOGD("%s supported chan %u", client->mac_addr, channel);
        }
    }
}

static void
bm_ie_parse_supported_op_classes(bm_client_t *client, const uint8_t *ie, size_t len)
{
    unsigned int i;

    if (WARN_ON(len == 0))
        return;

    memset(&client->op_classes, 0, sizeof(client->op_classes));

    if (len > BM_CLIENT_MAX_OP_CLASSES) {
        LOGW("%s Size of operating classes more than expected (%zu): %d",
             client->mac_addr, len, BM_CLIENT_MAX_OP_CLASSES);
        client->op_classes.size = BM_CLIENT_MAX_OP_CLASSES;
    } else {
        client->op_classes.size = len;
    }

    for (i = 0; i < client->op_classes.size; i++) {
        client->op_classes.op_class[i] = ie[i];
    }
}

void
bm_client_parse_assoc_ies(bm_client_t *client, const uint8_t *ies, size_t ies_len)
{
    const struct element *elem;

    for_each_element(elem, ies, ies_len) {
        LOGD("%s ie %d len %d", client->mac_addr, elem->id, elem->datalen);
        switch (elem->id) {
            case WLAN_EID_SUPPORTED_CHANNELS:
                LOGD("WLAN_EID_SUPPORTED_CHANNELS");
                bm_ie_supported_channels(client, elem->data, elem->datalen);
                break;
            case WLAN_EID_SUPPORTED_OPERATING_CLASSES:
                LOGD("WLAN_EID_SUPPORTED_OPERATING_CLASSES");
                bm_ie_parse_supported_op_classes(client, elem->data, elem->datalen);
                break;
            default:
                break;
        }
    }
}
