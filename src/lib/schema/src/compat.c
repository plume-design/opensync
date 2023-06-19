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

/* libc */
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* opensync */
#include <log.h>
#include <const.h>
#include <util.h>
#include <schema.h>
#include <schema_consts.h>
#include <schema_compat.h>

#define vif_is_gen1(x) ((x)->security_len > 0 && (x)->wpa_exists == false)
#define vif_is_gen2(x) ((x)->wpa_exists == true && !((x)->wpa_pairwise_tkip_exists == true \
                                                  || (x)->wpa_pairwise_ccmp_exists == true \
                                                  || (x)->rsn_pairwise_tkip_exists == true \
                                                  || (x)->rsn_pairwise_ccmp_exists == true))
#define vif_is_gen3(x) ((x)->wpa_exists == true &&  ((x)->wpa_pairwise_tkip_exists == true \
                                                  || (x)->wpa_pairwise_ccmp_exists == true \
                                                  || (x)->rsn_pairwise_tkip_exists == true \
                                                  || (x)->rsn_pairwise_ccmp_exists == true))

#if 0
static void
schema_vconf_from_gen1()
{
    bool akm_psk = false;
    bool akm_eap = false;
    bool open = false;
    bool wpa = false;
    bool rsn = false;

    for (i = 0; i < vconf->security_len; i++) {
        const char *k = vconf->security_keys[i];
        const char *v = vconf->security[i];

        if (strcmp(k, SCHEMA_CONSTS_SECURITY_ENCRYPT) == 0) {
            WARN_ON(strcmp(v, SCHEMA_CONSTS_SECURITY_ENCRYPT_WEP) == 0);
            open = (strcmp(v, SCHEMA_CONSTS_SECURITY_ENCRYPT_OPEN) == 0);
            akm_psk = (strcmp(v, SCHEMA_CONSTS_SECURITY_ENCRYPT_WPA) == 0);
            akm_eap = (strcmp(v, SCHEMA_CONSTS_SECURITY_ENCRYPT_WPA_EAP) == 0);
        }
        else if (strcmp(k, SCHEMA_CONSTS_SECURITY_MODE) == 0) {
            if (strcmp(v, SCHEMA_CONSTS_SECURITY_ENCRYPT_WPA_MIX) == 0)
                v = "3";

            const int mask = atoi(v)
            if (mask & 1) wpa = true;
            if (mask & 2) rsn = true;
        }
        else if (strstr(k, SCHEMA_CONSTS_SECURITY_KEY) == k) {
            SCHEMA_KEY_VAL_APPEND(vconf->wpa_psks, k, v)
        }
        /* FIXME: 
           SCHEMA_CONSTS_SECURITY_RADIUS_IP        "radius_server_ip"
           SCHEMA_CONSTS_SECURITY_RADIUS_PORT      "radius_server_port"
           SCHEMA_CONSTS_SECURITY_RADIUS_SECRET    "radius_server_secret"
        */
    }

    if (akm_psk) SCHEMA_VAL_APPEND(vconf->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_PSK);
    if (akm_psk && vconf->ft_psk) SCHEMA_VAL_APPEND(vconf->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_PSK);
    if (akm_eap) SCHEMA_VAL_APPEND(vconf->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_EAP);

    SCHEMA_SET_STR(vconf->pmf, SCHEMA_CONSTS_SECURITY_PMF_DISABLED);

    WARN_ON(open == true && vconf->wpa_key_mgmt_len > 0);
    WARN_ON(open == false && vconf->wpa_key_mgmt_len == 0);
}
#endif

static void
schema_vconf_oftag_gen1_to_gen2(struct schema_Wifi_VIF_Config *vconf)
{
    const char *mode = SCHEMA_KEY_VAL_NULL(vconf->security, SCHEMA_CONSTS_SECURITY_ENCRYPT);
    const bool psk = strcmp(mode, SCHEMA_CONSTS_SECURITY_ENCRYPT_WPA);
    const char *prefix = "oftag-";
    const char *def = "oftag";
    const char *defkey = "key";
    const size_t prefix_len = strlen(prefix);
    int i;

    for (i = 0; i < vconf->security_len; i++) {
        const char *k = vconf->security_keys[i];
        const char *v = vconf->security[i];

        if (strstr(k, prefix)) {
            k += prefix_len;
            SCHEMA_KEY_VAL_APPEND(vconf->wpa_oftags, k, v);
        }

        if (strcmp(k, def) == 0) {
            if (psk) SCHEMA_KEY_VAL_APPEND(vconf->wpa_oftags, defkey, v);
            else     SCHEMA_SET_STR(vconf->default_oftag, v);
        }
    }
}

#define schema_vif_gen1_to_gen2(vif) \
do { \
    bool open = false; \
    bool wep = false; \
    bool psk = false; \
    bool eap = false; \
    int mode = 3; \
    int i; \
    for (i = 0; i < (vif)->security_len; i++) { \
        const char *k = (vif)->security_keys[i]; \
        const char *v = (vif)->security[i]; \
 \
        if (strstr(k, SCHEMA_CONSTS_SECURITY_KEY) == k) { \
            SCHEMA_KEY_VAL_APPEND((vif)->wpa_psks, k, v); \
        } \
        if (strcmp(k, SCHEMA_CONSTS_SECURITY_MODE) == 0) { \
            if (strcmp(v, SCHEMA_CONSTS_SECURITY_ENCRYPT_WPA_MIX) == 0) \
                v = "3"; \
 \
            mode = atoi(v); \
        } \
        if (strcmp(k, SCHEMA_CONSTS_SECURITY_ENCRYPT) == 0) { \
            if (strcmp(v, SCHEMA_CONSTS_SECURITY_ENCRYPT_OPEN) == 0) { open = true; } \
            if (strcmp(v, SCHEMA_CONSTS_SECURITY_ENCRYPT_WEP) == 0) { wep = true; } \
            if (strcmp(v, SCHEMA_CONSTS_SECURITY_ENCRYPT_WPA) == 0) { psk = true; } \
            if (strcmp(v, SCHEMA_CONSTS_SECURITY_ENCRYPT_WPA_EAP) == 0) { eap = true; } \
        } \
 \
        if (strcmp(k, SCHEMA_CONSTS_SECURITY_RADIUS_IP) == 0) { WARN_ON(1); } \
        if (strcmp(k, SCHEMA_CONSTS_SECURITY_RADIUS_PORT) == 0) { WARN_ON(1); } \
        if (strcmp(k, SCHEMA_CONSTS_SECURITY_RADIUS_SECRET) == 0) { WARN_ON(1); } \
    } \
 \
    WARN_ON(wep); \
 \
    const bool wpa = (mode & 1) ? true : false; \
    const bool rsn = (mode & 2) ? true : false; \
 \
    SCHEMA_SET_BOOL((vif)->wpa, !open); \
 \
    if (wpa && psk) SCHEMA_VAL_APPEND((vif)->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_PSK); \
    if (rsn && psk) SCHEMA_VAL_APPEND((vif)->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA2_PSK); \
    if (wpa && psk && (vif)->ft_psk) SCHEMA_VAL_APPEND((vif)->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_PSK); \
    if (rsn && psk && (vif)->ft_psk) SCHEMA_VAL_APPEND((vif)->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_WPA2_PSK); \
    if (wpa && eap) SCHEMA_VAL_APPEND((vif)->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_EAP); \
    if (rsn && eap) SCHEMA_VAL_APPEND((vif)->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA2_EAP); \
} while (0)

/* FIXME: RADIUS support */
#define schema_vif_gen2_to_gen3(vif) \
do { \
    bool wpa = false; \
    bool rsn = false; \
    bool psk = false; \
    bool eap = false; \
    bool sae = false; \
    bool dpp = false; \
    bool ft_psk = false; \
    bool ft_sae = false; \
    int i; \
 \
    for (i = 0; i < (vif)->wpa_key_mgmt_len; i++) { \
        const char *v = (vif)->wpa_key_mgmt[i]; \
 \
             if (strcmp(v, SCHEMA_CONSTS_KEY_WPA2_PSK) == 0) { rsn = true; psk = true; } \
        else if (strcmp(v, SCHEMA_CONSTS_KEY_WPA2_EAP) == 0) { rsn = true; eap = true; } \
        else if (strcmp(v, SCHEMA_CONSTS_KEY_FT_WPA2_PSK) == 0) { rsn = true; ft_psk = true; } \
        else if (strcmp(v, SCHEMA_CONSTS_KEY_WPA_PSK) == 0) { wpa = true; psk = true; } \
        else if (strcmp(v, SCHEMA_CONSTS_KEY_WPA_EAP) == 0) { wpa = true; eap = true; } \
        else if (strcmp(v, SCHEMA_CONSTS_KEY_FT_PSK) == 0) { wpa = true; ft_psk = true; } \
        else if (strcmp(v, SCHEMA_CONSTS_KEY_SAE) == 0) { sae = true; } \
        else if (strcmp(v, SCHEMA_CONSTS_KEY_FT_SAE) == 0) { ft_sae = true; } \
        else if (strcmp(v, SCHEMA_CONSTS_KEY_DPP) == 0) { dpp = true; } \
        else { LOGW("%s: unexpected wpa_key_mgmt: %s", __func__, v); } \
    } \
 \
    const bool wpa3_transition = sae && (wpa || rsn); \
    const bool wpa3_only = sae && !(wpa || rsn); \
    const char *pmf = wpa3_only ? SCHEMA_CONSTS_SECURITY_PMF_REQUIRED : \
                      wpa3_transition ? SCHEMA_CONSTS_SECURITY_PMF_OPTIONAL : \
                      SCHEMA_CONSTS_SECURITY_PMF_DISABLED; \
 \
    SCHEMA_SET_INT((vif)->wpa_pairwise_tkip, wpa); \
    SCHEMA_SET_INT((vif)->wpa_pairwise_ccmp, false); \
    SCHEMA_SET_INT((vif)->rsn_pairwise_tkip, wpa && rsn); \
    SCHEMA_SET_INT((vif)->rsn_pairwise_ccmp, (rsn || sae || dpp)); \
    SCHEMA_SET_STR((vif)->pmf, pmf); \
 \
    (vif)->wpa_key_mgmt_len = 0; \
    if (psk) SCHEMA_VAL_APPEND((vif)->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_PSK); \
    if (sae) SCHEMA_VAL_APPEND((vif)->wpa_key_mgmt, SCHEMA_CONSTS_KEY_SAE); \
    if (eap) SCHEMA_VAL_APPEND((vif)->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_EAP); \
    if (dpp) SCHEMA_VAL_APPEND((vif)->wpa_key_mgmt, SCHEMA_CONSTS_KEY_DPP); \
    if (ft_psk) SCHEMA_VAL_APPEND((vif)->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_PSK); \
    if (ft_sae) SCHEMA_VAL_APPEND((vif)->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_SAE); \
} while (0)

#define schema_vif_gen1_to_gen2_clear(vif) \
do { \
    SCHEMA_UNSET_MAP((vif)->security); \
    SCHEMA_UNSET_FIELD((vif)->ft_psk); \
} while (0)

#define schema_vif_gen2_to_gen3_clear(vif) \
do { \
    SCHEMA_UNSET_FIELD((vif)->radius_srv_addr); \
    SCHEMA_UNSET_FIELD((vif)->radius_srv_port); \
    SCHEMA_UNSET_FIELD((vif)->radius_srv_secret); \
} while (0)

#define schema_vif_gen3_to_gen2_clear(vif) \
do { \
    SCHEMA_UNSET_FIELD(vif->wpa_pairwise_tkip); \
    SCHEMA_UNSET_FIELD(vif->wpa_pairwise_ccmp); \
    SCHEMA_UNSET_FIELD(vif->rsn_pairwise_tkip); \
    SCHEMA_UNSET_FIELD(vif->rsn_pairwise_ccmp); \
    SCHEMA_UNSET_FIELD(vif->pmf); \
} while (0)

#define schema_vif_gen2_to_gen1_clear(vif) \
do { \
    SCHEMA_SET_BOOL(vif->wpa, false); \
    SCHEMA_UNSET_MAP(vif->wpa_psks); \
    SCHEMA_UNSET_MAP(vif->wpa_key_mgmt); \
    SCHEMA_UNSET_FIELD(vif->radius_srv_addr); \
    SCHEMA_UNSET_FIELD(vif->radius_srv_port); \
    SCHEMA_UNSET_FIELD(vif->radius_srv_secret); \
    SCHEMA_UNSET_FIELD(vif->wpa); \
} while (0)


#define schema_vif_gen3_to_gen2(vif) \
do { \
    const bool wpa = vif->wpa_pairwise_tkip \
                  || vif->wpa_pairwise_ccmp; \
    const bool rsn = vif->rsn_pairwise_tkip \
                  || vif->rsn_pairwise_ccmp; \
    int i; \
 \
    for (i = 0; i < vif->wpa_key_mgmt_len; i++) { \
        char *v = vif->wpa_key_mgmt[i]; \
 \
        if (strcmp(v, SCHEMA_CONSTS_KEY_WPA_PSK) == 0) { \
            if ( wpa && rsn) SCHEMA_VAL_APPEND(vif->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA2_PSK); \
            if (!wpa && rsn) strcpy(v, SCHEMA_CONSTS_KEY_WPA2_PSK); \
        } \
 \
        if (strcmp(v, SCHEMA_CONSTS_KEY_WPA_EAP) == 0) { \
            if ( wpa && rsn) SCHEMA_VAL_APPEND(vif->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA2_EAP); \
            if (!wpa && rsn) strcpy(v, SCHEMA_CONSTS_KEY_WPA2_EAP); \
        } \
 \
        if (strcmp(v, SCHEMA_CONSTS_KEY_FT_PSK) == 0) { \
            if ( wpa && rsn) SCHEMA_VAL_APPEND(vif->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_WPA2_PSK); \
            if (!wpa && rsn) strcpy(v, SCHEMA_CONSTS_KEY_FT_WPA2_PSK); \
        } \
    } \
} while (0)

#define schema_vif_gen2_to_gen1(vif) \
do { \
    bool wpa = false; \
    bool rsn = false; \
    bool psk = false; \
    bool eap = false; \
    bool ft_psk = false; \
    int i; \
 \
    for (i = 0; i < vif->wpa_key_mgmt_len; i++) { \
        const char *v = vif->wpa_key_mgmt[i]; \
 \
             if (strcmp(v, SCHEMA_CONSTS_KEY_WPA2_PSK) == 0) { rsn = true; psk = true; } \
        else if (strcmp(v, SCHEMA_CONSTS_KEY_WPA2_EAP) == 0) { rsn = true; eap = true; } \
        else if (strcmp(v, SCHEMA_CONSTS_KEY_FT_WPA2_PSK) == 0) { rsn = true; ft_psk = true; } \
        else if (strcmp(v, SCHEMA_CONSTS_KEY_WPA_PSK) == 0) { wpa = true; psk = true; } \
        else if (strcmp(v, SCHEMA_CONSTS_KEY_WPA_EAP) == 0) { wpa = true; eap = true; } \
        else if (strcmp(v, SCHEMA_CONSTS_KEY_FT_PSK) == 0) { wpa = true; ft_psk = true; } \
        else { LOGW("%s: unexpected wpa_key_mgmt: %s", __func__, v); } \
    } \
 \
    const int mode = (wpa ? 1 : 0) | (rsn ? 2 : 0); \
    char mode_str[2]; \
    snprintf(mode_str, sizeof(mode_str), "%d", mode); \
 \
    const bool open = !(wpa || rsn); \
    const char *encr_str = open ? SCHEMA_CONSTS_SECURITY_ENCRYPT_OPEN : \
                           psk ? SCHEMA_CONSTS_SECURITY_ENCRYPT_WPA : \
                           eap ? SCHEMA_CONSTS_SECURITY_ENCRYPT_WPA_EAP : \
                           ""; \
 \
    WARN_ON(psk && eap); \
    WARN_ON(strlen(encr_str) == 0); \
    SCHEMA_KEY_VAL_APPEND(vif->security, SCHEMA_CONSTS_SECURITY_ENCRYPT, encr_str); \
    SCHEMA_KEY_VAL_APPEND(vif->security, SCHEMA_CONSTS_SECURITY_MODE, mode_str); \
    SCHEMA_SET_BOOL(vif->ft_psk, ft_psk); \
 \
    for (i = 0; i < vif->wpa_psks_len; i++) { \
        const char *k = vif->wpa_psks_keys[i]; \
        const char *v = vif->wpa_psks[i]; \
        SCHEMA_KEY_VAL_APPEND(vif->security, k, v); \
    } \
 \
    if (eap) { \
        if (vif->radius_srv_addr_exists) { \
            SCHEMA_KEY_VAL_APPEND(vif->security, \
                                  SCHEMA_CONSTS_SECURITY_RADIUS_IP, \
                                  vif->radius_srv_addr); \
        } \
 \
        if (vif->radius_srv_port_exists) { \
            char port_str[32]; \
            snprintf(port_str, sizeof(port_str), "%d", vif->radius_srv_port); \
            SCHEMA_KEY_VAL_APPEND(vif->security, \
                                  SCHEMA_CONSTS_SECURITY_RADIUS_PORT, \
                                  port_str); \
        } \
 \
        if (vif->radius_srv_secret_exists) { \
            SCHEMA_KEY_VAL_APPEND(vif->security, \
                                  SCHEMA_CONSTS_SECURITY_RADIUS_SECRET, \
                                  vif->radius_srv_secret); \
        } \
    } \
} while (0)

static void
schema_vconf_gen1_to_gen2(struct schema_Wifi_VIF_Config *vconf)
{
    schema_vconf_oftag_gen1_to_gen2(vconf);
    schema_vif_gen1_to_gen2(vconf);
    schema_vif_gen1_to_gen2_clear(vconf);
}

static void
schema_vconf_gen2_to_gen3(struct schema_Wifi_VIF_Config *vconf)
{
    schema_vif_gen2_to_gen3(vconf);
    schema_vif_gen2_to_gen3_clear(vconf);
}

static void
schema_vstate_gen1_to_gen2(struct schema_Wifi_VIF_State *vstate)
{
    schema_vif_gen1_to_gen2(vstate);
}

static void
schema_vstate_gen2_to_gen3(struct schema_Wifi_VIF_State *vstate)
{
    schema_vif_gen2_to_gen3(vstate);
    schema_vif_gen2_to_gen3_clear(vstate);
}

static void
schema_vstate_gen3_to_gen2(struct schema_Wifi_VIF_State *vstate)
{
    schema_vif_gen3_to_gen2(vstate);
    schema_vif_gen3_to_gen2_clear(vstate);
}

#if 0
/*
This isn't currently needed, but since I already coded it
I'm keeping it around just in case.
*/

static void
schema_vconf_oftag_gen2_to_gen1(struct schema_Wifi_VIF_Config *vconf)
{
    const char *mode = SCHEMA_KEY_VAL_NULL(vconf->security, SCHEMA_CONSTS_SECURITY_ENCRYPT);
    const bool open = (strcmp(mode, SCHEMA_CONSTS_SECURITY_ENCRYPT_OPEN) == 0);
    const bool eap = (strcmp(mode, SCHEMA_CONSTS_SECURITY_ENCRYPT_WPA_EAP) == 0);
    int i;

    for (i = 0; i < vconf->wpa_oftags_len; i++) {
        const char *k = vconf->wpa_oftags_keys[i];
        const char *v = vconf->wpa_oftags[i];

        char key[64];
        if (strcmp(k, "key") == 0) snprintf(key, sizeof(key), "oftag");
        else                       snprintf(key, sizeof(key), "oftag-%s", k);

        SCHEMA_KEY_VAL_APPEND(vconf->security, key, v);
    }

    if (vconf->default_oftag_exists) {
        if (open || eap) SCHEMA_KEY_VAL_APPEND(vconf->security, "oftag", vconf->default_oftag);
    }

    SCHEMA_UNSET_MAP(vconf->wpa_oftags);
    SCHEMA_UNSET_FIELD(vconf->default_oftag);
}
#endif

static void
schema_vstate_gen1_sync_to_vconf(struct schema_Wifi_VIF_State *vstate,
                                 struct schema_Wifi_VIF_Config *vconf)
{
    char *s_mode = SCHEMA_KEY_VAL_NULL(vstate->security, SCHEMA_CONSTS_SECURITY_MODE);
    char *c_mode = SCHEMA_KEY_VAL_NULL(vconf->security, SCHEMA_CONSTS_SECURITY_MODE);

    const bool strip_mode = (!c_mode && s_mode);
    if (strip_mode) {
        struct schema_Wifi_VIF_State tmp = *vstate;
        int i;

        vstate->security_len = 0;
        for (i = 0; i < tmp.security_len; i++) {
            char *k = tmp.security_keys[i];
            char *v = tmp.security[i];
            const bool is_mode = (strcmp(k, SCHEMA_CONSTS_SECURITY_MODE) == 0);
            if (is_mode == false) SCHEMA_KEY_VAL_APPEND(vstate->security, k, v);
        }
    }

    if (c_mode && s_mode) {
        const bool str_mismatch = (strcmp(s_mode, c_mode) != 0);
        if (str_mismatch) {
            const char *a = c_mode;
            const char *b = s_mode;
            if (strcmp(a, SCHEMA_CONSTS_SECURITY_ENCRYPT_WPA_MIX) == 0) a = "3";
            if (strcmp(b, SCHEMA_CONSTS_SECURITY_ENCRYPT_WPA_MIX) == 0) b = "3";

            const bool same_mode = (atoi(a) == atoi(b));
            if (same_mode) {
                strcpy(s_mode, c_mode);
            }
        }
    }

    int i;
    for (i = 0; i < vconf->security_len; i++) {
        const char *k = vconf->security_keys[i];
        const char *v = vconf->security[i];

        if (strstr(k, "oftag") != NULL) {
            SCHEMA_KEY_VAL_APPEND(vstate->security, k, v);
        }
    }
}

static void
schema_vstate_gen2_to_gen1(struct schema_Wifi_VIF_State *vstate)
{
    schema_vif_gen2_to_gen1(vstate);
    schema_vif_gen2_to_gen1_clear(vstate);
}

void
schema_vconf_unify(struct schema_Wifi_VIF_Config *vconf)
{
    if (vif_is_gen1(vconf)) schema_vconf_gen1_to_gen2(vconf);
    if (vif_is_gen2(vconf)) schema_vconf_gen2_to_gen3(vconf);
}

void
schema_vstate_unify(struct schema_Wifi_VIF_State *vstate)
{
    if (vif_is_gen1(vstate)) schema_vstate_gen1_to_gen2(vstate);
    if (vif_is_gen2(vstate)) schema_vstate_gen2_to_gen3(vstate);
}

static bool
schema_vstate_vconf_akm_intersect(const struct schema_Wifi_VIF_State *vstate,
                                  const struct schema_Wifi_VIF_Config *vconf)
{
    int i;
    int ii;
    for (i = 0; i < vconf->wpa_key_mgmt_len; i++) {
        const char *a = vconf->wpa_key_mgmt[i];
        for (ii = 0; ii < vstate->wpa_key_mgmt_len; ii++) {
            const char *b = vstate->wpa_key_mgmt[ii];
            if (strcmp(a, b) == 0) return true;
        }
    }
    return false;
}

static bool
schema_vconf_akm_has(const struct schema_Wifi_VIF_Config *vconf,
                     const char *akm)
{
    int i;
    for (i = 0; i < vconf->wpa_key_mgmt_len; i++) {
        if (strcmp(vconf->wpa_key_mgmt[i], akm) == 0) {
            return true;
        }
    }
    return false;
}

static bool
schema_vstate_akm_has(const struct schema_Wifi_VIF_State *vstate,
                      const char *akm)
{
    int i;
    for (i = 0; i < vstate->wpa_key_mgmt_len; i++) {
        if (strcmp(vstate->wpa_key_mgmt[i], akm) == 0) {
            return true;
        }
    }
    return false;
}

bool
schema_key_id_from_cstr(const char *key_id, int *idx)
{
    const char *idx0 = "key";
    const char *prefix = "key-";
    const size_t prefix_len = strlen(prefix);
    const bool is_idx0 = (strcmp(key_id, idx0) == 0);
    if (is_idx0) {
        *idx = 0;
        return true;
    }
    const bool invalid_prefix = (strncmp(key_id, prefix, prefix_len) != 0);
    if (invalid_prefix) {
        return false;
    }
    *idx = atoi(key_id + prefix_len);
    return true;
}

static bool
schema_key_id_is_0(const char *key_id)
{
    int idx;
    return schema_key_id_from_cstr(key_id, &idx) && idx == 0;
}

void
schema_vstate_sync_to_vconf(struct schema_Wifi_VIF_State *vstate,
                            struct schema_Wifi_VIF_Config *vconf)
{
    const bool is_ap = ((strcmp(vconf->mode, "ap") == 0) ||
                        (strcmp(vstate->mode, "ap") == 0));
    const bool is_sta = ((strcmp(vconf->mode, "sta") == 0) ||
                         (strcmp(vstate->mode, "sta") == 0));
    const bool is_single_psk = (vconf->wpa_psks_len == 1 &&
                                vstate->wpa_psks_len == 1);
    const bool has_sae = schema_vconf_akm_has(vconf, SCHEMA_CONSTS_KEY_SAE)
                      && schema_vstate_akm_has(vstate, SCHEMA_CONSTS_KEY_SAE);

    /* The reported key id string may differ from the
     * requested one because there's no key ids, per se, for
     * STA interfaces. As such, just copy over whatever's
     * found in the Config side to allow controller to match
     * and continue without timeouts.
     */
    if (is_sta && is_single_psk) {
        STRSCPY_WARN(vstate->wpa_psks_keys[0], vconf->wpa_psks_keys[0]);
    }

    /* Another exception is when SAE is involved. SAE can't
     * really be used with multi-psk and key_ids. Running
     * SAE + PSK and >1 passphrase is already undefined
     * behaviour because it's impossible for end-user to
     * know which will be used.
     */
    if (is_ap && is_single_psk && has_sae) {
        STRSCPY_WARN(vstate->wpa_psks_keys[0], vconf->wpa_psks_keys[0]);
    }

    /* The "key-0" and "key" mean the same thing. However
     * the way these can get generated may vary. Make sure
     * these are aligned properly such that if someone puts
     * one in Config, they can expect the identical string
     * back in State.
     */
    if (is_ap) {
        ssize_t vconf_idx0 = -1;
        ssize_t vstate_idx0 = -1;
        ssize_t i;

        for (i = 0; i < vconf->wpa_psks_len; i++) {
            if (schema_key_id_is_0(vconf->wpa_psks_keys[i])) {
                vconf_idx0 = i;
            }
        }

        for (i = 0; i < vstate->wpa_psks_len; i++) {
            if (schema_key_id_is_0(vstate->wpa_psks_keys[i])) {
                vstate_idx0 = i;
            }
        }

        if (vstate_idx0 != -1 && vstate_idx0 != -1) {
            STRSCPY_WARN(vstate->wpa_psks_keys[vstate_idx0],
                         vconf->wpa_psks_keys[vconf_idx0]);
        }
    }

    /* State schema doesn't differentiate between requested
     * network configuration and established link properties
     * when it comes to crypto parameters. Until that is
     * fixed, to avoid Config/State mismatches that could
     * confuse the controller, align them if they are
     * determined to be "good".
     */
    if (is_sta && vif_is_gen3(vconf)) {
        const bool pw_match = (vstate->wpa_pairwise_ccmp && vconf->wpa_pairwise_ccmp)
                           || (vstate->wpa_pairwise_tkip && vconf->wpa_pairwise_tkip)
                           || (vstate->rsn_pairwise_ccmp && vconf->rsn_pairwise_ccmp)
                           || (vstate->rsn_pairwise_tkip && vconf->rsn_pairwise_tkip);

        if (pw_match) {
            vstate->wpa_pairwise_ccmp = vconf->wpa_pairwise_ccmp;
            vstate->wpa_pairwise_tkip = vconf->wpa_pairwise_tkip;
            vstate->rsn_pairwise_ccmp = vconf->rsn_pairwise_ccmp;
            vstate->rsn_pairwise_tkip = vconf->rsn_pairwise_tkip;
        }

        const bool akm_match = schema_vstate_vconf_akm_intersect(vstate, vconf);
        if (akm_match) {
            if (!WARN_ON(sizeof(vstate->wpa_key_mgmt) != sizeof(vconf->wpa_key_mgmt))) {
                memcpy(vstate->wpa_key_mgmt,
                       vconf->wpa_key_mgmt,
                       sizeof(vconf->wpa_key_mgmt));
            }
        }
    }

    if (vif_is_gen2(vconf) || vif_is_gen1(vconf)) {
        schema_vstate_gen3_to_gen2(vstate);
    }

    if (vif_is_gen1(vconf)) {
        schema_vstate_gen2_to_gen1(vstate);
        schema_vstate_gen1_sync_to_vconf(vstate, vconf);
    }
}
