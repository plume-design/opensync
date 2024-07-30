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

#include <inttypes.h>
#include <ovsdb_table.h>
#include <ovsdb_cache.h>
#include <ovsdb_sync.h>
#include <util.h>
#include <const.h>
#include <log.h>
#include <osw_types.h>
#include <ow_steer_bm.h>

struct ow_ovsdb_steer {
    ovsdb_table_t neighbor_table;
    ovsdb_table_t config_table;
    ovsdb_table_t client_table;
};

static struct ow_ovsdb_steer *g_steering = NULL;

static bool
ow_ovsdb_steer_ht_mode_to_bm_neighbor_enum(const char *cstr,
                                           enum ow_steer_bm_neighbor_ht_mode *enumeration)
{
    ASSERT(cstr != NULL, "");
    ASSERT(enumeration != NULL, "");

    if (strcmp(cstr, "HT20") == 0) {
        *enumeration = OW_STEER_BM_NEIGHBOR_HT20;
        return true;
    }
    else if (strcmp(cstr, "HT2040") == 0) {
        *enumeration = OW_STEER_BM_NEIGHBOR_HT2040;
        return true;
    }
    else if (strcmp(cstr, "HT40") == 0) {
        *enumeration = OW_STEER_BM_NEIGHBOR_HT40;
        return true;
    }
    else if (strcmp(cstr, "HT40+") == 0) {
        *enumeration = OW_STEER_BM_NEIGHBOR_HT40P;
        return true;
    }
    else if (strcmp(cstr, "HT40-") == 0) {
        *enumeration = OW_STEER_BM_NEIGHBOR_HT40M;
        return true;
    }
    else if (strcmp(cstr, "HT80") == 0) {
        *enumeration = OW_STEER_BM_NEIGHBOR_HT80;
        return true;
    }
    else if (strcmp(cstr, "HT160") == 0) {
        *enumeration = OW_STEER_BM_NEIGHBOR_HT160;
        return true;
    }
    else if (strcmp(cstr, "HT80+80") == 0) {
        *enumeration = OW_STEER_BM_NEIGHBOR_HT80P80;
        return true;
    }
    else if (strcmp(cstr, "HT320") == 0) {
        *enumeration = OW_STEER_BM_NEIGHBOR_HT320;
        return true;
    }
    else {
        return false;
    }
}

static void
ow_ovsdb_steer_group_set(const struct schema_Band_Steering_Config *row)
{
    ASSERT(row != NULL, "");

    /* Either (if_name_2g, if_name_5g) or ifnames can exists */
    if (((row->ifnames_len > 0) && (row->if_name_2g_exists == true || row->if_name_5g_exists == true)) ||
        ((row->ifnames_len == 0) && row->if_name_2g_exists == false && row->if_name_5g_exists == false))
    {
        LOGW("ow: steer: ovsdb: group uuid: %s cannot set group, invalid ifnames", row->_uuid.uuid);
        return;
    }

    struct ow_steer_bm_group *group = ow_steer_bm_get_group(row->_uuid.uuid);

    if (row->if_name_2g_exists == true) {
        ow_steer_bm_group_get_vif(group, row->if_name_2g);
    }
    if (row->if_name_5g_exists == true) {
        ow_steer_bm_group_get_vif(group, row->if_name_5g);
    }
    if (row->ifnames_len > 0) {
        int i;
        for (i = 0; i < row->ifnames_len; i++)
            ow_steer_bm_group_get_vif(group, row->ifnames_keys[i]);
    }
}

static void
ow_ovsdb_steer_group_unset(const struct schema_Band_Steering_Config *row)
{
    ASSERT(row != NULL, "");

    struct ow_steer_bm_group *group = ow_steer_bm_get_group(row->_uuid.uuid);
    ow_steer_bm_group_unset(group);
}

static void
ow_ovsdb_steer_neighbor_set(const struct schema_Wifi_VIF_Neighbors *row)
{
    ASSERT(row != NULL, "");

    if (row->bssid_exists == false) {
        LOGW("ow: steer: ovsdb: cannot set neighbor, bssid is missing");
        return;
    }

    struct osw_hwaddr bssid;
    const bool addr_is_valid = osw_hwaddr_from_cstr(row->bssid, &bssid) == true;
    if (addr_is_valid == false) {
        LOGW("ow: steer: ovsdb: neighbor: bssid: %s invalid value", row->bssid);
        return;
    }

    struct ow_steer_bm_neighbor *neighbor = ow_steer_bm_get_neighbor(bssid.octet);

    if (row->if_name_exists == true) {
        const char *vif_name = row->if_name;
        ow_steer_bm_neighbor_set_vif_name(neighbor, vif_name);
    }
    else {
        ow_steer_bm_neighbor_set_vif_name(neighbor, NULL);
    }

    if (row->priority_exists == true) {
        const unsigned int priority = row->priority;
        ow_steer_bm_neighbor_set_priority(neighbor, &priority);
    }
    else {
        ow_steer_bm_neighbor_set_priority(neighbor, NULL);
    }

    if (row->mld_addr_exists == true) {
        const char *mld_addr_str = row->mld_addr;
        struct osw_hwaddr mld_addr;
        const bool addr_is_valid = osw_hwaddr_from_cstr(mld_addr_str, &mld_addr);
        if (addr_is_valid) {
            ow_steer_bm_neighbor_set_mld_addr(neighbor, &mld_addr);
        }
        else {
            ow_steer_bm_neighbor_set_mld_addr(neighbor, NULL);
        }
    }
    else {
        ow_steer_bm_neighbor_set_mld_addr(neighbor, NULL);
    }

    if (row->channel_exists == true) {
        const uint8_t channel = row->channel;
        ow_steer_bm_neighbor_set_channel_number(neighbor, &channel);
    }
    else {
        ow_steer_bm_neighbor_set_channel_number(neighbor, NULL);
    }

    if (row->center_freq0_chan_exists == true) {
        const uint8_t center_freq0_chan = row->center_freq0_chan;
        ow_steer_bm_neighbor_set_center_freq0_chan_number(neighbor, &center_freq0_chan);
    }
    else {
        ow_steer_bm_neighbor_set_center_freq0_chan_number(neighbor, NULL);
    }

    if (row->op_class_exists == true) {
        const uint8_t op_class = row->op_class;
        ow_steer_bm_neighbor_set_op_class(neighbor, &op_class);
    }
    else {
        ow_steer_bm_neighbor_set_op_class(neighbor, NULL);
    }

    if (row->ht_mode_exists == true) {
        enum ow_steer_bm_neighbor_ht_mode ht_mode;
        const bool translated = ow_ovsdb_steer_ht_mode_to_bm_neighbor_enum(row->ht_mode, &ht_mode);
        if (translated == true) {
            ow_steer_bm_neighbor_set_ht_mode(neighbor, &ht_mode);
        }
        else {
            LOGW("ow: steer: ovsdb: neighbor: bssid: "OSW_HWADDR_FMT" ht_mode: %s is invalid",
                 OSW_HWADDR_ARG(&bssid), row->ht_mode);
        }
    }
    else {
        ow_steer_bm_neighbor_set_ht_mode(neighbor, NULL);
    }
}

static void
ow_ovsdb_steer_neighbor_unset(const struct schema_Wifi_VIF_Neighbors *row)
{
    ASSERT(row != NULL, "");

    struct osw_hwaddr bssid;

    if (row->bssid_exists == false) {
        LOGW("ow: steer: ovsdb: cannot unset neighbor, bssid is missing");
        return;
    }

    const bool addr_is_valid = osw_hwaddr_from_cstr(row->bssid, &bssid) == true;
    if (addr_is_valid == false) {
        LOGW("ow: steer: ovsdb: bssid: %s invalid value", row->bssid);
        return;
    }

    struct ow_steer_bm_neighbor *neighbor = ow_steer_bm_get_neighbor(bssid.octet);
    ow_steer_bm_neighbor_unset(neighbor);
}

static void
ow_steer_bm_client_set_btm_params(const struct osw_hwaddr *sta_addr,
                                  struct ow_steer_bm_btm_params *btm_params,
                                  const char *btm_params_name,
                                  const char keys[][64],
                                  const char values[][32 + 1],
                                  int len)
{
    ASSERT(sta_addr != NULL, "");
    ASSERT(btm_params != NULL, "");
    ASSERT(btm_params_name != NULL, "");
    ASSERT(len > 0, "");

    int i;
    for (i = 0; i < len; i++) {
        const char *key = keys[i];
        const char *value = values[i];

        if (strcmp(key, "bssid") == 0) {
            struct osw_hwaddr bssid;
            const bool parsed = osw_hwaddr_from_cstr(value, &bssid);
            if (parsed == true) {
                ow_steer_bm_btm_params_set_bssid(btm_params, &bssid);
                continue;
            }
        }
        else if (strcmp(key, "disassoc_imminent") == 0) {
            const bool b = (atoi(value) == 0)
                         ? false
                         : true;
            ow_steer_bm_btm_params_set_disassoc_imminent(btm_params, &b);
            continue;
        }
        else {
            LOGD("ow: steer: ovsdb: client: sta_addr: "OSW_HWADDR_FMT" btm_params: %s key: %s is not supported",
                 OSW_HWADDR_ARG(sta_addr), btm_params_name, keys[i]);
             continue;
        }

        LOGW("ow: steer: ovsdb: client: sta_addr: "OSW_HWADDR_FMT" btm_params: %s key: %s value: %s is invalid",
             OSW_HWADDR_ARG(sta_addr), btm_params_name, key, value);
    }
}

static void
ow_steer_bm_client_set_cs_params(const struct osw_hwaddr *sta_addr,
                                 struct ow_steer_bm_cs_params *cs_params,
                                 const char keys[][64],
                                 const char values[][32 + 1],
                                 int len)
{
    ASSERT(sta_addr != NULL, "");
    ASSERT(cs_params != NULL, "");
    ASSERT(len > 0, "");

    int i;
    for (i = 0; i < len; i++) {
        const char *key = keys[i];
        const char *value = values[i];

        if (strcmp(key, "band") == 0) {
            if (strcmp(value, "2.4G") == 0) {
                const enum ow_steer_bm_cs_params_band band = OW_STEER_BM_CLIENT_CS_PARAMS_BAND_2G;
                ow_steer_bm_cs_params_set_band(cs_params, &band);
                continue;
            }
            else if (strcmp(value, "5G") == 0) {
                const enum ow_steer_bm_cs_params_band band = OW_STEER_BM_CLIENT_CS_PARAMS_BAND_5G;
                ow_steer_bm_cs_params_set_band(cs_params, &band);
                continue;
            }
            else if (strcmp(value, "5GL") == 0) {
                const enum ow_steer_bm_cs_params_band band = OW_STEER_BM_CLIENT_CS_PARAMS_BAND_5GL;
                ow_steer_bm_cs_params_set_band(cs_params, &band);
                continue;
            }
            else if (strcmp(value, "5GU") == 0) {
                const enum ow_steer_bm_cs_params_band band = OW_STEER_BM_CLIENT_CS_PARAMS_BAND_5GU;
                ow_steer_bm_cs_params_set_band(cs_params, &band);
                continue;
            }
            else if (strcmp(value, "6G") == 0) {
                const enum ow_steer_bm_cs_params_band band = OW_STEER_BM_CLIENT_CS_PARAMS_BAND_6G;
                ow_steer_bm_cs_params_set_band(cs_params, &band);
                continue;
            }
            else {
                LOGW("ow: steer: ovsdb: cs_params: sta_addr: "OSW_HWADDR_FMT" key: %s has invalid value: %s",
                     OSW_HWADDR_ARG(sta_addr), key, value);
                continue;
            }
        }
        else if (strcmp(key, "cs_enforce_period") == 0) {
            unsigned int enforce_period;
            const bool parsed = sscanf(value, "%u", &enforce_period) == 1;
            if (parsed == true) {
                ow_steer_bm_cs_params_set_enforce_period(cs_params, &enforce_period);
                continue;
            }
        }
        else {
            LOGD("ow: steer: ovsdb: cs_params: sta_addr: "OSW_HWADDR_FMT" key: %s is not supported",
                 OSW_HWADDR_ARG(sta_addr), key);
            continue;
        }

        LOGW("ow: steer: ovsdb: cs_params: sta_addr: "OSW_HWADDR_FMT" key: %s value: %s are invalid",
             OSW_HWADDR_ARG(sta_addr), key, value);
    }
}

static void
ow_ovsdb_steer_bm_client_set_cs_state_mutate_cb(const struct osw_hwaddr *client_addr,
                                                const enum ow_steer_bm_client_cs_state cs_state)
{
    ASSERT(client_addr != NULL, "");

    const char *cs_state_cstr = ow_steer_bm_client_cs_state_to_cstr(cs_state);
    if (cs_state_cstr == NULL) {
        LOGW("ow: steer: ovsdb: client: sta_addr: "OSW_HWADDR_FMT" cannot set cs_state, unknown enum value: %d", OSW_HWADDR_ARG(client_addr), cs_state);
        return;
    }

    struct schema_Band_Steering_Clients update;
    MEMZERO(update);

    struct osw_hwaddr_str client_addr_str;
    osw_hwaddr2str(client_addr, &client_addr_str);

    update._partial_update = true;
    SCHEMA_SET_STR(update.mac, client_addr_str.buf);
    if (strlen(cs_state_cstr) > 0) {
        SCHEMA_SET_STR(update.cs_state, cs_state_cstr);
    }
    else {
        SCHEMA_UNSET_FIELD(update.cs_state);
    }
    ovsdb_table_update(&g_steering->client_table, &update);

    LOGD("ow: steer: ovsdb: client: sta_addr: "OSW_HWADDR_FMT" set cs_state: %s", OSW_HWADDR_ARG(client_addr), cs_state_cstr);
}

enum ow_steer_bm_client_cs_mode
ow_ovsdb_steer_client_cs_mode_from_cstr(const char *cstr)
{
    if (strcmp(cstr, "off") == 0) return OW_STEER_BM_CLIENT_CS_MODE_OFF;
    if (strcmp(cstr, "home") == 0) return OW_STEER_BM_CLIENT_CS_MODE_HOME;
    if (strcmp(cstr, "away") == 0) return OW_STEER_BM_CLIENT_CS_MODE_AWAY;
    return OW_STEER_BM_CLIENT_CS_MODE_OFF;
}

enum ow_steer_bm_client_cs_state
ow_steer_bm_client_cs_state_from_cstr(const char *cstr)
{
    if (cstr == NULL) return OW_STEER_BM_CS_STATE_UNKNOWN;
    if (strcmp(cstr, "") == 0) return OW_STEER_BM_CS_STATE_INIT;
    if (strcmp(cstr, "none") == 0) return OW_STEER_BM_CS_STATE_NONE;
    if (strcmp(cstr, "steering") == 0) return OW_STEER_BM_CS_STATE_STEERING;
    if (strcmp(cstr, "expired") == 0) return OW_STEER_BM_CS_STATE_EXPIRED;
    if (strcmp(cstr, "failed") == 0) return OW_STEER_BM_CS_STATE_FAILED;
    return OW_STEER_BM_CS_STATE_UNKNOWN;
}

static void
ow_ovsdb_steer_client_set(const struct schema_Band_Steering_Clients *row)
{
    ASSERT(row != NULL, "");

    if (row->mac_exists == false) {
        LOGW("ow: steer: ovsdb: cannot set client, mac is missing");
        return;
    }

    struct osw_hwaddr addr;
    const bool result = osw_hwaddr_from_cstr(row->mac, &addr);
    if (result == false) {
        LOGW("ow: steer: ovsdb: client: addr: %s cannot set client, invalid sta addr", row->mac);
        return;
    }

    struct ow_steer_bm_client *client = ow_steer_bm_get_client(addr.octet);

    if (row->hwm_exists == true) {
        const unsigned int hwm = row->hwm;
        ow_steer_bm_client_set_hwm(client, &hwm);
    }
    else {
        ow_steer_bm_client_set_hwm(client, NULL);
    }

    if (row->lwm_exists == true) {
        const unsigned int lwm = row->lwm;
        ow_steer_bm_client_set_lwm(client, &lwm);
    }
    else {
        ow_steer_bm_client_set_lwm(client, NULL);
    }

    if (row->pre_assoc_auth_block_exists == true) {
        const bool auth_block = row->pre_assoc_auth_block;
        ow_steer_bm_client_set_pre_assoc_auth_block(client, &auth_block);
    }
    else {
        ow_steer_bm_client_set_pre_assoc_auth_block(client, NULL);
    }

    if (row->pref_5g_exists == true) {
        if (strcmp(row->pref_5g, "always") == 0) {
             const enum ow_steer_bm_client_pref_5g pref_5g = OW_STEER_BM_CLIENT_PREF_5G_ALWAYS;
             ow_steer_bm_client_set_pref_5g(client, &pref_5g);
        }
        else if (strcmp(row->pref_5g, "never") == 0) {
             const enum ow_steer_bm_client_pref_5g pref_5g = OW_STEER_BM_CLIENT_PREF_5G_NEVER;
             ow_steer_bm_client_set_pref_5g(client, &pref_5g);
        }
        else if (strcmp(row->pref_5g, "hwm") == 0) {
             const enum ow_steer_bm_client_pref_5g pref_5g = OW_STEER_BM_CLIENT_PREF_5G_HWM;
             ow_steer_bm_client_set_pref_5g(client, &pref_5g);
        }
        else if (strcmp(row->pref_5g, "nonDFS") == 0) {
             const enum ow_steer_bm_client_pref_5g pref_5g = OW_STEER_BM_CLIENT_PREF_5G_NON_DFS;
             ow_steer_bm_client_set_pref_5g(client, &pref_5g);
        }
        else {
            LOGW("ow: steer: ovsdb: client: sta_addr: "OSW_HWADDR_FMT" cannot add, unsupported pref_5g: %s",
                 OSW_HWADDR_ARG(&addr), row->pref_5g);
            ow_steer_bm_client_set_pref_5g(client, NULL);
        }
    }
    else {
        ow_steer_bm_client_set_pref_5g(client, NULL);
    }

    if (row->kick_type_exists == true) {
        if (strcmp(row->kick_type, "deauth") == 0) {
             const enum ow_steer_bm_client_kick_type kick_type = OW_STEER_BM_CLIENT_KICK_TYPE_DEAUTH;
             ow_steer_bm_client_set_kick_type(client, &kick_type);
        }
        else if (strcmp(row->kick_type, "btm_deauth") == 0) {
             const enum ow_steer_bm_client_kick_type kick_type = OW_STEER_BM_CLIENT_KICK_TYPE_BTM_DEAUTH;
             ow_steer_bm_client_set_kick_type(client, &kick_type);
        }
        else if (strcmp(row->kick_type, "none") == 0) {
             ow_steer_bm_client_set_kick_type(client, NULL);
        }
        else {
            LOGW("ow: steer: ovsdb: client: sta_addr: "OSW_HWADDR_FMT" cannot add, unsupported kick_type: %s",
                 OSW_HWADDR_ARG(&addr), row->kick_type);
            ow_steer_bm_client_set_kick_type(client, NULL);
        }
    }
    else {
        ow_steer_bm_client_set_kick_type(client, NULL);
    }

    if (row->backoff_secs_exists == true) {
        const unsigned int backoff_secs = row->backoff_secs;
        ow_steer_bm_client_set_backoff_secs(client, &backoff_secs);
    }
    else {
        ow_steer_bm_client_set_backoff_secs(client, NULL);
    }

    if (row->backoff_exp_base_exists == true) {
        const unsigned int backoff_exp_base = row->backoff_exp_base;
        ow_steer_bm_client_set_backoff_exp_base(client, &backoff_exp_base);
    }
    else {
        ow_steer_bm_client_set_backoff_exp_base(client, NULL);
    }

    if (row->max_rejects_exists == true) {
        const unsigned int max_rejects = row->max_rejects;
        ow_steer_bm_client_set_max_rejects(client, &max_rejects);
    }
    else {
        ow_steer_bm_client_set_max_rejects(client, NULL);
    }

    if (row->rejects_tmout_secs_exists == true) {
        const unsigned int rejects_tmout_secs = row->rejects_tmout_secs;
        ow_steer_bm_client_set_rejects_tmout_secs(client, &rejects_tmout_secs);
    }
    else {
        ow_steer_bm_client_set_rejects_tmout_secs(client, NULL);
    }

    const enum ow_steer_bm_client_cs_state cs_state = ow_steer_bm_client_cs_state_from_cstr(row->cs_state);
    const enum ow_steer_bm_client_cs_mode cs_mode = ow_ovsdb_steer_client_cs_mode_from_cstr(row->cs_mode);
    const enum ow_steer_bm_client_cs_mode cs_mode_off = OW_STEER_BM_CLIENT_CS_MODE_OFF;

    switch (cs_state) {
        case OW_STEER_BM_CS_STATE_STEERING:
            /* There's a chance this is leftovers from a
             * previous process life (before it crashed). In
             * that case it'd be more of a problem to _not_
             * respect it, even if this means resulting
             * enforce period, was to end up being longer
             * than originally intended.
             */
             /* fallthrough */
        case OW_STEER_BM_CS_STATE_UNKNOWN:
        case OW_STEER_BM_CS_STATE_INIT:
            ow_steer_bm_client_set_cs_mode(client, &cs_mode);
            break;
        case OW_STEER_BM_CS_STATE_NONE:
        case OW_STEER_BM_CS_STATE_EXPIRED:
        case OW_STEER_BM_CS_STATE_FAILED:
            /* The controller is expected to poke cs_mode to
             * "none" or "[]". Otherwise it needs to be
             * assumed the work related with this was
             * already done, possibly by a previous process
             * life (before crash).
             */
            ow_steer_bm_client_set_cs_mode(client, &cs_mode_off);
            break;
    }

    if (row->sc_btm_params_len != 0) {
        struct ow_steer_bm_btm_params *sc_btm_params = ow_steer_bm_client_get_sc_btm_params(client);
        ow_steer_bm_client_set_btm_params(&addr, sc_btm_params, "sc_btm_params", row->sc_btm_params_keys,
                                          row->sc_btm_params, row->sc_btm_params_len);
    }
    else {
        ow_steer_bm_client_unset_sc_btm_params(client);
    }

    if (row->steering_btm_params_len != 0) {
        struct ow_steer_bm_btm_params *steering_btm_params = ow_steer_bm_client_get_steering_btm_params(client);
        ow_steer_bm_client_set_btm_params(&addr, steering_btm_params, "steering_btm_params", row->steering_btm_params_keys,
                                          row->steering_btm_params, row->steering_btm_params_len);
    }
    else {
        ow_steer_bm_client_unset_steering_btm_params(client);
    }

    if (row->sticky_btm_params_len != 0) {
        struct ow_steer_bm_btm_params *sticky_btm_params = ow_steer_bm_client_get_sticky_btm_params(client);
        ow_steer_bm_client_set_btm_params(&addr, sticky_btm_params, "sticky_btm_params", row->sticky_btm_params_keys,
                                          row->sticky_btm_params, row->sticky_btm_params_len);
    }
    else {
        ow_steer_bm_client_unset_sticky_btm_params(client);
    }

    if (row->sc_kick_type_exists == true) {
        if (strcmp(row->sc_kick_type, "deauth") == 0) {
             const enum ow_steer_bm_client_sc_kick_type sc_kick_type = OW_STEER_BM_CLIENT_SC_KICK_TYPE_DEAUTH;
             ow_steer_bm_client_set_sc_kick_type(client, &sc_kick_type);
        }
        else if (strcmp(row->kick_type, "btm_deauth") == 0) {
             const enum ow_steer_bm_client_sc_kick_type sc_kick_type = OW_STEER_BM_CLIENT_SC_KICK_TYPE_BTM_DEAUTH;
             ow_steer_bm_client_set_sc_kick_type(client, &sc_kick_type);
        }
        else {
            LOGW("ow: steer: ovsdb: client: sta_addr: "OSW_HWADDR_FMT" cannot add, unsupported sc_kick_type: %s",
                 OSW_HWADDR_ARG(&addr), row->sc_kick_type);
            ow_steer_bm_client_set_sc_kick_type(client, NULL);
        }
    }
    else {
        ow_steer_bm_client_set_sc_kick_type(client, NULL);
    }

    if (row->sticky_kick_type_exists == true) {
        if (strcmp(row->sticky_kick_type, "deauth") == 0) {
             const enum ow_steer_bm_client_sticky_kick_type sticky_kick_type = OW_STEER_BM_CLIENT_STICKY_KICK_TYPE_DEAUTH;
             ow_steer_bm_client_set_sticky_kick_type(client, &sticky_kick_type);
        }
        else if (strcmp(row->kick_type, "btm_deauth") == 0) {
             const enum ow_steer_bm_client_sticky_kick_type sticky_kick_type = OW_STEER_BM_CLIENT_STICKY_KICK_TYPE_BTM_DEAUTH;
             ow_steer_bm_client_set_sticky_kick_type(client, &sticky_kick_type);
        }
        else {
            LOGW("ow: steer: ovsdb: client: sta_addr: "OSW_HWADDR_FMT" unsupported sticky_kick_type: %s",
                 OSW_HWADDR_ARG(&addr), row->sticky_kick_type);
            ow_steer_bm_client_set_sticky_kick_type(client, NULL);
        }
    }
    else {
        ow_steer_bm_client_set_sticky_kick_type(client, NULL);
    }

    if (row->neighbor_list_filter_by_beacon_report_exists == true) {
        const bool value = row->neighbor_list_filter_by_beacon_report;
        ow_steer_bm_client_set_neighbor_list_filter_by_beacon_report(client, &value);
    }
    else {
        ow_steer_bm_client_set_neighbor_list_filter_by_beacon_report(client, NULL);
    }

    if (row->pref_5g_pre_assoc_block_timeout_msecs_exists == true) {
        const unsigned int value = row->pref_5g_pre_assoc_block_timeout_msecs;
        ow_steer_bm_client_set_pref_5g_pre_assoc_block_timeout_msecs(client, &value);
    }
    else {
        ow_steer_bm_client_set_pref_5g_pre_assoc_block_timeout_msecs(client, NULL);
    }

    if (row->force_kick_exists == true) {
        if (strcmp(row->force_kick, "none") == 0) {
            ow_steer_bm_client_set_force_kick(client, NULL);
        }
        else if (strcmp(row->force_kick, "speculative") == 0) {
            const enum ow_steer_bm_client_force_kick force_kick = OW_STEER_BM_CLIENT_FORCE_KICK_SPECULATIVE;
            ow_steer_bm_client_set_force_kick(client, &force_kick);
        }
        else if (strcmp(row->force_kick, "directed") == 0) {
            const enum ow_steer_bm_client_force_kick force_kick = OW_STEER_BM_CLIENT_FORCE_KICK_DIRECTED;
            ow_steer_bm_client_set_force_kick(client, &force_kick);
        }
        else{
            LOGW("ow: steer: ovsdb: client: sta_addr: "OSW_HWADDR_FMT" unsupported force_kick: %s",
                 OSW_HWADDR_ARG(&addr), row->force_kick);
            ow_steer_bm_client_set_force_kick(client, NULL);
        }
    }
    else {
        ow_steer_bm_client_set_force_kick(client, NULL);
    }

    if (row->kick_upon_idle_exists == true) {
        const bool kick_upon_idle = row->kick_upon_idle;
        ow_steer_bm_client_set_kick_upon_idle(client, &kick_upon_idle);
    }
    else {
        ow_steer_bm_client_set_kick_upon_idle(client, NULL);
    }

    if (row->cs_params_len > 0) {
        struct ow_steer_bm_cs_params *cs_params = ow_steer_bm_client_get_cs_params(client);
        ow_steer_bm_client_set_cs_params(&addr, cs_params, row->cs_params_keys, row->cs_params, row->cs_params_len);
    }
    else {
        ow_steer_bm_client_unset_cs_params(client);
    }

    if (row->send_rrm_after_assoc_exists) {
        const bool enable = row->send_rrm_after_assoc;
        ow_steer_bm_client_set_send_rrm_after_assoc(client, &enable);
    }
    else {
        ow_steer_bm_client_set_send_rrm_after_assoc(client, NULL);
    }

    ow_steer_bm_client_set_cs_state_mutate_cb(client, ow_ovsdb_steer_bm_client_set_cs_state_mutate_cb);
}

static void
ow_ovsdb_steer_client_unset(const struct schema_Band_Steering_Clients *row)
{
    ASSERT(row != NULL, "");

    if (row->mac_exists == false) {
        LOGW("ow: steer: ovsdb: cannot unset client, mac is missing");
        return;
    }

    struct osw_hwaddr addr;
    const bool result = osw_hwaddr_from_cstr(row->mac, &addr);
    if (result == false) {
        LOGW("ow: steer: ovsdb: client: addr: %s cannot unset client, invalid sta addr", row->mac);
        return;
    }

    struct ow_steer_bm_client *client = ow_steer_bm_get_client(addr.octet);
    ow_steer_bm_client_unset(client);
}

static void
ow_ovsdb_steer_neighbor_table_cb(ovsdb_update_monitor_t *mon,
                                  struct schema_Wifi_VIF_Neighbors *old,
                                  struct schema_Wifi_VIF_Neighbors *rec,
                                  ovsdb_cache_row_t *row)
{
    const struct schema_Wifi_VIF_Neighbors *neighbor = (void *)row->record;

    switch (mon->mon_type) {
        case OVSDB_UPDATE_NEW:
            ow_ovsdb_steer_neighbor_set(neighbor);
            break;
        case OVSDB_UPDATE_MODIFY:
            ow_ovsdb_steer_neighbor_set(neighbor);
            break;
        case OVSDB_UPDATE_DEL:
            ow_ovsdb_steer_neighbor_unset(neighbor);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

static void
ow_ovsdb_steer_config_table_cb(ovsdb_update_monitor_t *mon,
                               struct schema_Band_Steering_Config *old,
                               struct schema_Band_Steering_Config *rec,
                               ovsdb_cache_row_t *row)
{
    const struct schema_Band_Steering_Config *group = (void *)row->record;

    switch (mon->mon_type) {
        case OVSDB_UPDATE_NEW:
            ow_ovsdb_steer_group_set(group);
            break;
        case OVSDB_UPDATE_MODIFY:
            ow_ovsdb_steer_group_set(group);
            break;
        case OVSDB_UPDATE_DEL:
            ow_ovsdb_steer_group_unset(group);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

static void
ow_ovsdb_steer_client_table_cb(ovsdb_update_monitor_t *mon,
                               struct schema_Band_Steering_Clients *old,
                               struct schema_Band_Steering_Clients *rec,
                               ovsdb_cache_row_t *row)
{
    const struct schema_Band_Steering_Clients *client = (void *)row->record;

    switch (mon->mon_type) {
        case OVSDB_UPDATE_NEW:
            ow_ovsdb_steer_client_set(client);
            break;
        case OVSDB_UPDATE_MODIFY:
            ow_ovsdb_steer_client_set(client);
            break;
        case OVSDB_UPDATE_DEL:
            ow_ovsdb_steer_client_unset(client);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

struct ow_ovsdb_steer*
ow_ovsdb_steer_create(void)
{
    ASSERT(g_steering == NULL, "");
    g_steering = CALLOC(1, sizeof(*g_steering));

    OVSDB_TABLE_VAR_INIT_NO_KEY(&g_steering->neighbor_table, Wifi_VIF_Neighbors);
    ovsdb_cache_monitor(&g_steering->neighbor_table, (void *)ow_ovsdb_steer_neighbor_table_cb, true);
    OVSDB_TABLE_VAR_INIT_NO_KEY(&g_steering->config_table, Band_Steering_Config);
    ovsdb_cache_monitor(&g_steering->config_table, (void *)ow_ovsdb_steer_config_table_cb, true);
    OVSDB_TABLE_VAR_INIT(&g_steering->client_table, Band_Steering_Clients, mac);
    ovsdb_cache_monitor(&g_steering->client_table, (void *)ow_ovsdb_steer_client_table_cb, true);

    LOGI("ow: steer: ovsdb: initialized");

    return g_steering;
}
