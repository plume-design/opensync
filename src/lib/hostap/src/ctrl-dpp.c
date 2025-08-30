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

/* libc */
#include <errno.h>
#include <string.h>
#include <net/if.h>
#include <linux/un.h>
#include <linux/limits.h>
#include <assert.h>

/* other */
#include <ev.h>
#include <wpa_ctrl.h>

/* opensync */
#include <util.h>
#include <schema.h>
#include <target.h>
#include <opensync-ctrl.h>
#include <opensync-wpas.h>
#include <opensync-hapd.h>

#include "memutil.h"

#define MODULE_ID LOG_MODULE_ID_CTRL

#define EV(x) strchomp(strdupa(x), " ")

#ifndef DPP_CLI_UNSUPPORTED
#define DPP_CLI_UNSUPPORTED "not supported"
#endif
#ifndef DPP_EVENT_CHIRP_RX
#define DPP_EVENT_CHIRP_RX DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_AUTH_SUCCESS
#define DPP_EVENT_AUTH_SUCCESS DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_CONFOBJ_SSID
#define DPP_EVENT_CONFOBJ_SSID DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_CONNECTOR
#define DPP_EVENT_CONNECTOR DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_C_SIGN_KEY
#define DPP_EVENT_C_SIGN_KEY DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_NET_ACCESS_KEY
#define DPP_EVENT_NET_ACCESS_KEY DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_CONF_REQ_RX
#define DPP_EVENT_CONF_REQ_RX DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_CONF_RECEIVED
#define DPP_EVENT_CONF_RECEIVED DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_REQ_RX
#define DPP_EVENT_REQ_RX DPP_CLI_UNSUPPORTED
#endif
#ifndef DPP_EVENT_CONF_SENT
#define DPP_EVENT_CONF_SENT DPP_CLI_UNSUPPORTED
#endif

struct ctrl_dpp_bi {
    struct ctrl_dpp_bi *next;
    int peer;
    int own;
    char uuid[36 + 1];
};

static struct ctrl_dpp_bi *g_ctrl_dpp_bi;

static struct ctrl_dpp_bi *
ctrl_dpp_bi_lookup(int peer, int own)
{
    struct ctrl_dpp_bi *i;
    if (peer == -1 && own == -1)
        return NULL;
    for (i = g_ctrl_dpp_bi; i; i = i->next)
        if ((peer == -1 || peer == i->peer) &&
            (own == -1 || own == i->own))
            return i;
    return NULL;
}

static void
ctrl_dpp_bi_set(int peer, int own, const char *uuid)
{
    struct ctrl_dpp_bi *bi = ctrl_dpp_bi_lookup(peer, own);
    LOGD("bi cache: peer=%d own=%d uuid=%s bi=%p", peer, own, uuid, bi);
    if (!bi) {
        bi = MALLOC(sizeof(*bi));
        bi->peer = peer;
        bi->own = own;
        bi->next = g_ctrl_dpp_bi;
        g_ctrl_dpp_bi = bi;
        STRSCPY_WARN(bi->uuid, uuid);
    }
    WARN_ON(strcmp(bi->uuid, uuid));
}

static const char *
ctrl_dpp_bi_get_uuid(int peer, int own)
{
    struct ctrl_dpp_bi *bi = ctrl_dpp_bi_lookup(peer, own);
    return bi ? bi->uuid : NULL;
}

static void
ctrl_dpp_bi_flush(void)
{
    struct ctrl_dpp_bi *next;
    struct ctrl_dpp_bi *i;
    LOGD("bi cache: flush");
    for (i = g_ctrl_dpp_bi; i; i = next) {
        next = i->next;
        FREE(i);
    }
    g_ctrl_dpp_bi = NULL;
}

static const char *
ctrl_dpp_conf(const char *schema)
{
    if (!strcmp(schema, "sta-dpp-sae")) return "sta-sae-dpp";
    if (!strcmp(schema, "sta-dpp-psk-sae")) return "sta-psk-sae-dpp";
    if (!strcmp(schema, "ap-dpp-sae")) return "ap-sae-dpp";
    if (!strcmp(schema, "ap-dpp-psk-sae")) return "ap-psk-sae-dpp";
    return schema;
}

static int
ctrl_dpp_set_configurator(struct ctrl *ctrl,
                          const struct schema_DPP_Config *config,
                          const int own_bi_idx,
                          const int peer_uri_idx)
{
    const char *cmd;
    bool ok;
    int idx;

    if (!config->configurator_key_curve_exists) return -1;
    if (!config->configurator_key_hex_exists) return -1;

    cmd = strfmta("DPP_CONFIGURATOR_ADD curve=%s key=%s",
                  config->configurator_key_curve,
                  config->configurator_key_hex);

    ok = ctrl_request_int(ctrl, cmd, &idx);
    if (!ok) return -1;

    cmd = strfmta("SET dpp_configurator_params conf=%s ssid=%s configurator=%d",
                  ctrl_dpp_conf(config->configurator_conf_role),
                  config->configurator_conf_ssid_hex,
                  idx);

    if (config->configurator_conf_psk_hex_exists)
        cmd = strfmta("%s pass=%s", cmd, config->configurator_conf_psk_hex);

    ok = ctrl_request_ok(ctrl, cmd);
    if (!ok) return -1;

    return idx;
}

static int
ctrl_dpp_set_own_bi(struct ctrl *ctrl,
                    const struct schema_DPP_Config *config)
{
    const char *cmd;
    bool ok;
    int idx;

    if (!config->own_bi_key_curve_exists) return -1;
    if (!config->own_bi_key_hex_exists) return -1;

    cmd = strfmta("DPP_BOOTSTRAP_GEN type=qrcode curve=%s key=%s",
                  config->own_bi_key_curve,
                  config->own_bi_key_hex);
    ok = ctrl_request_int(ctrl, cmd, &idx);
    if (!ok) return -1;

    return idx;
}

static int
ctrl_dpp_set_peer_uri(struct ctrl *ctrl,
                      const struct schema_DPP_Config *config)
{
    const char *cmd;
    bool ok;
    int idx;

    if (!config->peer_bi_uri_exists) return -1;

    cmd = strfmta("DPP_QR_CODE %s", config->peer_bi_uri);
    ok = ctrl_request_int(ctrl, cmd, &idx);
    if (!ok) return -1;

    return idx;
}

static int
ctrl_dpp_listen(struct ctrl *ctrl,
                const struct schema_DPP_Config *config)
{
    const char *cmd;
    char reply[1024];
    size_t reply_len;
    bool ok;
    int err;
    int freq;

    /* This isn't perfect, but should be good enough to get
     * the freq. In fact, sometimes hostapd may not know the
     * radio works on another interfaces on proprietary
     * drivers, so the freq passed here needs to match what
     * hostapd thinks anyway.
     *
     * This does not cover non-associated wpas case.
     */
    cmd = "STATUS";
    reply_len = sizeof(reply);
    err = ctrl_request(ctrl, cmd, strlen(cmd), reply, &reply_len);
    if (err) return -1;

    freq = atoi(ini_geta(reply, "freq") ?: "-1");

    /* hostapd checks if dpp_listen freq is within AP oper channel.
     * But some hostapd drivers don't report that at all,
     * in which case passing any non-zero freq to dpp_listen
     * will work.
     */
    if (freq == 0) freq = 1;

    cmd = strfmta("DPP_LISTEN %d", freq);
    ok = ctrl_request_ok(ctrl, cmd);
    if (!ok) return -1;

    return 0;
}

static int
ctrl_dpp_auth_init(struct ctrl *ctrl,
                   const struct schema_DPP_Config *config,
                   const int conf_idx,
                   const int own_bi_idx,
                   const int peer_uri_idx)
{
    const char *cmd;

    if (!config->configurator_conf_role_exists) return -1;
    if (!config->configurator_conf_ssid_hex_exists) return -1;

    cmd = strfmta("DPP_AUTH_INIT peer=%d conf=%s ssid=%s configurator=%d",
                  peer_uri_idx,
                  ctrl_dpp_conf(config->configurator_conf_role),
                  config->configurator_conf_ssid_hex,
                  conf_idx);

    if (config->configurator_conf_psk_hex_exists)
        cmd = strfmta("%s pass=%s", cmd, config->configurator_conf_psk_hex);

    if (own_bi_idx >= 0)
        cmd = strfmta("%s own_bi=%d", cmd, own_bi_idx);

    if (!ctrl_request_ok(ctrl, cmd))
        return -1;

    return 0;
}

static int
ctrl_dpp_chirp(struct ctrl *ctrl,
               const struct schema_DPP_Config *config,
               const int own_bi_idx)
{
    const char *cmd = strfmta("DPP_CHIRP own=%d iter=10", own_bi_idx);
    return ctrl_request_ok(ctrl, cmd) ? 0 : -1;
}

static bool
ctrl_dpp_conf_has_ifname(struct ctrl *ctrl, const struct schema_DPP_Config *config)
{
    int i;
    for (i = 0; i < config->ifnames_len; i++)
        if (!strcmp(ctrl->bss, config->ifnames[i]))
            return true;
    return false;
}

static int
ctrl_dpp_config_each(struct ctrl *ctrl, void *ptr)
{
    const struct schema_DPP_Config *config = ptr;
    int own_bi_idx;
    int peer_uri_idx;
    int conf_idx;
    int err = 0;

    if (!ctrl->wpa)
        return 0;

    own_bi_idx = ctrl_dpp_set_own_bi(ctrl, config);
    peer_uri_idx = ctrl_dpp_set_peer_uri(ctrl, config);
    conf_idx = ctrl_dpp_set_configurator(ctrl, config, own_bi_idx, peer_uri_idx);
    ctrl_dpp_bi_set(peer_uri_idx, own_bi_idx, config->_uuid.uuid);

    if (!strcmp(config->auth, SCHEMA_CONSTS_DPP_INIT_ON_ANNOUNCE))
        err |= WARN_ON(ctrl_dpp_listen(ctrl, config));

    if (!strcmp(config->auth, SCHEMA_CONSTS_DPP_INIT_NOW))
        if (ctrl_dpp_conf_has_ifname(ctrl, config))
            err |= WARN_ON(ctrl_dpp_auth_init(ctrl, config, conf_idx, own_bi_idx, peer_uri_idx));

    if (!strcmp(config->auth, SCHEMA_CONSTS_DPP_RESPOND_ONLY))
        err |= WARN_ON(ctrl_dpp_listen(ctrl, config));

    if (!strcmp(config->auth, SCHEMA_CONSTS_DPP_CHIRP))
        err |= WARN_ON(ctrl_dpp_chirp(ctrl, config, own_bi_idx));

    return err;
}

static int
ctrl_dpp_clear_each(struct ctrl *ctrl, void *ptr)
{
    int err = 0;

    if (!ctrl->wpa)
        return 0;

    err |= WARN_ON(!ctrl_request_ok(ctrl, "DPP_CONFIGURATOR_REMOVE *"));
    err |= WARN_ON(!ctrl_request_ok(ctrl, "DPP_BOOTSTRAP_REMOVE *"));
    err |= WARN_ON(!ctrl_request_ok(ctrl, "DPP_STOP_LISTEN"));
    err |= WARN_ON(!ctrl_request_ok(ctrl, "DPP_STOP_CHIRP"));
    return err;
}

void
ctrl_dpp_init(struct ctrl_dpp *dpp)
{
    memset(dpp, 0, sizeof(*dpp));
    STRSCPY_WARN(dpp->conf_tx_pkhash, "0000000000000000000000000000000000000000000000000000000000000000");
}

static enum target_dpp_conf_akm
ctrl_dpp_akm_str2enum(const char *s)
{
    if (!strcmp(s, "dpp")) return TARGET_DPP_CONF_DPP;
    if (!strcmp(s, "psk")) return TARGET_DPP_CONF_PSK;
    if (!strcmp(s, "sae")) return TARGET_DPP_CONF_SAE;
    if (!strcmp(s, "psk+sae")) return TARGET_DPP_CONF_PSK_SAE;
    if (!strcmp(s, "dpp+sae")) return TARGET_DPP_CONF_DPP_SAE;
    if (!strcmp(s, "dpp+psk+sae")) return TARGET_DPP_CONF_DPP_PSK_SAE;
    if (!strcmp(s, "dot1x")) return TARGET_DPP_CONF_UNKNOWN;
    if (!strcmp(s, "??")) return TARGET_DPP_CONF_UNKNOWN;
    return TARGET_DPP_CONF_UNKNOWN;
}

static bool
ctrl_dpp_conf_rx_ready(struct ctrl *ctrl)
{
    struct ctrl_dpp *dpp = &ctrl->dpp;
    return strlen(dpp->conf_rx_ssid_hex) > 1 &&
           strlen(dpp->conf_rx_connector) > 1 &&
           strlen(dpp->conf_rx_netaccesskey_hex) > 1 &&
           strlen(dpp->conf_rx_csign_hex) > 1;
}

static void
ctrl_dpp_conf_rx_cb(struct ctrl *ctrl)
{
    struct ctrl_dpp *dpp = &ctrl->dpp;
    struct target_dpp_conf_network arg = {
        .ifname = ctrl->bss,
        .ssid_hex = dpp->conf_rx_ssid_hex,
        .dpp_connector = dpp->conf_rx_connector,
        .dpp_netaccesskey_hex = dpp->conf_rx_netaccesskey_hex,
        .dpp_csign_hex = dpp->conf_rx_csign_hex,
        .akm = ctrl_dpp_akm_str2enum(dpp->conf_rx_akm),
        .psk_hex = dpp->conf_rx_psk_hex,
        .config_uuid = dpp->conf_uuid,
    };

    if (!ctrl->dpp_conf_received)
        return;

    if (!ctrl_dpp_conf_rx_ready(ctrl))
        return;

    ctrl->dpp_conf_received(ctrl, &arg);
    ctrl_dpp_init(dpp);
}

int
ctrl_dpp_cb(struct ctrl *ctrl, int level, const char *buf, size_t len)
{
    struct ctrl_dpp *dpp = &ctrl->dpp;
    char *args = strdupa(buf);
    char *kv;
    const char *sha256_hash = NULL;
    const char *mac = NULL;
    const char *uuid;
    const char *k;
    const char *v;
    const char *event = strsep(&args, " ") ?: "_nope_";
    int bi_peer = -1;
    int bi_own = -1;

    if (!strcmp(event, EV(DPP_EVENT_CHIRP_RX))) {
        while ((kv = strsep(&args, " "))) {
            if ((k = strsep(&kv, "=")) && (v = strsep(&kv, ""))) {
                if (!strcmp(k, "src"))
                    mac = v;
                if (!strcmp(k, "hash"))
                    sha256_hash = v;
            }
        }

        LOGI("%s: dpp: chirp received: sta=%s hash=%s", ctrl->bss, mac, sha256_hash ?: "");
        if (ctrl->dpp_chirp_received) {
            const struct target_dpp_chirp_obj chirp = {
                .ifname = ctrl->bss,
                .mac_addr = mac,
                .sha256_hex = sha256_hash
            };
            ctrl->dpp_chirp_received(ctrl, &chirp);
        }
        return 0;
    }

    if (!strcmp(event, EV(DPP_EVENT_AUTH_SUCCESS))) {
        while ((kv = strsep(&args, " "))) {
            if ((k = strsep(&kv, "=")) && (v = strsep(&kv, ""))) {
                if (!strcmp(k, "own"))
                    bi_own = atoi(v);
                if (!strcmp(k, "peer"))
                    bi_peer = atoi(v);
                if (!strcmp(k, "pkhash"))
                    sha256_hash = v;
            }
        }

        if (sha256_hash)
            STRSCPY_WARN(dpp->conf_tx_pkhash, sha256_hash);

        uuid = ctrl_dpp_bi_get_uuid(bi_peer, bi_own);
        if (uuid)
            STRSCPY_WARN(dpp->conf_uuid, uuid);

        LOGI("%s: dpp: auth success received: peer=%d own=%d uuid=%s pkhash=%s",
             ctrl->bss, bi_peer, bi_own, uuid ?: "", sha256_hash);
        dpp->auth_success = 1;
        return 0;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONF_RECEIVED))) {
        LOGI("%s: dpp: conf received", ctrl->bss);
        if (dpp->auth_success && ctrl->dpp_conf_received)
            dpp->auth_success = 0;
        return 0;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONFOBJ_SSID))) {
        if (ascii2hex(args, dpp->conf_rx_ssid_hex, sizeof(dpp->conf_rx_ssid_hex))) {
            LOGI("%s: dpp: conf received: ssid=%s hex=%s", ctrl->bss, args, dpp->conf_rx_ssid_hex);
            ctrl_dpp_conf_rx_cb(ctrl);
        }
        return 0;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONFOBJ_PASS))) {
        LOGI("%s: dpp: conf received: pass=%s", ctrl->bss, args);
        STRSCPY_WARN(dpp->conf_rx_psk_hex, args);
        ctrl_dpp_conf_rx_cb(ctrl);
        return 0;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONFOBJ_AKM))) {
        LOGI("%s: dpp: conf received: akm=%s", ctrl->bss, args);
        STRSCPY_WARN(dpp->conf_rx_akm, args);
        ctrl_dpp_conf_rx_cb(ctrl);
        return 0;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONNECTOR))) {
        LOGI("%s: dpp: conf received: connector=%s", ctrl->bss, args);
        STRSCPY_WARN(dpp->conf_rx_connector, args);
        ctrl_dpp_conf_rx_cb(ctrl);
        return 0;
    }

    if (!strcmp(event, EV(DPP_EVENT_C_SIGN_KEY))) {
        LOGI("%s: dpp: conf received: csignkey=%s", ctrl->bss, args);
        STRSCPY_WARN(dpp->conf_rx_csign_hex, args);
        ctrl_dpp_conf_rx_cb(ctrl);
        return 0;
    }

    if (!strcmp(event, EV(DPP_EVENT_NET_ACCESS_KEY))) {
        LOGI("%s: dpp: conf received: netaccesskey=%s", ctrl->bss, args);
        STRSCPY_WARN(dpp->conf_rx_netaccesskey_hex, args);
        ctrl_dpp_conf_rx_cb(ctrl);
        return 0;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONF_REQ_RX))) {
        LOGI("%s: dpp conf request received", ctrl->bss);
        if (!dpp->conf_req_rx) {
            dpp->conf_req_rx = 1;
            while ((kv = strsep(&args, " "))) {
                if ((k = strsep(&kv, "=")) && (v = strsep(&kv, ""))) {
                    if (!strcmp(k, "src"))
                        mac = v;
                }
            }
            STRSCPY_WARN(dpp->conf_tx_sta, mac);
            LOGI("%s: dpp conf request received from: %s", ctrl->bss, dpp->conf_tx_sta);
        }
        else {
            LOGW("%s: dpp conf already in progress, igorning", ctrl->bss);
        }
        return 0;
    }

    if (!strcmp(event, EV(DPP_EVENT_CONF_SENT))) {
        LOGI("%s: dpp conf sent event received", ctrl->bss);
        if (ctrl->dpp_conf_sent && dpp->conf_req_rx) {
            const struct target_dpp_conf_enrollee enrollee = {
                .ifname = ctrl->bss,
                .sta_mac_addr = dpp->conf_tx_sta,
                .sta_netaccesskey_sha256_hex = dpp->conf_tx_pkhash,
                .config_uuid = dpp->conf_uuid,
            };
            ctrl->dpp_conf_sent(ctrl, &enrollee);
            LOGI("%s: dpp conf sent to: %s", ctrl->bss, dpp->conf_tx_sta);
            ctrl_dpp_init(dpp);
        }
        return 0;
    }

    return -1;
}

bool
ctrl_dpp_config(const struct schema_DPP_Config **config)
{
    int err = 0;

    wpas_each(ctrl_dpp_clear_each, NULL);
    hapd_each(ctrl_dpp_clear_each, NULL);
    ctrl_dpp_bi_flush();

    if (!*config)
        return true;

    for (; *config; config++) {
        err |= wpas_each(ctrl_dpp_config_each, (void *)*config);
        err |= hapd_each(ctrl_dpp_config_each, (void *)*config);
    }

    if (err) {
        wpas_each(ctrl_dpp_clear_each, NULL);
        hapd_each(ctrl_dpp_clear_each, NULL);
        return false;
    }

    return true;
}
