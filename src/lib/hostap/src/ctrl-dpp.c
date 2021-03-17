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

/* opensync */
#include <util.h>
#include <schema.h>
#include <opensync-ctrl.h>
#include <opensync-wpas.h>
#include <opensync-hapd.h>

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

    ok = ctrl_request_ok(ctrl, "SET dpp_configurator_connectivity 1");
    if (!ok) return -1;

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
ctrl_dpp_config_match(struct ctrl *ctrl, const struct schema_DPP_Config *conf)
{
    int i;

    for (i = 0; i < conf->ifnames_len; i++)
        if (!strcmp(conf->ifnames[i], ctrl->bss))
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

    if (!ctrl_dpp_config_match(ctrl, config))
        return 0;

    own_bi_idx = ctrl_dpp_set_own_bi(ctrl, config);
    peer_uri_idx = ctrl_dpp_set_peer_uri(ctrl, config);
    conf_idx = ctrl_dpp_set_configurator(ctrl, config, own_bi_idx, peer_uri_idx);

    if (!strcmp(config->auth, SCHEMA_CONSTS_DPP_INIT_ON_ANNOUNCE))
        err |= WARN_ON(ctrl_dpp_listen(ctrl, config));

    if (!strcmp(config->auth, SCHEMA_CONSTS_DPP_INIT_NOW))
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
    err |= WARN_ON(!ctrl_request_ok(ctrl, "DPP_CONFIGURATOR_REMOVE *"));
    err |= WARN_ON(!ctrl_request_ok(ctrl, "DPP_BOOTSTRAP_REMOVE *"));
    err |= WARN_ON(!ctrl_request_ok(ctrl, "DPP_STOP_LISTEN"));
    err |= WARN_ON(!ctrl_request_ok(ctrl, "DPP_STOP_CHIRP"));
    return err;
}

bool
ctrl_dpp_config(const struct schema_DPP_Config *config)
{
    int err = 0;

    wpas_each(ctrl_dpp_clear_each, NULL);
    hapd_each(ctrl_dpp_clear_each, NULL);

    /* TODO: This might actually require a dpp_listen even
     * without any config because we want to receive chirps
     * *always*, and for that to work hw rx filters need to
     * be adjusted. As such dpp_listen would be relative
     * nice way to relay that to the driver through
     * hostap/wpas.
     */
    if (!config)
        return true;

    err |= wpas_each(ctrl_dpp_config_each, (void *)config);
    err |= hapd_each(ctrl_dpp_config_each, (void *)config);

    if (err) {
        wpas_each(ctrl_dpp_clear_each, NULL);
        hapd_each(ctrl_dpp_clear_each, NULL);
        return false;
    }

    return true;
}
