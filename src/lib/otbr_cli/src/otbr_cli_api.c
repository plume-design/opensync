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

#include "otbr_cli_api.h"
#include "const.h"
#include "log.h"
#include "osa_assert.h"
#include "otbr_cli.h"
#include "util.h"

#include "openthread/netdiag.h"
#include "ot_tlv.h"

bool otbr_cli_get_role(otDeviceRole *const role)
{
    static c_item_t role_map[] = {
        C_ITEM_STR(OT_DEVICE_ROLE_DISABLED, "disabled"),
        C_ITEM_STR(OT_DEVICE_ROLE_DETACHED, "detached"),
        C_ITEM_STR(OT_DEVICE_ROLE_CHILD, "child"),
        C_ITEM_STR(OT_DEVICE_ROLE_ROUTER, "router"),
        C_ITEM_STR(OT_DEVICE_ROLE_LEADER, "leader")};
    otbr_cli_response_t rsp = {0};
    bool success = false;

    /* "state" command gets the current role */
    if (otbr_cli_get("state", &rsp, 1, -1))
    {
        const c_item_t *const role_map_item = c_get_item_by_str(role_map, rsp.lines[0]);

        if (role_map_item != NULL)
        {
            *role = (int)role_map_item->key;
            success = true;
        }
        else
        {
            LOGE("Invalid state/role '%s'", rsp.lines[0]);
        }
    }
    otbr_cli_response_free(&rsp);

    return success;
}

const char *otbr_cli_get_multicast_address(const bool whole_network, const bool including_med, const bool including_sed)
{
    static char addr[C_IPV6ADDR_LEN] = "";

    addr[0] = '\0';

    /* Realm-local scope multicast address is built on the unicast Mesh-Local prefix */
    if (including_sed)
    {
        const char l_atn_type = whole_network ? 'r' : 'l';
        /* Link-Local (l) or Realm-Local (r) All Thread Nodes multicast address */
        return otbr_cli_get_string(otbr_cli_cmd("ipmaddr %clatn", l_atn_type), addr, sizeof(addr)) ? addr : NULL;
    }

    switch ((whole_network ? 0b10 : 0) | (including_med ? 0b01 : 0))
    {
        case 0b11:
            return "ff03::01"; /*< Mesh/Realm-Local all-nodes (FTDs and MEDs) */
        case 0b10:
            return "ff03::02"; /*< Mesh/Realm-Local all FTDs (routers, REEDs and FEDs) */
        case 0b01:
            /* Link-Local all-nodes (FTDs and MEDs) */
            return "ff02::01";
        default:
            /* Link-local all FTDs (routers, REEDs and FEDs) */
            return "ff02::02";
    }
}

static bool parse_network_diagnostic_peer_tlvs(
        const uint8_t *tlvs_raw,
        size_t tlvs_raw_len,
        struct otbr_network_diagnostic_peer_tlvs_s *const tlvs)
{
    const ot_tlv_t *raw_tlv;

    while ((raw_tlv = ot_tlv_get_next(&tlvs_raw, &tlvs_raw_len)) != NULL)
    {
        otNetworkDiagTlv tlv;

        if (ot_tlv_parse_network_diagnostic_tlv(raw_tlv, &tlv))
        {
            ARRAY_APPEND_COPY(tlvs->tlvs, tlvs->num_tlvs, tlv);
        }
        else
        {
            return false;
        }
    }

    /* When successful, all TLVs should be parsed */
    return (tlvs_raw_len == 0);
}

bool otbr_cli_get_network_diagnostic(
        const char *const address,
        const otNetworkDiagTlvType *const diag_types,
        const unsigned int num_diag_types,
        const float timeout,
        struct otbr_network_diagnostic_tlvs_s *const tlvs)
{
    /* networkdiagnostic get <addr> <type> ... */
    char types_str[OT_NETWORK_DIAGNOSTIC_TYPELIST_MAX_ENTRIES * 3 + 1];
    char *p_types_str = types_str;
    otbr_cli_response_t rsp = {0};

    ASSERT(num_diag_types <= OT_NETWORK_DIAGNOSTIC_TYPELIST_MAX_ENTRIES, "Too many diagnostic types");

    types_str[0] = '\0';
    for (unsigned int i = 0; i < num_diag_types; i++)
    {
        p_types_str += snprintf(p_types_str, sizeof(types_str) - (p_types_str - types_str), " %d", diag_types[i]);
    }

    if (!otbr_cli_get(otbr_cli_cmd("networkdiagnostic get %s %s", address, types_str), &rsp, 0, timeout))
    {
        otbr_cli_response_free(&rsp);
        return false;
    }

    for (size_t i_line = 0; i_line < rsp.count; i_line++)
    {
        /* "DIAG_GET.rsp/ans from %s: %s", IPv6 Peer Address, Diag TLVs HEX
         * ... parsed TLVs
         */
        const char *peer_addr = strstra(rsp.lines[i_line], "DIAG_GET.rsp/ans from ");
        const char *diag_tlvs = strstra(peer_addr, ": ");
        uint8_t tlvs_raw[OT_NETWORK_BASE_TLV_MAX_LENGTH];
        ssize_t tlvs_raw_len;
        struct otbr_network_diagnostic_peer_tlvs_s peer_tlvs = {0};

        /* Ignore other lines, which are just parsed TLVs */
        if (diag_tlvs == NULL)
        {
            continue;
        }

        tlvs_raw_len = hex2bin(diag_tlvs, strlen(diag_tlvs), tlvs_raw, sizeof(tlvs_raw));
        if (tlvs_raw_len < 2)
        {
            LOGE("Failed to parse TLVs '%s'", diag_tlvs);
            continue;
        }

        if (!parse_network_diagnostic_peer_tlvs(tlvs_raw, (size_t)tlvs_raw_len, &peer_tlvs))
        {
            ARRAY_FREE(peer_tlvs.tlvs, peer_tlvs.num_tlvs);
            continue;
        }
        ARRAY_APPEND_COPY(tlvs->peers, tlvs->num_peers, peer_tlvs);
    }
    otbr_cli_response_free(&rsp);

    return true;
}

void otbr_cli_get_network_diagnostic_tlvs_free(struct otbr_network_diagnostic_tlvs_s *const tlvs)
{
    for (size_t i_peer = 0; i_peer < tlvs->num_peers; i_peer++)
    {
        ARRAY_FREE(tlvs->peers[i_peer].tlvs, tlvs->peers[i_peer].num_tlvs);
    }
    ARRAY_FREE(tlvs->peers, tlvs->num_peers);
}

bool otbr_cli_get_leader_data(otLeaderData *const leader_data)
{
    otbr_cli_response_t rsp = {0};
    bool success;

    /* > leaderdata
     * "Partition ID: %lu"
     * "Weighting: %u"
     * "Data Version: %u"
     * "Stable Data Version: %u"
     * "Leader Router ID: %u"
     */
    if (otbr_cli_get("leaderdata", &rsp, 5, -1))
    {
        success = strtonum(
                          strstra(rsp.lines[0], "Partition ID: "),
                          &leader_data->mPartitionId,
                          sizeof(leader_data->mPartitionId),
                          10)
                  && strtonum(
                          strstra(rsp.lines[1], "Weighting: "),
                          &leader_data->mWeighting,
                          sizeof(leader_data->mWeighting),
                          10)
                  && strtonum(
                          strstra(rsp.lines[2], "Data Version: "),
                          &leader_data->mDataVersion,
                          sizeof(leader_data->mDataVersion),
                          10)
                  && strtonum(
                          strstra(rsp.lines[3], "Stable Data Version: "),
                          &leader_data->mStableDataVersion,
                          sizeof(leader_data->mStableDataVersion),
                          10)
                  && strtonum(
                          strstra(rsp.lines[4], "Leader Router ID: "),
                          &leader_data->mLeaderRouterId,
                          sizeof(leader_data->mLeaderRouterId),
                          10);
    }
    else
    {
        success = false;
    }
    otbr_cli_response_free(&rsp);

    return success;
}
