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

#include <mqueue.h>
#include <string.h>
#include <errno.h>

#include "target.h"
#include "util.h"

#define QUEUE_MSG_SIZE 10240

typedef struct {
    char ifname[BSAL_IFNAME_LEN];
    bsal_client_config_t conf;

    ds_dlist_node_t node;
} client_config_t;

typedef struct {
    os_macaddr_t hwaddr;
    bsal_client_info_t info;
    ds_dlist_t configs;

    ds_dlist_node_t node;
} client_t;

static int g_bsal_in = -1;
static int g_bsal_out = -1;
static ev_io g_bsal_in_watcher;
static bsal_event_cb_t g_event_cb = NULL;
static ds_dlist_t g_clients = DS_DLIST_INIT(client_t, node);

static bsal_rssi_change_t
bsal_rssi_change_from_str(const char *type)
{
    if (strcmp(type, "unchanged") == 0) {
        return BSAL_RSSI_UNCHANGED;
    }
    else if (strcmp(type, "higher") == 0) {
        return BSAL_RSSI_HIGHER;
    }
    else if (strcmp(type, "lower") == 0) {
        return BSAL_RSSI_LOWER;
    }
    else {
        LOGE("BSAL: Unknown bsal_rssi_change: '%s'", type);
        exit(1);
    }
}

static void
parse_hwaddr(const char *mac,
             os_macaddr_t *hwaddr)
{
    sscanf(mac, "%"SCNx8":%"SCNx8":%"SCNx8":%"SCNx8":%"SCNx8":%"SCNx8,
                &hwaddr->addr[0], &hwaddr->addr[1], &hwaddr->addr[2], &hwaddr->addr[3], &hwaddr->addr[4], &hwaddr->addr[5]);
}

static
client_t* get_client(const os_macaddr_t *hwaddr)
{
    client_t *client;

    ds_dlist_foreach(&g_clients, client)
    {
        if (memcmp(&client->hwaddr, hwaddr, sizeof(client->hwaddr)))
            continue;

        return client;
    }

    return NULL;
}

static bool
handle_probe_req(const char *msg)
{
    bsal_ev_probe_req_t *probe_event;
    bsal_event_t event;
    os_macaddr_t hwaddr;
    const char *token;
    char *args = strdupa(msg);

    if (strstr(msg, "PROBE") != msg)
        return false;

    memset(&event, 0, sizeof(event));
    event.type = BSAL_EVENT_PROBE_REQ;
    probe_event = &event.data.probe_req;

    while((token = strsep(&args, " "))) {
        char *kv = strdupa(token);
        const char *k = strsep(&kv, "=");
        const char *v = strsep(&kv, "=");

        if (!k || !v)
            continue;

        if (strcmp(k, "mac") == 0) {
            parse_hwaddr(v, &hwaddr);
            memcpy(&probe_event->client_addr, &hwaddr.addr, sizeof(probe_event->client_addr));
        }
        else if (strcmp(k, "if_name") == 0) {
            STRSCPY_WARN(event.ifname, v);
        }
        else if (strcmp(k, "snr") == 0) {
            probe_event->rssi = atoi(v);
        }
        else if (strcmp(k, "ssid") == 0) {
            probe_event->ssid_null = strcmp(v, "true") == 0;
        }
        else if (strcmp(k, "blocked") == 0) {
            probe_event->blocked = strcmp(v, "true") == 0;
        }
        else {
            LOGE("BSAL: Unknown PROBE argument: '%s'", k);
            exit(1);
        }
    }

    LOGI("BSAL: probe event sent");

    g_event_cb(&event);

    return true;
}

static bool
handle_update_client(const char *msg)
{
    os_macaddr_t hwaddr;
    client_t *client;
    const char *token;
    char *args = strdupa(msg);

    if (strstr(msg, "UPDATE_CLIENT") != msg)
        return false;

    while((token = strsep(&args, " "))) {
        char *kv = strdupa(token);
        const char *k = strsep(&kv, "=");
        const char *v = strsep(&kv, "=");

        if (!k || !v)
            continue;

        if (strcmp(k, "mac") == 0) {
            parse_hwaddr(v, &hwaddr);
            client = get_client(&hwaddr);
        }
        else if (strcmp(k, "btm") == 0) {
            client->info.is_BTM_supported = strcmp(v, "true") == 0;
        }
        else if (strcmp(k, "rrm") == 0) {
            client->info.is_RRM_supported = strcmp(v, "true") == 0;
        }
        else if (strcmp(k, "rrm_beacon_passive_mes") == 0) {
            client->info.rrm_caps.bcn_rpt_passive = strcmp(v, "true") == 0;
        }
        else if (strcmp(k, "rrm_beacon_active_mes") == 0) {
            client->info.rrm_caps.bcn_rpt_active = strcmp(v, "true") == 0;
        }
        else if (strcmp(k, "rrm_beacon_table_mes") == 0) {
            client->info.rrm_caps.bcn_rpt_table = strcmp(v, "true") == 0;
        }
        else if (strcmp(k, "assoc_ies") == 0) {
            unsigned char assoc_ies[4096];
            ssize_t assoc_ies_len;

            assoc_ies_len = hex2bin(v, strlen(v), assoc_ies, sizeof(assoc_ies));
            if (assoc_ies_len < 0) {
                LOGE("BSAL: Failed to UPDATE_CLIENT's assoc_ies to binary");
                exit(1);
            }

            memcpy(client->info.assoc_ies, &assoc_ies, assoc_ies_len);
            client->info.assoc_ies_len = assoc_ies_len;
        }
        else {
            LOGE("BSAL: Unknown UPDATE_CLIENT argument: '%s'", k);
            exit(1);
        }
    }

    if (!client) {
        LOGE("BSAL: UPDATE_CLIENT didn't look up client");
        exit(1);
    }

    LOGI("BSAL: update client");

    return true;
}

static bool
handle_connect(const char *msg)
{
    bsal_ev_connect_t *connect_event;
    bsal_event_t event;
    os_macaddr_t hwaddr;
    client_t *client;
    const char *token;
    char *args = strdupa(msg);

    if (strstr(msg, "CONNECT") != msg)
        return false;

    memset(&event, 0, sizeof(event));
    event.type = BSAL_EVENT_CLIENT_CONNECT;
    connect_event = &event.data.connect;

    while((token = strsep(&args, " "))) {
        char *kv = strdupa(token);
        const char *k = strsep(&kv, "=");
        const char *v = strsep(&kv, "=");

        if (!k || !v)
            continue;

        if (strcmp(k, "mac") == 0) {
            parse_hwaddr(v, &hwaddr);
            client = get_client(&hwaddr);
            memcpy(&connect_event->client_addr, &hwaddr.addr, sizeof(connect_event->client_addr));
        }
        else if (strcmp(k, "if_name") == 0) {
            STRSCPY_WARN(event.ifname, v);
        }
    }

    if (!client) {
        LOGE("BSAL: CONNECT didn't look up client");
        exit(1);
    }

    client->info.connected = true;

    LOGI("BSAL: connect event sent");

    g_event_cb(&event);

    return true;
}

static bool
handle_disconnect(const char *msg)
{
    bsal_ev_disconnect_t *disconnect_event;
    bsal_event_t event;
    os_macaddr_t hwaddr;
    client_t *client;
    const char *token;
    char *args = strdupa(msg);

    if (strstr(msg, "DISCONNECT") != msg)
        return false;

    memset(&event, 0, sizeof(event));
    event.type = BSAL_EVENT_CLIENT_DISCONNECT;
    disconnect_event = &event.data.disconnect;

    while((token = strsep(&args, " "))) {
        char *kv = strdupa(token);
        const char *k = strsep(&kv, "=");
        const char *v = strsep(&kv, "=");

        if (!k || !v)
            continue;

        if (strcmp(k, "mac") == 0) {
            parse_hwaddr(v, &hwaddr);
            client = get_client(&hwaddr);
            memcpy(&disconnect_event->client_addr, &hwaddr.addr, sizeof(disconnect_event->client_addr));
        }
        else if (strcmp(k, "if_name") == 0) {
            STRSCPY_WARN(event.ifname, v);
        }
        else if (strcmp(k, "reason") == 0) {
            disconnect_event->reason = atoi(v);
        }
        else if (strcmp(k, "source") == 0) {
            disconnect_event->source = strcmp(v, "local") == 0 ? BSAL_DISC_SOURCE_LOCAL : BSAL_DISC_SOURCE_REMOTE;
        }
        else if (strcmp(k, "type") == 0) {
            disconnect_event->type = strcmp(v, "deuth") == 0 ? BSAL_DISC_TYPE_DEAUTH : BSAL_DISC_TYPE_DISASSOC;
        }
        else {
            LOGE("BSAL: Unknown DISCONNECT argument: '%s'", k);
            exit(1);
        }
    }

    if (!client) {
        LOGE("BSAL: DISCONNECT didn't look up client");
        exit(1);
    }

    memset(&client->info, 0, sizeof(client->info));

    LOGI("BSAL: disconnect event sent");

    g_event_cb(&event);

    return true;
}

static bool
handle_rssi_xing(const char *msg)
{
    bsal_ev_rssi_xing_t *xing_event;
    bsal_event_t event;
    os_macaddr_t hwaddr;
    client_t *client;
    const char *token;
    char *args = strdupa(msg);

    if (strstr(msg, "RSSI_XING") != msg)
        return false;

    memset(&event, 0, sizeof(event));
    event.type = BSAL_EVENT_RSSI_XING;
    xing_event = &event.data.rssi_change;

    while((token = strsep(&args, " "))) {
        char *kv = strdupa(token);
        const char *k = strsep(&kv, "=");
        const char *v = strsep(&kv, "=");

        if (!k || !v)
            continue;

        if (strcmp(k, "mac") == 0) {
            parse_hwaddr(v, &hwaddr);
            client = get_client(&hwaddr);
            memcpy(&xing_event->client_addr, &hwaddr.addr, sizeof(xing_event->client_addr));
        }
        else if (strcmp(k, "if_name") == 0) {
            STRSCPY_WARN(event.ifname, v);
        }
        else if (strcmp(k, "snr") == 0) {
            xing_event->rssi = atoi(v);
        }
        else if (strcmp(k, "inact_xing") == 0) {
            xing_event->inact_xing = bsal_rssi_change_from_str(v);
        }
        else if (strcmp(k, "high_xing") == 0) {
            xing_event->high_xing = bsal_rssi_change_from_str(v);
        }
        else if (strcmp(k, "low_xing") == 0) {
            xing_event->low_xing = bsal_rssi_change_from_str(v);
        }
        else {
            LOGE("BSAL: Unknown RSSI_XING argument: '%s'", k);
            exit(1);
        }
    }

    if (!client->info.connected) {
        LOGE("BSAL: Cannot generate RSSI XING event for disconnected client");
        exit(1);
    }

    client->info.snr = xing_event->rssi;

    g_event_cb(&event);

    return true;
}

static void
bsal_in_cb(EV_P_ ev_io *w, int revents)
{
    char buf[QUEUE_MSG_SIZE];
    ssize_t res;

    memset(buf, 0, sizeof(buf));
    res = mq_receive(g_bsal_in, buf, sizeof(buf), NULL);
    if (res < 0) {
        LOGE("Failed to receive msg on IN queue");
        exit(-1);
    }

    if (handle_probe_req(buf)) return;
    if (handle_update_client(buf)) return;
    if (handle_connect(buf)) return;
    if (handle_disconnect(buf)) return;
    if (handle_rssi_xing(buf)) return;

    LOGE("BSAL invalid or unknown command '%s'", buf);
    exit(1);
}

int
target_bsal_init(bsal_event_cb_t event_cb, struct ev_loop *loop)
{
    struct mq_attr attr;
    const char *prefix = getenv("BM_TEST_PREFIX");
    const char *queue_path;

    if (!prefix) return -1;
    if (strlen(prefix) == 0) return -1;

    LOGI("BM_TEST_PREFIX: %s", prefix);

    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = 16;
    attr.mq_msgsize = QUEUE_MSG_SIZE;

    queue_path = strfmta("%s_bsal_in", prefix);
    g_bsal_in = mq_open(queue_path, O_CREAT | O_RDONLY, 0644, &attr);
    if (g_bsal_in < 0) {
        LOGE("Failed to create queue: %s because: %s", queue_path, strerror(errno));
        return -1;
    }

    queue_path = strfmta("%s_bsal_out", prefix);
    g_bsal_out = mq_open(queue_path, O_CREAT | O_WRONLY | O_NONBLOCK, 0644, &attr);
    if (g_bsal_out < 0) {
        LOGE("Failed to create queue: %s because: %s", queue_path, strerror(errno));
        return -1;
    }

    ev_io_init(&g_bsal_in_watcher, bsal_in_cb, g_bsal_in, EV_READ);
    ev_io_start(loop, &g_bsal_in_watcher);

    g_event_cb = event_cb;

    return 0;
}

int
target_bsal_iface_add(const bsal_ifconfig_t *ifcfg)
{
    char buf[QUEUE_MSG_SIZE];
    int ret;

    LOGI("BSAL: iface add");

    snprintf(buf, sizeof(buf),
        "IFACE_ADD\n"  \
        "ifname=%s\n" \
        "chan_util_check_sec=%d\n" \
        "chan_util_avg_count=%d\n" \
        "inact_check_sec=%d\n" \
        "inact_tmout_sec_normal=%d\n" \
        "inact_tmout_sec_overload=%d\n" \
        "def_rssi_inact_xing=%d\n" \
        "def_rssi_low_xing=%d\n" \
        "def_rssi_xing=%d\n" \
        "debug.raw_chan_util=%s\n" \
        "debug.raw_rssi=%s\n",
        ifcfg->ifname,
        ifcfg->chan_util_check_sec,
        ifcfg->chan_util_avg_count,
        ifcfg->inact_check_sec,
        ifcfg->inact_tmout_sec_normal,
        ifcfg->inact_tmout_sec_overload,
        ifcfg->def_rssi_inact_xing,
        ifcfg->def_rssi_low_xing,
        ifcfg->def_rssi_xing,
        ifcfg->debug.raw_chan_util ? "true" : "false",
        ifcfg->debug.raw_rssi ? "true" : "false");

    ret = mq_send(g_bsal_out, buf, strlen(buf), 0);
    if (ret < 0) {
        LOGE("BSAL: Failed to send IFACE_ADD");
        exit(1);
    }

    return 0;
}

int
target_bsal_iface_update(const bsal_ifconfig_t *ifcfg)
{
    char buf[QUEUE_MSG_SIZE];
    int ret;

    LOGI("BSAL: iface update");

    snprintf(buf, sizeof(buf),
        "IFACE_UPDATE\n"  \
        "ifname=%s\n" \
        "chan_util_check_sec=%d\n" \
        "chan_util_avg_count=%d\n" \
        "inact_check_sec=%d\n" \
        "inact_tmout_sec_normal=%d\n" \
        "inact_tmout_sec_overload=%d\n" \
        "def_rssi_inact_xing=%d\n" \
        "def_rssi_low_xing=%d\n" \
        "def_rssi_xing=%d\n" \
        "debug.raw_chan_util=%s\n" \
        "debug.raw_rssi=%s\n",
        ifcfg->ifname,
        ifcfg->chan_util_check_sec,
        ifcfg->chan_util_avg_count,
        ifcfg->inact_check_sec,
        ifcfg->inact_tmout_sec_normal,
        ifcfg->inact_tmout_sec_overload,
        ifcfg->def_rssi_inact_xing,
        ifcfg->def_rssi_low_xing,
        ifcfg->def_rssi_xing,
        ifcfg->debug.raw_chan_util ? "true" : "false",
        ifcfg->debug.raw_rssi ? "true" : "false");

    ret = mq_send(g_bsal_out, buf, strlen(buf), 0);
    if (ret < 0) {
        LOGE("BSAL: Failed to send IFACE_UPDATE");
        exit(1);
    }

    return 0;
}

int
target_bsal_iface_remove(const bsal_ifconfig_t *ifcfg)
{
    char buf[QUEUE_MSG_SIZE];
    int ret;

    LOGI("BSAL: iface remove");

    snprintf(buf, sizeof(buf),
        "IFACE_REMOVE\n"  \
        "ifname=%s\n",
        ifcfg->ifname);

    ret = mq_send(g_bsal_out, buf, strlen(buf), 0);
    if (ret < 0) {
        LOGE("BSAL: Failed to send IFACE_REMOVE");
        exit(1);
    }

    return 0;
}

int
target_bsal_client_add(const char *ifname, const uint8_t *mac_addr,
                       const bsal_client_config_t *conf)
{
    os_macaddr_t hwaddr;
    client_t *client;
    client_config_t *client_cfg;
    char buf[QUEUE_MSG_SIZE];
    int ret;

    LOGI("BSAL: client add");
    
    memcpy(&hwaddr.addr, mac_addr, sizeof(hwaddr.addr));
    client = get_client(&hwaddr);
    if (!client) {
        client = CALLOC(1, sizeof(*client));

        memcpy(&client->hwaddr, &hwaddr, sizeof(hwaddr));
        ds_dlist_init(&client->configs, client_config_t, node);

        ds_dlist_insert_tail(&g_clients, client);
    }

    client_cfg = CALLOC(1, sizeof(*client_cfg));

    STRSCPY_WARN(client_cfg->ifname, ifname);
    memcpy(&client_cfg->conf, conf, sizeof(client_cfg->conf));

    ds_dlist_insert_tail(&client->configs, client_cfg);

    memcpy(&hwaddr.addr, mac_addr, sizeof(hwaddr.addr));
    snprintf(buf, sizeof(buf),
        "CLIENT_ADD\n"
        "mac="PRI(os_macaddr_t)"\n" \
        "if_name=%s\n" \
        "blacklist=%s\n" \
        "rssi_probe_hwm=%d\n" \
        "rssi_probe_lwm=%d\n" \
        "rssi_auth_hwm=%d\n" \
        "rssi_auth_lwm=%d\n" \
        "rssi_inact_xing=%d\n" \
        "rssi_high_xing=%d\n" \
        "rssi_low_xing=%d\n" \
        "auth_reject_reason=%d",
        FMT(os_macaddr_t, hwaddr),
        ifname,
        conf->blacklist == true ? "true" : "false",
        conf->rssi_probe_hwm,
        conf->rssi_probe_lwm,
        conf->rssi_auth_hwm,
        conf->rssi_auth_lwm,
        conf->rssi_inact_xing,
        conf->rssi_high_xing,
        conf->rssi_low_xing,
        conf->auth_reject_reason);

    ret = mq_send(g_bsal_out, buf, strlen(buf), 0);
    if (ret < 0) {
        LOGE("BSAL: Failed to send CLIENT_ADD");
        exit(1);
    }

    return 0;
}

int
target_bsal_client_update(const char *ifname, const uint8_t *mac_addr,
                          const bsal_client_config_t *conf)
{
    os_macaddr_t hwaddr;
    char buf[QUEUE_MSG_SIZE];
    int ret;

    LOGI("BSAL: client update");

    memcpy(&hwaddr.addr, mac_addr, sizeof(hwaddr.addr));
    snprintf(buf, sizeof(buf),
        "CLIENT_UPDATE\n"
        "mac="PRI(os_macaddr_t)"\n" \
        "if_name=%s\n" \
        "blacklist=%s\n" \
        "rssi_probe_hwm=%d\n" \
        "rssi_probe_lwm=%d\n" \
        "rssi_auth_hwm=%d\n" \
        "rssi_auth_lwm=%d\n" \
        "rssi_inact_xing=%d\n" \
        "rssi_high_xing=%d\n" \
        "rssi_low_xing=%d\n" \
        "auth_reject_reason=%d",
        FMT(os_macaddr_t, hwaddr),
        ifname,
        conf->blacklist == true ? "true" : "false",
        conf->rssi_probe_hwm,
        conf->rssi_probe_lwm,
        conf->rssi_auth_hwm,
        conf->rssi_auth_lwm,
        conf->rssi_inact_xing,
        conf->rssi_high_xing,
        conf->rssi_low_xing,
        conf->auth_reject_reason);

    ret = mq_send(g_bsal_out, buf, strlen(buf), 0);
    if (ret < 0) {
        LOGE("BSAL: Failed to send CLIENT_UPDATE");
        exit(1);
    }

    return 0;
}

int
target_bsal_client_remove(const char *ifname, const uint8_t *mac_addr)
{
    LOGI("BSAL: Remove client");
    return 0;
}

int
target_bsal_client_measure(const char *ifname, const uint8_t *mac_addr,
                           int num_samples)
{
    bsal_ev_rssi_t *rssi_event;
    bsal_event_t event;
    os_macaddr_t hwaddr;
    client_t *client;

    memcpy(&hwaddr.addr, mac_addr, sizeof(hwaddr.addr));
    client = get_client(&hwaddr);

    memset(&event, 0, sizeof(event));
    event.type = BSAL_EVENT_RSSI;
    rssi_event = &event.data.rssi;

    STRSCPY_WARN(event.ifname, ifname);
    memcpy(&rssi_event->client_addr, &hwaddr.addr, sizeof(rssi_event->client_addr));
    rssi_event->rssi = client->info.snr;

    g_event_cb(&event);

    LOGI("BSAL: client measure");

    return 0;
}

int
target_bsal_client_disconnect(const char *ifname, const uint8_t *mac_addr,
                              bsal_disc_type_t type, uint8_t reason)
{
    bsal_ev_disconnect_t *disconnect_event;
    bsal_event_t event;
    os_macaddr_t hwaddr;
    client_t *client;

    memcpy(&hwaddr.addr, mac_addr, sizeof(hwaddr.addr));
    client = get_client(&hwaddr);

    memset(&event, 0, sizeof(event));
    event.type = BSAL_EVENT_CLIENT_DISCONNECT;
    disconnect_event = &event.data.disconnect;

    STRSCPY_WARN(event.ifname, ifname);
    memcpy(&disconnect_event->client_addr, &hwaddr.addr, sizeof(disconnect_event->client_addr));
    disconnect_event->reason = reason;
    disconnect_event->source = BSAL_DISC_SOURCE_REMOTE;
    disconnect_event->source = type;

    memset(&client->info, 0, sizeof(client->info));

    LOGI("BSAL: client disconnect");

    g_event_cb(&event);

    return 0;
}

int
target_bsal_client_info(const char *ifname, const uint8_t *mac_addr, bsal_client_info_t *info)
{
    os_macaddr_t hwaddr;
    client_t *client;

    memcpy(&hwaddr.addr, mac_addr, sizeof(hwaddr.addr));
    client = get_client(&hwaddr);

    LOGI("BSAL: client info");

    if (!client) {
        return -1;
    }

    memcpy(info, &client->info, sizeof(*info));

    return 0;
}

int
target_bsal_bss_tm_request(const char *ifname, const uint8_t *mac_addr,
                           const bsal_btm_params_t *btm_params)
{
    os_macaddr_t hwaddr;
    char *buffer = NULL;
    int ret;
    int i;

    LOGI("BSAL: BTM request");

    memcpy(&hwaddr.addr, mac_addr, sizeof(hwaddr.addr));

    strgrow(&buffer, "BTM_REQUEST\n");
    strgrow(&buffer, "mac="PRI(os_macaddr_t)"\n", FMT(os_macaddr_t, hwaddr));
    strgrow(&buffer, "ifname=%s\n", ifname);
    strgrow(&buffer, "neighs=\n");

    if (btm_params->num_neigh == 0)
        strgrow(&buffer, "    none\n\n");

    for (i = 0; i < btm_params->num_neigh; i++) {
        const bsal_neigh_info_t *neigh = &btm_params->neigh[i];
        os_macaddr_t bssid;
        char opt_subelems_buffer[2048];

        memset(&opt_subelems_buffer, 0, sizeof(opt_subelems_buffer));
        bin2hex(neigh->opt_subelems, neigh->opt_subelems_len, opt_subelems_buffer, sizeof(opt_subelems_buffer));

        memcpy(&bssid.addr, neigh->bssid, sizeof(bssid.addr));
        strgrow(&buffer, "    bssid="PRI(os_macaddr_t)"\n", FMT(os_macaddr_t, bssid));
        strgrow(&buffer, "    bssid_info=%d\n", neigh->bssid_info);
        strgrow(&buffer, "    op_class=%d\n", neigh->op_class);
        strgrow(&buffer, "    phy_type=%d\n", neigh->phy_type);
        strgrow(&buffer, "    opt_subelems=%s\n", opt_subelems_buffer);
        strgrow(&buffer, "    opt_subelems_len=%d\n\n", neigh->opt_subelems_len);
    }

    strgrow(&buffer, "num_neigh=%d\n", btm_params->num_neigh);
    strgrow(&buffer, "valid_int=%d\n", btm_params->valid_int);
    strgrow(&buffer, "abridged=%d\n", btm_params->abridged);
    strgrow(&buffer, "pref=%d\n", btm_params->pref);
    strgrow(&buffer, "disassoc_imminent=%d\n", btm_params->disassoc_imminent);
    strgrow(&buffer, "bss_term=%d\n", btm_params->bss_term);
    strgrow(&buffer, "tries=%d\n", btm_params->tries);
    strgrow(&buffer, "max_tries=%d\n", btm_params->max_tries);
    strgrow(&buffer, "retry_interval=%d\n", btm_params->retry_interval);
    strgrow(&buffer, "inc_neigh=%s\n", btm_params->inc_neigh ? "true" : "false");
    strgrow(&buffer, "inc_self=%s\n\n", btm_params->inc_self ? "true" : "false");

    ret = mq_send(g_bsal_out, buffer, strlen(buffer), 0);
    FREE(buffer);
    if (ret < 0) {
        LOGE("BSAL: Failed to send CLIENT_UPDATE");
        exit(1);
    }

    return 0;
}

int
target_bsal_rrm_beacon_report_request(const char *ifname, const uint8_t *mac_addr,
                                      const bsal_rrm_params_t *rrm_params)
{
    os_macaddr_t hwaddr;
    char buf[QUEUE_MSG_SIZE];
    int ret;

    LOGI("BSAL: rrm beacon report request");

    memcpy(&hwaddr.addr, mac_addr, sizeof(hwaddr.addr));

    snprintf(buf, sizeof(buf),
        "RRM_BEACON_REPORT_REQUEST\n"
        "mac="PRI(os_macaddr_t)"\n" \
        "if_name=%s\n" \
        "op_class=%d\n"  \
        "channel=%d\n"  \
        "rand_ivl=%d\n"  \
        "meas_dur=%d\n"  \
        "meas_mode=%d\n"  \
        "req_ssid=%d\n"  \
        "rep_cond=%d\n"  \
        "rpt_detail=%d\n"  \
        "req_ie=%d\n"  \
        "chanrpt_mode=%d\n",
        FMT(os_macaddr_t, hwaddr),
        ifname,
        rrm_params->op_class,
        rrm_params->channel,
        rrm_params->rand_ivl,
        rrm_params->meas_dur,
        rrm_params->meas_mode,
        rrm_params->req_ssid,
        rrm_params->rep_cond,
        rrm_params->rpt_detail,
        rrm_params->req_ie,
        rrm_params->chanrpt_mode);

    ret = mq_send(g_bsal_out, buf, strlen(buf), 0);
    if (ret < 0) {
        LOGE("BSAL: Failed to send RRM_BEACON_REPORT_REQUEST");
        exit(1);
    }

    return 0;
}

int
target_bsal_rrm_set_neighbor(const char *ifname, const bsal_neigh_info_t *nr)
{
    os_macaddr_t bssid;
    char buf[QUEUE_MSG_SIZE];
    char opt_subelems[4096];
    int ret;

    LOGI("BSAL: set neighbor");

    memcpy(&bssid.addr, nr->bssid, sizeof(bssid.addr));
    bin2hex(nr->opt_subelems, nr->opt_subelems_len, opt_subelems, sizeof(opt_subelems));

    snprintf(buf, sizeof(buf),
        "SET_NEIGHBOR\n"  \
        "if_name=%s\n"  \
        "bssid="PRI(os_macaddr_t)"\n"  \
        "bssid_info=%d\n"  \
        "op_class=%d\n"  \
        "channel=%d\n"  \
        "phy_type=%d\n"  \
        "opt_subelems=%s\n"  \
        "opt_subelems_len=%d\n",
        ifname,
        FMT(os_macaddr_t, bssid),
        nr->bssid_info,
        nr->op_class,
        nr->channel,
        nr->phy_type,
        opt_subelems,
        nr->opt_subelems_len);

    ret = mq_send(g_bsal_out, buf, strlen(buf), 0);
    if (ret < 0) {
        LOGE("BSAL: Failed to send SET_NEIGHBOR");
        exit(1);
    }

    return 0;
}

int
target_bsal_rrm_remove_neighbor(const char *ifname, const bsal_neigh_info_t *nr)
{
    os_macaddr_t bssid;
    char buf[QUEUE_MSG_SIZE];
    int ret;

    LOGI("BSAL: remove neighbor");

    memcpy(&bssid.addr, nr->bssid, sizeof(bssid.addr));

    snprintf(buf, sizeof(buf),
        "REMOVE_NEIGHBOR\n"
        "if_name=%s\n" \
        "bssid="PRI(os_macaddr_t),
        ifname,
        FMT(os_macaddr_t, bssid));

    ret = mq_send(g_bsal_out, buf, strlen(buf), 0);
    if (ret < 0) {
        LOGE("BSAL: Failed to send REMOVE_NEIGHBOR");
        exit(1);
    }

    return 0;
}

int
target_bsal_send_action(const char *ifname, const uint8_t *mac_addr,
                        const uint8_t *data, unsigned int data_len)
{
    os_macaddr_t hwaddr;
    char buf[QUEUE_MSG_SIZE];
    char action[4096];
    int ret;

    LOGI("BSAL: send action");

    memcpy(&hwaddr.addr, mac_addr, sizeof(hwaddr.addr));
    bin2hex(data, data_len, action, sizeof(action));

    snprintf(buf, sizeof(buf),
        "SEND_ACTION\n"
        "mac="PRI(os_macaddr_t)"\n" \
        "if_name=%s\n" \
        "data=%s\n",
        FMT(os_macaddr_t, hwaddr),
        ifname,
        data);

    ret = mq_send(g_bsal_out, buf, strlen(buf), 0);
    if (ret < 0) {
        LOGE("BSAL: Failed to send SEND_ACTION");
        exit(1);
    }

    return 0;
}
