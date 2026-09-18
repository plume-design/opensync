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

#include "os_tr181_types.h"
#include <jansson.h>
#include <memutil.h>
#include <ovsdb_sync.h>
#include <ox_log.h>
#include <ox_ovsdb_type.h>
#include <ox_route.h>
#include <ox_table_instance.h>
#include <ox_types.h>
#include <schema_consts.h>

#define OX_DM_WIFI_TR181_MFP_DISABLED "Disabled"
#define OX_DM_WIFI_TR181_MFP_OPTIONAL "Optional"
#define OX_DM_WIFI_TR181_MFP_REQUIRED "Required"

#define OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_20MHZ_STR    "20MHz"
#define OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_40MHZ_STR    "40MHz"
#define OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_80MHZ_STR    "80MHz"
#define OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_160MHZ_STR   "160MHz"
#define OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_320MHZ_1_STR "320MHz-1"
#define OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_320MHZ_2_STR "320MHz-2"
#define OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_UNKNOWN_STR  "Unknown"

#define OX_DM_WIFI_TR181_SECURITY_NONE                        "None"
#define OX_DM_WIFI_TR181_SECURITY_WPA_PERSONAL                "WPA-Personal"
#define OX_DM_WIFI_TR181_SECURITY_WPA2_PERSONAL               "WPA2-Personal"
#define OX_DM_WIFI_TR181_SECURITY_WPA3_PERSONAL               "WPA3-Personal"
#define OX_DM_WIFI_TR181_SECURITY_WPA_WPA2_PERSONAL           "WPA-WPA2-Personal"
#define OX_DM_WIFI_TR181_SECURITY_WPA3_PERSONAL_TRANSITION    "WPA3-Personal-Transition"
#define OX_DM_WIFI_TR181_SECURITY_WPA3_PERSONAL_COMPATIBILITY "WPA3-Personal-Compatibility"

static json_t *ox_dm_wifi_enabled_to_status_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *ctx)
{
    const char *status_up = "Up";
    const char *status_down = "Down";
    const char *status_unknown = "Unknown";

    // There's no way to map onto these from OVSDB alone.
    // const char *status_dormant = "Dormant";
    // const char *status_not_present = "NotPresent";
    // const char *status_lower_layer_down = "LowerLayerDown";
    // const char *status_error = "Error";

    const char *status = json_is_boolean(value_borrowed)
                                 ? (json_boolean_value(value_borrowed) ? status_up : status_down)
                                 : status_unknown;

    return json_string(status);
}

static json_t *ox_dm_wifi_vif_status_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *ctx)
{
    const char *status_enabled = "Enabled";
    const char *status_disabled = "Disabled";
    const char *status_unknown = "Error";

    const char *status = json_is_boolean(value_borrowed)
                                 ? (json_boolean_value(value_borrowed) ? status_enabled : status_disabled)
                                 : status_unknown;

    return json_string(status);
}

static json_t *ox_dm_wifi_radio_freq_band_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *ctx)
{
    const char *freq_band_2_4 = "2.4GHz";
    const char *freq_band_5 = "5GHz";
    const char *freq_band_6 = "6GHz";
    const char *freq_band_unknown = "";
    const char *freq_band =
            json_is_string(value_borrowed)
                    ? (strcmp(json_string_value(value_borrowed), SCHEMA_CONSTS_RADIO_TYPE_STR_2G) == 0 ? freq_band_2_4
                       : (strcmp(json_string_value(value_borrowed), SCHEMA_CONSTS_RADIO_TYPE_STR_5G) == 0
                          || strcmp(json_string_value(value_borrowed), SCHEMA_CONSTS_RADIO_TYPE_STR_5GL) == 0
                          || strcmp(json_string_value(value_borrowed), SCHEMA_CONSTS_RADIO_TYPE_STR_5GU) == 0)
                               ? freq_band_5
                       : (strcmp(json_string_value(value_borrowed), SCHEMA_CONSTS_RADIO_TYPE_STR_6G) == 0)
                               ? freq_band_6
                               : freq_band_unknown)
                    : freq_band_unknown;
    return json_string(freq_band);
}

static json_t *ox_dm_wifi_radio_freq_band_to_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *ctx)
{
    const char *value_str = json_string_value(value_borrowed) ?: "";
    const char *freq_band_2_4 = "2.4GHz";
    const char *freq_band_5 = "5GHz";
    const char *freq_band_6 = "6GHz";
    // FIXME: Can't differentiate between 5GL and 5GU. Low prio. We don't
    // expect Radios to be created. This is mostly for testing anyway.
    const char *freq_band = strcmp(value_str, freq_band_2_4) == 0 ? SCHEMA_CONSTS_RADIO_TYPE_STR_2G
                            : strcmp(value_str, freq_band_5) == 0 ? SCHEMA_CONSTS_RADIO_TYPE_STR_5G
                            : strcmp(value_str, freq_band_6) == 0 ? SCHEMA_CONSTS_RADIO_TYPE_STR_6G
                                                                  : "";
    return json_string(freq_band);
}

static json_t *ox_dm_wifi_radio_hw_mode_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *ctx)
{
    json_t *hw_mode_json_borrowed = json_array_get(value_borrowed, 0);
    json_t *freq_band_json_borrowed = json_array_get(value_borrowed, 1);

    const char *hw_mode_str = json_string_value(hw_mode_json_borrowed) ?: "";
    const char *freq_band_str = json_string_value(freq_band_json_borrowed) ?: "";

    const bool is_2_4ghz = strcmp(freq_band_str, SCHEMA_CONSTS_RADIO_TYPE_STR_2G) == 0;
    const bool is_5ghz = strcmp(freq_band_str, SCHEMA_CONSTS_RADIO_TYPE_STR_5G) == 0
                         || strcmp(freq_band_str, SCHEMA_CONSTS_RADIO_TYPE_STR_5GL) == 0
                         || strcmp(freq_band_str, SCHEMA_CONSTS_RADIO_TYPE_STR_5GU) == 0;
    const bool is_6ghz = strcmp(freq_band_str, SCHEMA_CONSTS_RADIO_TYPE_STR_6G) == 0;

    const bool is_11a = strcmp(hw_mode_str, "11a") == 0;
    const bool is_11b = strcmp(hw_mode_str, "11b") == 0;
    const bool is_11g = strcmp(hw_mode_str, "11g") == 0;
    const bool is_11n = strcmp(hw_mode_str, "11n") == 0;
    const bool is_11ac = strcmp(hw_mode_str, "11ac") == 0;
    const bool is_11ax = strcmp(hw_mode_str, "11ax") == 0;
    const bool is_11be = strcmp(hw_mode_str, "11be") == 0;

    char *csv = NULL;

    if (is_5ghz || is_6ghz)
    {
        if (is_11a || is_11n || is_11ac || is_11ax || is_11be)
        {
            strgrow(&csv, "11a,");
        }
        if (is_11n || is_11ac || is_11ax || is_11be)
        {
            strgrow(&csv, "11n,");
        }
        if (is_11ac || is_11ax || is_11be)
        {
            strgrow(&csv, "11ac,");
        }
        if (is_11ax || is_11be)
        {
            strgrow(&csv, "11ax,");
        }
        if (is_11be)
        {
            strgrow(&csv, "11be,");
        }
    }
    else if (is_2_4ghz)
    {
        if (is_11b || is_11g || is_11n || is_11ax)
        {
            strgrow(&csv, "11b,");
        }
        if (is_11g || is_11n || is_11ax)
        {
            strgrow(&csv, "11g,");
        }
        if (is_11n || is_11ax)
        {
            strgrow(&csv, "11n,");
        }
        if (is_11ax)
        {
            strgrow(&csv, "11ax,");
        }
        // FIXME: 11ac is technically not supported on 2.4GHz as far as 802.11
        // is concerned, but vendors have been marketing "11ac on 2.4GHz" for
        // years now to mean 11n with some vendor-specific extensions, so maybe
        // we should include it here as well for better compatibility with
        // existing vendor implementations?
    }

    strchomp(csv, ",");
    json_t *csv_json_owned = json_string(csv ?: "");
    FREE(csv);
    return csv_json_owned;
}

static json_t *ox_dm_wifi_radio_channel_list_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *ctx)
{
    json_t *set_borrowed = ox_ovsdb_type_borrow_set(value_borrowed);
    json_t *channel_borrowed;
    char *csv_channels_owned = NULL;
    size_t i;
    json_array_foreach(set_borrowed, i, channel_borrowed)
    {
        if (json_is_integer(channel_borrowed))
        {
            const int channel_num = json_integer_value(channel_borrowed);
            strgrow(&csv_channels_owned, "%d,", channel_num);
        }
    }
    strchomp(csv_channels_owned, ",");
    json_t *result_owned = json_string(csv_channels_owned ?: "");
    FREE(csv_channels_owned);
    return result_owned;
}

static const char *ox_dm_wifi_radio_oper_chan_width_to_str(int width_mhz, bool is_320_1, bool is_320_2)
{
    switch (width_mhz)
    {
        case 20:
            return OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_20MHZ_STR;
        case 40:
            return OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_40MHZ_STR;
        case 80:
            return OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_80MHZ_STR;
        case 160:
            return OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_160MHZ_STR;
        case 320:
            if (is_320_1)
            {
                return OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_320MHZ_1_STR;
            }
            else if (is_320_2)
            {
                return OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_320MHZ_2_STR;
            }
            else
            {
                /* When 320-1/320-2 can't be properly inferred default to
                 * 320-1. This is necessary to guarantee back-to-back
                 * (ovsdb->tr181->ovsdb) to be at least somewhat sane. This
                 * prevents self-removal of radio instances when they get
                 * inserted via ovsdb update notification.
                 */
                return OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_320MHZ_1_STR;
            }
    }
    return OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_UNKNOWN_STR;
}

static json_t *ox_dm_wifi_radio_oper_chan_width_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *ctx)
{
    json_t *ht_mode_json_borrowed = json_array_get(value_borrowed, 0);
    json_t *center_freq0_chan_json_borrowed = json_array_get(value_borrowed, 1);

    const char *ht_mode_str = json_string_value(ht_mode_json_borrowed) ?: "";
    if (strlen(ht_mode_str) == 0)
    {
        return NULL;
    }

    const int center_num =
            json_is_integer(center_freq0_chan_json_borrowed) ? json_integer_value(center_freq0_chan_json_borrowed) : 0;

    const int width_mhz = atoi(strpbrk(ht_mode_str, "0123456789") ?: "");
    const bool is_320_1 = unii_6g_is_320_1(center_num);
    const bool is_320_2 = unii_6g_is_320_2(center_num);
    const char *oper_chan_width = ox_dm_wifi_radio_oper_chan_width_to_str(width_mhz, is_320_1, is_320_2);
    return json_string(oper_chan_width);
}

static json_t *ox_dm_wifi_compute_center_freq(const ox_table_instance_t *instance)
{
    json_t *channel_borrowed = json_object_get(instance->stash, "Channel");
    json_t *bandwidth_borrowed = json_object_get(instance->stash, "OperatingChannelBandwidth");
    const int channel_num = json_is_integer(channel_borrowed) ? json_integer_value(channel_borrowed) : 0;
    const char *bandwidth_str = json_string_value(bandwidth_borrowed) ?: "";

    if (channel_borrowed == NULL) return NULL;
    if (bandwidth_borrowed == NULL) return NULL;
    if (channel_num == 0) return NULL;

    const bool is_320mhz_1 = strcmp(bandwidth_str, OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_320MHZ_1_STR) == 0;
    const bool is_320mhz_2 = strcmp(bandwidth_str, OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_320MHZ_2_STR) == 0;
    const int *channels1 = unii_6g_320_1_chan2list(channel_num);
    const int *channels2 = unii_6g_320_2_chan2list(channel_num);
    const int center1 = chanlist_to_center(channels1);
    const int center2 = chanlist_to_center(channels2);
    const int center = is_320mhz_1 ? center1 : (is_320mhz_2 ? center2 : 0);
    LOGD(LOG_PREFIX_INSTANCE(
                 instance,
                 "computed center frequency %d for channel %d with bandwidth '%s' (is_320mhz_1=%d, is_320mhz_2=%d)"),
         center,
         channel_num,
         bandwidth_str,
         is_320mhz_1,
         is_320mhz_2);
    return json_integer(center);
}

static json_t *ox_dm_wifi_radio_oper_chan_width_to_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    const char *value_str = json_string_value(value_borrowed) ?: "";
    const char *ht_mode_str =
            strcmp(value_str, OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_20MHZ_STR) == 0   ? SCHEMA_CONSTS_HT_MODE_20MHZ
            : strcmp(value_str, OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_40MHZ_STR) == 0 ? SCHEMA_CONSTS_HT_MODE_40MHZ
            : strcmp(value_str, OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_80MHZ_STR) == 0 ? SCHEMA_CONSTS_HT_MODE_80MHZ
            : strcmp(value_str, OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_160MHZ_STR) == 0
                    ? SCHEMA_CONSTS_HT_MODE_160MHZ
                    : (strcmp(value_str, OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_320MHZ_1_STR) == 0
                               ? SCHEMA_CONSTS_HT_MODE_320MHZ
                               : (strcmp(value_str, OX_DM_WIFI_RADIO_OPER_CHAN_WIDTH_320MHZ_2_STR) == 0
                                          ? SCHEMA_CONSTS_HT_MODE_320MHZ
                                          : NULL));
    const bool invalid_str = ht_mode_str == NULL && strlen(value_str) > 0;
    if (invalid_str)
    {
        LOGW(LOG_PREFIX_INSTANCE(table_instance, "unknown operating channel width '%s', cannot convert to ht_mode"),
             value_str);
        return NULL;
    }

    const bool is_320mhz = ht_mode_str && strcmp(ht_mode_str, SCHEMA_CONSTS_HT_MODE_320MHZ) == 0;
    json_t *ht_mode_owned = ht_mode_str ? json_string(ht_mode_str) : ox_ovsdb_type_set_with_array(json_array());
    json_t *center_freq0_chan_owned =
            is_320mhz ? ox_dm_wifi_compute_center_freq(table_instance) : ox_ovsdb_type_set_with_array(json_array());

    json_t *values_owned = json_object();
    json_object_set_new(values_owned, "ht_mode", ht_mode_owned);
    json_object_set_new(values_owned, "center_freq0_chan", center_freq0_chan_owned);
    return values_owned;
}

static const char *ox_dm_wifi_security_mode_infer(
        bool enabled,
        json_t *akms,
        bool wpa_pairwise_tkip,
        bool wpa_pairwise_ccmp,
        bool rsn_pairwise_tkip,
        bool rsn_pairwise_ccmp,
        bool rsn_pairwise_ccmp256,
        bool rsn_pairwise_gcmp,
        bool rsn_pairwise_gcmp256,
        const char *pmf,
        const char *rsno)
{
    if (enabled == false) return OX_DM_WIFI_TR181_SECURITY_NONE;

    size_t i;
    json_t *akm;
    bool psk = false;
    bool sae = false;
    json_array_foreach(akms, i, akm)
    {
        const char *akm_str = json_string_value(akm) ?: "";
        if (strcmp(akm_str, SCHEMA_CONSTS_KEY_WPA_PSK) == 0)
        {
            psk = true;
        }
        else if (strcmp(akm_str, SCHEMA_CONSTS_KEY_SAE) == 0)
        {
            sae = true;
        }
    }
    const bool rsno_wfa = strcmp(rsno, SCHEMA_CONSTS_RSNO_WPA3_COMPAT) == 0;
    const bool rsno_plume = strcmp(rsno, SCHEMA_CONSTS_RSNO_WPA3_COMPAT_TRANS) == 0;
    const bool rsno_any = rsno_wfa || rsno_plume;

    const bool pmf_required = strcmp(pmf, SCHEMA_CONSTS_SECURITY_PMF_REQUIRED) == 0;
    const bool pmf_optional = strcmp(pmf, SCHEMA_CONSTS_SECURITY_PMF_OPTIONAL) == 0;
    const bool pmf_any = pmf_required || pmf_optional;
    const bool wpa = wpa_pairwise_tkip || wpa_pairwise_ccmp;
    const bool rsn =
            rsn_pairwise_tkip || rsn_pairwise_ccmp || rsn_pairwise_ccmp256 || rsn_pairwise_gcmp || rsn_pairwise_gcmp256;
    const bool wpa1_personal = wpa && !rsn && psk;
    const bool wpa1_2_personal = wpa && rsn && (psk || sae);
    const bool wpa2_personal = !wpa && rsn && (psk || sae);
    const bool wpa3_personal = !wpa && rsn && !psk && sae && pmf_required;
    const bool wpa3_transition = rsn && psk && sae && pmf_any;
    const bool wpa3_compat = wpa3_transition && rsno_any;

    if (wpa3_compat)
    {
        return OX_DM_WIFI_TR181_SECURITY_WPA3_PERSONAL_COMPATIBILITY;
    }
    else if (wpa3_transition)
    {
        return OX_DM_WIFI_TR181_SECURITY_WPA3_PERSONAL_TRANSITION;
    }
    else if (wpa3_personal)
    {
        return OX_DM_WIFI_TR181_SECURITY_WPA3_PERSONAL;
    }
    else if (wpa2_personal)
    {
        return OX_DM_WIFI_TR181_SECURITY_WPA2_PERSONAL;
    }
    else if (wpa1_2_personal)
    {
        return OX_DM_WIFI_TR181_SECURITY_WPA_WPA2_PERSONAL;
    }
    else if (wpa1_personal)
    {
        return OX_DM_WIFI_TR181_SECURITY_WPA_PERSONAL;
    }
    else
    {
        return OX_DM_WIFI_TR181_SECURITY_NONE;
    }
}

static json_t *ox_dm_wifi_security_mode_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *ctx)
{
    json_t *wpa_json_borrowed = json_array_get(value_borrowed, 0);
    json_t *wpa_key_mgmt_json_borrowed = json_array_get(value_borrowed, 1);
    json_t *wpa_pairwise_tkip_json_borrowed = json_array_get(value_borrowed, 2);
    json_t *wpa_pairwise_ccmp_json_borrowed = json_array_get(value_borrowed, 3);
    json_t *rsn_pairwise_tkip_json_borrowed = json_array_get(value_borrowed, 4);
    json_t *rsn_pairwise_ccmp_json_borrowed = json_array_get(value_borrowed, 5);
    json_t *rsn_pairwise_ccmp256_json_borrowed = json_array_get(value_borrowed, 6);
    json_t *rsn_pairwise_gcmp_json_borrowed = json_array_get(value_borrowed, 7);
    json_t *rsn_pairwise_gcmp256_json_borrowed = json_array_get(value_borrowed, 8);
    json_t *pmf = json_array_get(value_borrowed, 9);
    json_t *rsno_json_borrowed = json_array_get(value_borrowed, 10);

    json_t *akms_owned = json_array();
    json_array_append(akms_owned, wpa_key_mgmt_json_borrowed);
    wpa_key_mgmt_json_borrowed = ox_ovsdb_type_borrow_set(wpa_key_mgmt_json_borrowed) ?: akms_owned;

    const char *security_mode_str = ox_dm_wifi_security_mode_infer(
            json_boolean_value(wpa_json_borrowed),
            wpa_key_mgmt_json_borrowed,
            json_boolean_value(wpa_pairwise_tkip_json_borrowed),
            json_boolean_value(wpa_pairwise_ccmp_json_borrowed),
            json_boolean_value(rsn_pairwise_tkip_json_borrowed),
            json_boolean_value(rsn_pairwise_ccmp_json_borrowed),
            json_boolean_value(rsn_pairwise_ccmp256_json_borrowed),
            json_boolean_value(rsn_pairwise_gcmp_json_borrowed),
            json_boolean_value(rsn_pairwise_gcmp256_json_borrowed),
            json_string_value(pmf) ?: "",
            json_string_value(rsno_json_borrowed) ?: "");
    json_decref(akms_owned);
    return json_string(security_mode_str);
}

static json_t *ox_dm_wifi_radio_channel_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *ctx)
{
    // .ovsdb_column = "channel,freq_band,ht_mode,center_freq0_chan",
    json_t *channel_num_json_borrowed = json_array_get(value_borrowed, 0);
    json_t *freq_band_json_borrowed = json_array_get(value_borrowed, 1);
    json_t *ht_mode_json_borrowed = json_array_get(value_borrowed, 2);
    json_t *center_freq0_chan_json_borrowed = json_array_get(value_borrowed, 3);

    if (!json_is_integer(channel_num_json_borrowed) || !json_is_string(freq_band_json_borrowed)
        || !json_is_string(ht_mode_json_borrowed))
    {
        return json_string("");
    }

    const int channel_num = json_integer_value(channel_num_json_borrowed);
    const char *freq_band_str = json_string_value(freq_band_json_borrowed);
    const char *ht_mode_str = json_string_value(ht_mode_json_borrowed);
    const int width_mhz = atoi(strpbrk(ht_mode_str, "0123456789") ?: "");

    const bool is_2_4 = strcmp(freq_band_str, SCHEMA_CONSTS_RADIO_TYPE_STR_2G) == 0;
    const bool is_5 = strcmp(freq_band_str, SCHEMA_CONSTS_RADIO_TYPE_STR_5G) == 0
                      || strcmp(freq_band_str, SCHEMA_CONSTS_RADIO_TYPE_STR_5GL) == 0
                      || strcmp(freq_band_str, SCHEMA_CONSTS_RADIO_TYPE_STR_5GU) == 0;
    const bool is_6 = strcmp(freq_band_str, SCHEMA_CONSTS_RADIO_TYPE_STR_6G) == 0;

    char *csv_channels_owned = NULL;
    if (is_2_4)
    {
        if (width_mhz == 20)
        {
            strgrow(&csv_channels_owned, "%d", channel_num);
        }
        else if (width_mhz == 40)
        {
            const int center_chan =
                    (json_is_integer(center_freq0_chan_json_borrowed)
                             ? json_integer_value(center_freq0_chan_json_borrowed)
                             : 0);
            const bool center_after_primary = (center_chan > channel_num);
            if (center_after_primary)
            {
                strgrow(&csv_channels_owned, "%d,%d", channel_num, channel_num + 4);
            }
            else
            {
                strgrow(&csv_channels_owned, "%d,%d", channel_num - 4, channel_num);
            }
        }
    }
    else
    {
        const int *channels = NULL;
        if (is_5)
        {
            channels = unii_5g_chan2list(channel_num, width_mhz);
        }
        else if (is_6)
        {
            if (width_mhz == 320)
            {
                const int *channels1 = unii_6g_320_1_chan2list(channel_num);
                const int *channels2 = unii_6g_320_2_chan2list(channel_num);
                const int center1 = chanlist_to_center(channels1);
                const int center2 = chanlist_to_center(channels2);
                const int center = json_is_integer(center_freq0_chan_json_borrowed)
                                           ? json_integer_value(center_freq0_chan_json_borrowed)
                                           : 0;
                channels = (center == center1) ? channels1 : (center == center2) ? channels2 : NULL;
            }
            else
            {
                channels = unii_6g_chan2list(channel_num, width_mhz);
            }
        }

        while (channels != NULL && *channels != 0)
        {
            strgrow(&csv_channels_owned, "%d,", *channels);
            channels++;
        }
        strchomp(csv_channels_owned, ",");
    }

    json_t *channels_json_owned = json_string(csv_channels_owned ?: "");
    FREE(csv_channels_owned);
    return channels_json_owned;
}

static json_t *ox_dm_wifi_mfp_config_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    const char *pmf_str = json_string_value(value_borrowed) ?: "";
    const char *mfp_config = strcmp(pmf_str, SCHEMA_CONSTS_SECURITY_PMF_DISABLED) == 0   ? OX_DM_WIFI_TR181_MFP_DISABLED
                             : strcmp(pmf_str, SCHEMA_CONSTS_SECURITY_PMF_OPTIONAL) == 0 ? OX_DM_WIFI_TR181_MFP_OPTIONAL
                             : strcmp(pmf_str, SCHEMA_CONSTS_SECURITY_PMF_REQUIRED) == 0 ? OX_DM_WIFI_TR181_MFP_REQUIRED
                                                                                         : "";
    return json_string(mfp_config);
}

static json_t *ox_dm_wifi_mfp_config_to_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    const char *value_str = json_string_value(value_borrowed) ?: "";
    const bool is_empty = value_str && strlen(value_str) == 0;
    if (is_empty)
    {
        return ox_ovsdb_type_set_with_array(json_array());
    }

    const char *pmf_str = strcmp(value_str, OX_DM_WIFI_TR181_MFP_DISABLED) == 0   ? SCHEMA_CONSTS_SECURITY_PMF_DISABLED
                          : strcmp(value_str, OX_DM_WIFI_TR181_MFP_OPTIONAL) == 0 ? SCHEMA_CONSTS_SECURITY_PMF_OPTIONAL
                          : strcmp(value_str, OX_DM_WIFI_TR181_MFP_REQUIRED) == 0 ? SCHEMA_CONSTS_SECURITY_PMF_REQUIRED
                                                                                  : NULL;
    return pmf_str ? json_string(pmf_str) : NULL;
}

static os_tr181_error_t ox_dm_wifi_vif_reference_ssid(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(param_path == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(route == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(table_instance == NULL)) return OS_TR181_ERROR;

    const char *uuid_str = table_instance->uuid;
    if (uuid_str == NULL)
    {
        LOGD(LOG_PREFIX_INSTANCE_PARAM(table_instance, param_path, "instance uuid is NULL"));
        return OS_TR181_ERROR;
    }

    ox_router_t *router = table_instance->table_ctx->router;
    const char *route_path = "Device.WiFi.SSID.";
    ox_table_ctx_t *ssid_table_ctx = ds_tree_find(&router->table_ctxs, route_path);
    if (ssid_table_ctx == NULL)
    {
        LOGD(LOG_PREFIX_INSTANCE_PARAM(
                table_instance,
                param_path,
                "failed to find route context for path %s",
                route_path));
        return OS_TR181_ERROR;
    }

    ox_table_instance_t *ssid_instance = ds_tree_find(&ssid_table_ctx->table_instances_by_uuid, uuid_str);
    if (ssid_instance == NULL)
    {
        LOGD(LOG_PREFIX_INSTANCE_PARAM(
                table_instance,
                param_path,
                "failed to find SSID instance for uuid %s",
                uuid_str));
        return OS_TR181_ERROR;
    }

    char path[OS_TR181_PATH_MAX];
    snprintf(path, sizeof(path), "%s", ssid_instance->tr181_path);
    char *last_dot = strrchr(path, '.');
    char *last_char = path + strlen(path) - 1;
    if (last_dot != NULL && last_char != NULL && *last_char == '.' && last_dot == last_char)
    {
        *last_dot = '\0';
    }

    os_val_set_str_dup(tr181_value, path);
    return OS_TR181_SUCCESS;
}

static os_tr181_error_t ox_dm_wifi_radio_reference_ssid(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(param_path == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(route == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(table_instance == NULL)) return OS_TR181_ERROR;

    const char *uuid_str = table_instance->uuid;
    if (uuid_str == NULL)
    {
        LOGD(LOG_PREFIX_INSTANCE_PARAM(table_instance, param_path, "instance uuid is NULL"));
        return OS_TR181_ERROR;
    }

    ox_router_t *router = table_instance->table_ctx->router;
    const char *route_path = "Device.WiFi.Radio.";
    ox_table_ctx_t *radio_table_ctx = ds_tree_find(&router->table_ctxs, route_path);
    if (radio_table_ctx == NULL)
    {
        LOGD(LOG_PREFIX_INSTANCE_PARAM(
                table_instance,
                param_path,
                "failed to find route context for path %s",
                route_path));
        return OS_TR181_ERROR;
    }

    const char *cond_column = "vif_configs";
    json_t *cond_value_owned = ovsdb_tran_uuid_json(uuid_str);
    json_t *cond_owned = ovsdb_tran_cond_single_json(cond_column, OFUNC_INC, cond_value_owned);
    cond_value_owned = NULL;  // moved to cond_owned
    json_t *where_owned = json_array();
    json_array_append_new(where_owned, cond_owned);
    cond_owned = NULL;  // moved to where_owned
    json_t *rows_owned = ovsdb_sync_select_where2("Wifi_Radio_Config", where_owned);
    where_owned = NULL;  // moved to rows_owned
    json_t *first_row_borrowed = json_array_get(rows_owned, 0);
    json_t *radio_uuid_borrowed = json_object_get(first_row_borrowed, OX_OVSDB_UUID);
    const char *radio_uuid_str = json_string_value(json_array_get(radio_uuid_borrowed, 1)) ?: "";
    ox_table_instance_t *radio_instance = ds_tree_find(&radio_table_ctx->table_instances_by_uuid, radio_uuid_str);
    if (radio_instance == NULL)
    {
        LOGD(LOG_PREFIX_INSTANCE_PARAM(
                table_instance,
                param_path,
                "failed to find Radio instance for uuid %s",
                radio_uuid_str));
        json_decref(rows_owned);
        return OS_TR181_ERROR;
    }
    json_decref(rows_owned);
    radio_uuid_str = NULL;  // was borrowed from rows_owned

    char path[OS_TR181_PATH_MAX];
    snprintf(path, sizeof(path), "%s", radio_instance->tr181_path);
    char *last_dot = strrchr(path, '.');
    char *last_char = path + strlen(path) - 1;
    if (last_dot != NULL && last_char != NULL && *last_char == '.' && last_dot == last_char)
    {
        *last_dot = '\0';
    }

    os_val_set_str_dup(tr181_value, path);
    return OS_TR181_SUCCESS;
}

static json_t *ox_xlate_always_true(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)value_borrowed;
    (void)route;
    (void)table_instance;
    return json_boolean(true);
}

static json_t *ox_dm_wifi_xlate_negate(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)route;
    (void)table_instance;
    json_t *negated_owned =
            json_is_boolean(value_borrowed) ? json_boolean(!json_boolean_value(value_borrowed)) : json_null();
    return negated_owned;
}

static json_t *ox_dm_wifi_radio_init_row(const ox_route_table_t *route_table)
{
    (void)route_table;
    json_t *row_owned = json_object();
    json_object_set_new(row_owned, "freq_band", json_string("2.4G"));
    return row_owned;
}

const ox_route_t g_ox_dm_wifi[] = {
    {
        .table =
                {
                    .tr181_table = "Device.WiFi.Radio.",
                    .ovsdb_table = "Wifi_Radio_Config",
                    .ovsdb_sibling_table = "Wifi_Radio_State",
                    .ovsdb_sibling_column = "if_name",
                    .add_cb = ox_table_instance_add,
                    .del_cb = ox_table_instance_del,
                    .init_row_cb = ox_dm_wifi_radio_init_row,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.Radio.{i}.Enable",
                    .tr181_type = OS_TR181_TYPE_BOOL,
                    .ovsdb_column = "enabled",
                    .ovsdb_no_sibling = true,
                    .get_cb = ox_table_instance_param_get,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.Radio.{i}.Name",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "if_name",
                    .get_cb = ox_table_instance_param_get,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.Radio.{i}.Channel",
                    .tr181_type = OS_TR181_TYPE_INT,
                    .tr181_stash_as = "Channel",
                    .ovsdb_column = "channel",
                    .ovsdb_no_sibling = true,
                    .get_cb = ox_table_instance_param_get,
                    .set_cb = ox_table_instance_param_set,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.Radio.{i}.Status",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "enabled",
                    .ovsdb_sibling_only = true,
                    .get_cb = ox_table_instance_param_get,
                    .from_ovsdb = ox_dm_wifi_enabled_to_status_from_ovsdb,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.Radio.{i}.X_PLUME_freq_band",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "freq_band",
                    .get_cb = ox_table_instance_param_get,
                },
    },
#if 0
    /* FIXME: parameter can't be of type DICT, this will need custom mapping */
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.Radio.{i}.X_PLUME_hw_config",
                    .tr181_type = OS_TR181_TYPE_DICT,
                    .ovsdb_column = "hw_config",
                    .get_cb = ox_table_instance_param_get,
                    .set_cb = ox_table_instance_param_set,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.Radio.{i}.X_PLUME_vif_configs",
                    .tr181_type = OS_TR181_TYPE_DICT,
                    .ovsdb_column = "vif_configs",
                    .get_cb = ox_table_instance_param_get,
                },
    },
#endif
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.Radio.{i}.OperatingFrequencyBand",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "freq_band",
                    .ovsdb_no_sibling = true,
                    .get_cb = ox_table_instance_param_get,
                    .from_ovsdb = ox_dm_wifi_radio_freq_band_from_ovsdb,
                    .to_ovsdb = ox_dm_wifi_radio_freq_band_to_ovsdb,
                    /* This is not writable because Opensync doesn't support changing
                     * radio frequency band.
                     */
                },
    },
    {
        /* Same as OperatingFrequencyBand. Opensync always assumes single-band
         * radios and doesn't even allow expressing anything different.
         */
        .param =
                {
                    .tr181_param = "Device.WiFi.Radio.{i}.SupportedFrequencyBands",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "freq_band",
                    .get_cb = ox_table_instance_param_get,
                    .from_ovsdb = ox_dm_wifi_radio_freq_band_from_ovsdb,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.Radio.{i}.OperatingStandards",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "hw_mode,freq_band",
                    .ovsdb_no_sibling = true,
                    .get_cb = ox_table_instance_param_get,
                    .from_ovsdb = ox_dm_wifi_radio_hw_mode_from_ovsdb,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.Radio.{i}.PossibleChannels",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "allowed_channels",
                    .ovsdb_sibling_only = true,
                    .get_cb = ox_table_instance_param_get,
                    .from_ovsdb = ox_dm_wifi_radio_channel_list_from_ovsdb,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.Radio.{i}.OperatingChannelBandwidth",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .tr181_stash_as = "OperatingChannelBandwidth",
                    .ovsdb_column = "ht_mode,center_freq0_chan",
                    .ovsdb_no_sibling = true,
                    .get_cb = ox_table_instance_param_get,
                    .set_cb = ox_table_instance_param_set,
                    .from_ovsdb = ox_dm_wifi_radio_oper_chan_width_from_ovsdb,
                    .to_ovsdb = ox_dm_wifi_radio_oper_chan_width_to_ovsdb,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.Radio.{i}.ChannelsInUse",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "channel,freq_band,ht_mode,center_freq0_chan",
                    .ovsdb_sibling_only = true,
                    .get_cb = ox_table_instance_param_get,
                    .from_ovsdb = ox_dm_wifi_radio_channel_from_ovsdb,
                },
    },
    {
        .table =
                {
                    .tr181_table = "Device.WiFi.SSID.",
                    .ovsdb_table = "Wifi_VIF_Config",
                    .ovsdb_sibling_table = "Wifi_VIF_State",
                    .ovsdb_sibling_column = "if_name",
                    .add_cb = ox_table_instance_add,
                    .del_cb = ox_table_instance_del,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.SSID.{i}.Name",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "if_name",
                    .set_cb = ox_table_instance_param_set,
                    .get_cb = ox_table_instance_param_get,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.SSID.{i}.SSID",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "ssid",
                    .set_cb = ox_table_instance_param_set,
                    .get_cb = ox_table_instance_param_get,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.SSID.{i}.MACAddress",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "mac",
                    .get_cb = ox_table_instance_param_get,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.SSID.{i}.LowerLayers",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .get_cb = ox_dm_wifi_radio_reference_ssid,
                },
    },
    {
        .table =
                {
                    .tr181_table = "Device.WiFi.AccessPoint.",
                    .ovsdb_table = "Wifi_VIF_Config",
                    .ovsdb_sibling_table = "Wifi_VIF_State",
                    .ovsdb_sibling_column = "if_name",
                    .ovsdb_gating_column_name = "mode",
                    .ovsdb_gating_column_value = "ap",
                    .add_cb = ox_table_instance_add,
                    .del_cb = ox_table_instance_del,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.AccessPoint.{i}.SSIDReference",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .get_cb = ox_dm_wifi_vif_reference_ssid,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.AccessPoint.{i}.SSIDAdvertisementEnabled",
                    .tr181_type = OS_TR181_TYPE_BOOL,
                    .ovsdb_column = "ssid_broadcast",
                    .set_cb = ox_table_instance_param_set,
                    .get_cb = ox_table_instance_param_get,
                },
    },
    {
        .object =
                {
                    .tr181_object = "Device.WiFi.AccessPoint.{i}.Security",
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.AccessPoint.{i}.Security.ModeEnabled",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_no_sibling = true,
                    .ovsdb_column = "wpa"
                                    ",wpa_key_mgmt"
                                    ",wpa_pairwise_tkip"
                                    ",wpa_pairwise_ccmp"
                                    ",rsn_pairwise_tkip"
                                    ",rsn_pairwise_ccmp"
                                    ",rsn_pairwise_ccmp256"
                                    ",rsn_pairwise_gcmp"
                                    ",rsn_pairwise_gcmp256"
                                    ",pmf"
                                    ",rsno",
                    // FIXME: Setting is going to be a bit more involving. WiFi
                    // 7 updated WPA3 means different things for AKMs and
                    // ciphers so that will somehow be factored in.
                    //.set_cb = ox_table_instance_param_set,
                    .get_cb = ox_table_instance_param_get,
                    .from_ovsdb = ox_dm_wifi_security_mode_from_ovsdb,
                    //.to_ovsdb = ox_dm_wifi_security_mode_to_ovsdb,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.AccessPoint.{i}.Security.KeyPassphrase",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "wpa_psks",
                    // FIXME: This historically could've been "key" or any
                    // "key-X" if it was a lone "key-*" entry. To simplify for
                    // now, this should be enough.
                    .ovsdb_map_key = "key--1",
                    .set_cb = ox_table_instance_param_set,
                    .get_cb = ox_table_instance_param_get,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.AccessPoint.{i}.Security.MFPConfig",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "pmf",
                    .set_cb = ox_table_instance_param_set,
                    .get_cb = ox_table_instance_param_get,
                    .from_ovsdb = ox_dm_wifi_mfp_config_from_ovsdb,
                    .to_ovsdb = ox_dm_wifi_mfp_config_to_ovsdb,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.AccessPoint.{i}.IsolationEnable",
                    .tr181_type = OS_TR181_TYPE_BOOL,
                    .ovsdb_column = "ap_bridge",
                    .set_cb = ox_table_instance_param_set,
                    .get_cb = ox_table_instance_param_get,
                    .to_ovsdb = ox_dm_wifi_xlate_negate,
                    .from_ovsdb = ox_dm_wifi_xlate_negate,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.AccessPoint.{i}.Enable",
                    .tr181_type = OS_TR181_TYPE_BOOL,
                    .ovsdb_column = "enabled",
                    .ovsdb_no_sibling = true,
                    .set_cb = ox_table_instance_param_set,
                    .get_cb = ox_table_instance_param_get,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.AccessPoint.{i}.Status",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "enabled",
                    .ovsdb_sibling_only = true,
                    .get_cb = ox_table_instance_param_get,
                    .from_ovsdb = ox_dm_wifi_vif_status_from_ovsdb,
                },
    },
    {
        .table =
                {
                    .tr181_table = "Device.WiFi.AccessPoint.{i}.AssociatedDevice.",
                    .ovsdb_table = "Wifi_Associated_Clients",
                    .ovsdb_parent_table = "Wifi_VIF_State",
                    .ovsdb_parent_column = "associated_clients",
                    .add_cb = ox_table_instance_add,
                    .del_cb = ox_table_instance_del,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.AccessPoint.{i}.AssociatedDevice.{i}.MACAddress",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "mac",
                    .set_cb = ox_table_instance_param_set,
                    .get_cb = ox_table_instance_param_get,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.AccessPoint.{i}.AssociatedDevice.{i}.Active",
                    .tr181_type = OS_TR181_TYPE_BOOL,
                    .ovsdb_column = "mac",
                    .get_cb = ox_table_instance_param_get,
                    .from_ovsdb = ox_xlate_always_true,
                },
    },
    {
        .table =
                {
                    .tr181_table = "Device.WiFi.EndPoint.",
                    .ovsdb_table = "Wifi_VIF_Config",
                    .ovsdb_sibling_table = "Wifi_VIF_State",
                    .ovsdb_sibling_column = "if_name",
                    .ovsdb_gating_column_name = "mode",
                    .ovsdb_gating_column_value = "sta",
                    .add_cb = ox_table_instance_add,
                    .del_cb = ox_table_instance_del,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.EndPoint.{i}.Enable",
                    .tr181_type = OS_TR181_TYPE_BOOL,
                    .ovsdb_column = "enabled",
                    .ovsdb_no_sibling = true,
                    .get_cb = ox_table_instance_param_get,
                    .set_cb = ox_table_instance_param_set,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.EndPoint.{i}.Status",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "enabled",
                    .ovsdb_sibling_only = true,
                    .get_cb = ox_table_instance_param_get,
                    .from_ovsdb = ox_dm_wifi_vif_status_from_ovsdb,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.EndPoint.{i}.SSIDReference",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .get_cb = ox_dm_wifi_vif_reference_ssid,
                },
    },
    {
        .table =
                {
                    .tr181_table = "Device.WiFi.EndPoint.{i}.Profile.",
                    .ovsdb_table = "Wifi_Credential_Config",
                    .ovsdb_parent_table = "Wifi_VIF_Config",
                    .ovsdb_parent_column = "credential_configs",
                    .add_cb = ox_table_instance_add,
                    .del_cb = ox_table_instance_del,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.EndPoint.{i}.Profile.{i}.Enable",
                    .tr181_type = OS_TR181_TYPE_BOOL,
                    .ovsdb_column = "enabled",
                    .get_cb = ox_table_instance_param_get,
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.EndPoint.{i}.Profile.{i}.SSID",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "ssid",
                    .get_cb = ox_table_instance_param_get,
                    .set_cb = ox_table_instance_param_set,
                },
    },
    {
        .object =
                {
                    .tr181_object = "Device.WiFi.EndPoint.{i}.Profile.{i}.Security",
                },
    },
    {
        .param =
                {
                    .tr181_param = "Device.WiFi.EndPoint.{i}.Profile.{i}.Security.KeyPassphrase",
                    .tr181_type = OS_TR181_TYPE_STRING,
                    .ovsdb_column = "security",
                    .ovsdb_map_key = "key",
                    .get_cb = ox_table_instance_param_get,
                    .set_cb = ox_table_instance_param_set,
                },
    },

    {},
};
