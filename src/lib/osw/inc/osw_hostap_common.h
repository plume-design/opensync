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

#ifndef OSW_HOSTAP_COMMON_H_INCLUDED
#define OSW_HOSTAP_COMMON_H_INCLUDED

/* Definitions of struct fields for config/state */
#define OSW_HOSTAP_CONF_DECL_INT(name) \
    OSW_HOSTAP_CONF_DECL_OPTIONAL(int, name)

#define OSW_HOSTAP_CONF_DECL_BOOL(name) \
    OSW_HOSTAP_CONF_DECL_OPTIONAL(bool, name)

#define OSW_HOSTAP_CONF_DECL_STR(name, len) \
    OSW_HOSTAP_CONF_DECL_OPTIONAL_ARRAY(char, name, len)

#define OSW_HOSTAP_CONF_DECL_OPTIONAL(__type, __name) \
    __type __name; \
    bool __name ## _exists

#define OSW_HOSTAP_CONF_DECL_OPTIONAL_ARRAY(__type, __name, __len) \
    __type __name[__len]; \
    bool __name ## _exists

/* Setters for config/state struct fields */
#define OSW_HOSTAP_CONF_SET_VAL(var, val) do { \
    var = val; \
    var ## _exists = true; \
    } while(0)

#define OSW_HOSTAP_CONF_SET_BUF(dst, src) do { \
    STRSCPY_WARN(dst, src); \
    dst ## _exists = true; \
    } while(0)

#define OSW_HOSTAP_CONF_UNSET(var) do { \
    var ## _exists = false; \
    } while(0)

/* Quoted */
#define OSW_HOSTAP_CONF_SET_BUF_Q(dst, src) do { \
    STRSCPY_WARN(dst, "\""); \
    strscat(dst, src, sizeof(dst) - 1); \
    STRSCAT(dst, "\""); \
    dst ## _exists = true; \
    } while(0)

/* Quoted with length */
#define OSW_HOSTAP_CONF_SET_BUF_Q_LEN(dst, src, len) do { \
    STRSCPY_WARN(dst, "\""); \
    strscat(dst, src, len + 2); \
    STRSCAT(dst, "\""); \
    dst ## _exists = true; \
    } while(0)

/* Helpers to generate config files */
#define CONF_INIT(buf) \
    char *_pbuf = &(buf[0]); \
    char **__buf = &_pbuf; \
    char *__indent = ""; \
    size_t _len = (sizeof(buf)/sizeof(buf[0])); \
    memset(buf, 0, _len); \
    size_t *__len = &_len

#define CONF_APPEND(ARG, FMT) \
    if (conf->ARG ## _exists == false) {} else { \
    csnprintf(__buf, __len, "%s"# ARG"="FMT, __indent, conf->ARG); \
    csnprintf(__buf, __len, "\n"); }

#define OSW_HOSTAP_CONF_NETWORK_BLOCK_START() \
    __indent = "\t"; \
    csnprintf(__buf, __len, "network={\n"); \
    { /* Open a section here to warn if block is not closed */

#define OSW_HOSTAP_CONF_NETWORK_BLOCK_END() \
    __indent = ""; \
    csnprintf(__buf, __len, "}\n"); \
    } /* Close a section here to warn if used without START */

#define CONF_FINI() do { \
    assert(_len != 1); \
    } while (0)

/* The below macro is complex to be able to pass
 * the whole SRC buffer to __buf variable. To do
 * it just pass an empty string as OPT. ("") */
#define __STATE_GET(SRC, OPT) \
    const char *__buf = NULL;  \
    do { \
    if (strlen(OPT) == 0) { \
        __buf = SRC;      } \
    if (SRC == NULL) {  \
        __buf = "";  }  \
    if (__buf == NULL) {                    \
        __buf = ini_geta(SRC, OPT) ?: ""; } \
    } while(0)

#define STATE_GET_BOOL(DST, SRC, OPT) do { \
    __STATE_GET(SRC, OPT); \
    if (__buf) { if (atoi(__buf) == 1) {DST = true;} else {DST = false;}; } \
    } while(0)

#define STATE_GET_INT(DST, SRC, OPT) do { \
    __STATE_GET(SRC, OPT); \
   /* Dumb, but ekh... */ \
    if (__buf) { DST = atoi(__buf); } \
    } while(0)

#define STATE_GET_BY_FN(DST, SRC, OPT, FN) do { \
    __STATE_GET(SRC, OPT); \
    if (__buf) { FN(__buf, &DST); } \
    } while(0)

#define OSW_HOSTAP_CONF_WPA_KEY_MGMT_MAX_LEN 128

enum osw_hostap_conf_auth_algs {
    OSW_HOSTAP_CONF_AUTH_ALG_OPEN = 0x01,
    OSW_HOSTAP_CONF_AUTH_ALG_SHARED = 0x02,
    OSW_HOSTAP_CONF_AUTH_ALG_LEAP = 0x04,
    OSW_HOSTAP_CONF_AUTH_ALG_FT = 0x08,
    OSW_HOSTAP_CONF_AUTH_ALG_SAE = 0x10,
    OSW_HOSTAP_CONF_AUTH_ALG_FILS = 0x20,
    OSW_HOSTAP_CONF_AUTH_ALG_FILS_SK_PFS = 0x40,
};

enum osw_hostap_conf_pmf {
    OSW_HOSTAP_CONF_PMF_DISABLED = 0,
    OSW_HOSTAP_CONF_PMF_OPTIONAL,
    OSW_HOSTAP_CONF_PMF_REQUIRED,
};

enum osw_hostap_conf_wpa {
    OSW_HOSTAP_CONF_WPA_WPA = 0x01,
    OSW_HOSTAP_CONF_WPA_RSN = 0x02,
};

enum osw_hostap_conf_chanwidth {
    OSW_HOSTAP_CONF_CHANWIDTH_20MHZ_40MHZ = 0,
    OSW_HOSTAP_CONF_CHANWIDTH_80MHZ,
    OSW_HOSTAP_CONF_CHANWIDTH_160MHZ,
    OSW_HOSTAP_CONF_CHANWIDTH_80P80MHZ,
    OSW_HOSTAP_CONF_CHANWIDTH_320MHZ = 9,
};

enum osw_hostap_conf_acl_policy {
    OSW_HOSTAP_CONF_ACL_DENY_LIST = 0,
    OSW_HOSTAP_CONF_ACL_ACCEPT_LIST,
    OSW_HOSTAP_CONF_ACL_USE_EXT_RADIUS
};

struct osw_hostap_conf_capab {
    bool rxkh_supported;
    bool multi_ap_supported;
};

struct osw_hostap_conf_ap_state_bufs {
    const char *config;
    const char *get_config;
    const char *status;
    const char *mib;
    const char *wps_get_status;
    const char *wpa_psk_file;
    const char *show_neighbor;
};

struct osw_hostap_conf_sta_state_bufs {
    const char *config;
    const char *status;
    const char *list_networks;
    const char *mib;
    const char *bridge_if_name;
};

enum osw_hostap_conf_chanwidth
osw_hostap_conf_chwidth_from_osw(enum osw_channel_width width);

enum osw_channel_width
osw_hostap_conf_chwidth_to_osw(enum osw_hostap_conf_chanwidth width);

struct osw_drv_phy_config*
osw_hostap_conf_phy_lookup(const struct osw_drv_conf *drv_conf,
                           const char *phy_name);

struct osw_drv_vif_config*
osw_hostap_conf_vif_lookup(const struct osw_drv_phy_config *phy,
                           const char *vif_name);

enum osw_hostap_conf_pmf
osw_hostap_conf_pmf_from_osw(const struct osw_wpa *wpa);

enum osw_pmf
osw_hostap_conf_pmf_to_osw(enum osw_hostap_conf_pmf pmf);

enum osw_hostap_conf_auth_algs
osw_hostap_conf_auth_algs_from_osw(const struct osw_wpa *wpa);

enum osw_hostap_conf_wpa
osw_hostap_conf_wpa_from_osw(const struct osw_wpa *wpa);

char *
osw_hostap_conf_wpa_key_mgmt_from_osw(const struct osw_wpa *wpa);

char *
osw_hostap_conf_pairwise_from_osw(const struct osw_wpa *wpa);

char *
osw_hostap_conf_proto_from_osw(const struct osw_wpa *wpa);

bool
osw_hostap_util_proto_to_osw(const char *proto,
                             struct osw_wpa *wpa);
bool
osw_hostap_util_pairwise_to_osw(const char *pairwise,
                                struct osw_wpa *wpa);
bool
osw_hostap_util_ssid_to_osw(const char *ssid_str,
                            struct osw_ssid *ssid);
bool
osw_hostap_util_sta_freq_to_channel(const char *freq,
                                    struct osw_channel *channel);
bool
osw_hostap_util_ieee80211w_to_osw(const char *ieee80211w,
                                  struct osw_wpa *osw_wpa);
bool
osw_hostap_util_wpa_key_mgmt_to_osw(const char *wpa_key_mgmt,
                                    struct osw_wpa *osw_wpa);
bool
osw_hostap_util_sta_state_to_osw(const char *wpa_state,
                                 enum osw_drv_vif_state_sta_link_status *status);
bool
osw_hostap_util_key_mgmt_to_osw(const char *key_mgmt,
                                struct osw_wpa *wpa);
bool
osw_hostap_util_unquote(const char *original,
                        char *unquoted);

#endif /* OSW_HOSTAP_COMMON_H_INCLUDED */

