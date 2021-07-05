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

#ifndef OPENSYNC_CTRL_H_INCLUDED
#define OPENSYNC_CTRL_H_INCLUDED

struct hapd;
struct wpas;

struct ctrl_dpp {
    char conf_rx_ssid_hex[65];
    char conf_rx_connector[1025];
    char conf_rx_psk_hex[65];
    char conf_rx_csign_hex[513];
    char conf_rx_netaccesskey_hex[513];
    char conf_rx_akm[513];
    char conf_tx_sta[18];
    char conf_tx_pkhash[64+1];
    char conf_uuid[36+1];
    int conf_req_rx;
    int auth_success;
};

struct ctrl {
    char sockpath[UNIX_PATH_MAX];
    char sockdir[UNIX_PATH_MAX];
    char bss[IFNAMSIZ];
    char reply[4096];
    void (*cb)(struct ctrl *ctrl, int level, const char *buf, size_t len);
    void (*opened)(struct ctrl *ctrl);
    void (*closed)(struct ctrl *ctrl);
    void (*overrun)(struct ctrl *ctrl);
    void (*dpp_conf_sent)(struct ctrl *ctrl, const struct target_dpp_conf_enrollee *enrollee);
    void (*dpp_conf_received)(struct ctrl *ctrl, const struct target_dpp_conf_network *conf);
    void (*dpp_chirp_received)(struct ctrl *ctrl, const struct target_dpp_chirp_obj *chirp);
    struct hapd *hapd;
    struct wpas *wpas;
    struct wpa_ctrl *wpa;
    struct ctrl_dpp dpp;
    unsigned int ovfl;
    size_t reply_len;
    ev_timer watchdog;
    ev_timer retry;
    ev_stat stat;
    ev_io io;
};

int ctrl_enable(struct ctrl *ctrl);
int ctrl_disable(struct ctrl *ctrl);
int ctrl_running(struct ctrl *ctrl);
int ctrl_request(struct ctrl *ctrl, const char *cmd, size_t cmd_len, char *reply, size_t *reply_len);
bool ctrl_request_ok(struct ctrl *ctrl, const char *cmd);
bool ctrl_request_int(struct ctrl *ctrl, const char *cmd, int *ret);

#endif /* OPENSYNC_CTRL_H_INCLUDED */
