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

#ifndef INTERNAL_UTIL_H_INCLUDED
#define INTERNAL_UTIL_H_INCLUDED

/* Protected Management Frames */
enum hostap_conf_pmf {
    HOSTAP_CONF_PMF_DISABLED = 0,
    HOSTAP_CONF_PMF_OPTIONAL = 1,
    HOSTAP_CONF_PMF_REQUIRED = 2
};

/* WPA mode as defined in hostapd.conf */
enum hostap_conf_wpa {
    HOSTAP_CONF_WPA_OPEN = 0,
    HOSTAP_CONF_WPA_WPA  = 1,
    HOSTAP_CONF_WPA_RSN  = 2
};

/* Key Management Algorithm */
enum hostap_conf_key_mgmt {
    HOSTAP_CONF_KEY_MGMT_WPA_PSK,
    HOSTAP_CONF_KEY_MGMT_WPA_PSK_SHA256,
    HOSTAP_CONF_KEY_MGMT_WPA_EAP,
    HOSTAP_CONF_KEY_MGMT_WPA_EAP_SHA256,
    HOSTAP_CONF_KEY_MGMT_WPA_EAP_B_192,
    HOSTAP_CONF_KEY_MGMT_FT_SAE,
    HOSTAP_CONF_KEY_MGMT_FT_PSK,
    HOSTAP_CONF_KEY_MGMT_FT_EAP,
    HOSTAP_CONF_KEY_MGMT_FT_EAP_SHA384,
    HOSTAP_CONF_KEY_MGMT_DPP,
    HOSTAP_CONF_KEY_MGMT_SAE,
    HOSTAP_CONF_KEY_MGMT_OWE,
    /* legacy and deprecated */
    HOSTAP_CONF_KEY_MGMT_WPA2_PSK,
    HOSTAP_CONF_KEY_MGMT_WPA_PSK_LEGACY,
    HOSTAP_CONF_KEY_MGMT_WPA2_EAP,
    HOSTAP_CONF_KEY_MGMT_FT_WPA2_PSK,
    HOSTAP_CONF_KEY_MGMT_UNKNOWN
};

bool
util_vif_wpa_key_mgmt_partial_match(const struct schema_Wifi_VIF_Config *vconf,
                                    const char *key_mgmt);

bool
util_vif_wpa_key_mgmt_exact_match(const struct schema_Wifi_VIF_Config *vconf,
                                  const char *key_mgmt);

void
util_vif_get_wpa_pairwise(const struct schema_Wifi_VIF_Config *vconf,
                          char *buf,
                          size_t len);

bool
util_vif_pairwise_supported(const struct schema_Wifi_VIF_Config *vconf);

void
util_vif_get_wpa_key_mgmt(const struct schema_Wifi_VIF_Config *vconf,
                          char *buf,
                          size_t len);

int
util_vif_get_wpa(const struct schema_Wifi_VIF_Config *vconf);

void
util_vif_get_ieee80211w(const struct schema_Wifi_VIF_Config *vconf,
                        char *buf,
                        size_t len);

/* Type conversion helpers. schema->internal, internal->schema */
enum hostap_conf_pmf
util_vif_pmf_schema_to_enum(const char* pmf);

const char*
util_vif_pmf_enum_to_schema(enum hostap_conf_pmf pmf);

enum hostap_conf_key_mgmt
util_vif_wpa_key_mgmt_to_enum(const char *key_mgmt);

#endif /* INTERNAL_UTIL_H_INCLUDED */
