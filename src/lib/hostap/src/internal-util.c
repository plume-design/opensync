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

#include "log.h"
#include "schema.h"
#include "internal-util.h"


bool
util_vif_wpa_key_mgmt_match(const struct schema_Wifi_VIF_Config *vconf,
                            const char *key_mgmt)
{
    int i = 0;

    for (i = 0; i < vconf->wpa_key_mgmt_len; i++) {
        if (strstr(vconf->wpa_key_mgmt[i], key_mgmt))
            return true;
    }

    return false;
}

void
util_vif_get_wpa_pairwise(const struct schema_Wifi_VIF_Config *vconf,
                          char *buf,
                          size_t len)
{
    memset(buf, 0, len);

    if (!vconf->wpa)
        return;

    if (util_vif_wpa_key_mgmt_match(vconf, "wpa-"))
        csnprintf(&buf, &len, "TKIP ");
    if (util_vif_wpa_key_mgmt_match(vconf, "wpa2-") ||
        util_vif_wpa_key_mgmt_match(vconf, "sae") ||
        util_vif_wpa_key_mgmt_match(vconf, "dpp"))
        csnprintf(&buf, &len, "CCMP ");

    WARN_ON(len == 1); /* likely buf was truncated */
}

void
util_vif_get_wpa_key_mgmt(const struct schema_Wifi_VIF_Config *vconf,
                          char *buf,
                          size_t len)
{
    memset(buf, 0, len);

    if (!vconf->wpa)
        return;

    /* Both WPA-PSK and WPA2-PSK */
    if (util_vif_wpa_key_mgmt_match(vconf, "-psk")) csnprintf(&buf, &len, "WPA-PSK ");

    if (util_vif_wpa_key_mgmt_match(vconf, "wpa2-eap")) csnprintf(&buf, &len, "WPA-EAP ");
    if (util_vif_wpa_key_mgmt_match(vconf, "sae")) csnprintf(&buf, &len, "SAE ");
    if (util_vif_wpa_key_mgmt_match(vconf, "ft-wpa2-psk")) csnprintf(&buf, &len, "FT-PSK ");
    if (util_vif_wpa_key_mgmt_match(vconf, "dpp")) csnprintf(&buf, &len, "DPP ");

    WARN_ON(len == 1); /* likely buf was truncated */
}

void
util_vif_get_ieee80211w(const struct schema_Wifi_VIF_Config *vconf,
                        char *buf,
                        size_t len)
{
    bool need_11w = util_vif_wpa_key_mgmt_match(vconf, "sae")
                 || util_vif_wpa_key_mgmt_match(vconf, "dpp");
    bool non_11w = util_vif_wpa_key_mgmt_match(vconf, "ft-wpa2-")
                || util_vif_wpa_key_mgmt_match(vconf, "wpa2-");

    memset(buf, 0, len);

    if (!vconf->wpa)
        return;

    if (need_11w) {
        if (non_11w)
            csnprintf(&buf, &len, "1"); /* optional, for mixed mode */
        else
            csnprintf(&buf, &len, "2");
    }
    /* For non-SAE configs leave ieee80211w empty ("") */

    WARN_ON(len == 1); /* likely buf was truncated */
}
