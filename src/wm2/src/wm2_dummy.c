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

/* Dummy WM2 target implementation
 *
 * Usage:
 *
 * env DUMMY_WM_CMD=test-run-1.sh ovsdb-server --run path/to/wm ...
 *
 * The test-run-1.sh is expected to be a script which
 * interacts in some way with the system to orchestrate test
 * scenarios. Once script finishes WM shall terminate
 * gracefully. This in turn will cause ovsdb-server to
 * terminate as well.
 *
 * The script is called first with "init" argument to set
 * things up before WM is actually given a chance to do
 * anything. This is a good place to prep things like
 * onboarding info.
 *
 * Then the script is called with "test" argument when WM is
 * ready. The script can now use stdout to issue events to
 * the dummy WM driver to fake events for testing.
 */
#define _GNU_SOURCE

/* std libc */
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

/* 3rd party */
#include <ev.h>

/* internal */
#include <target.h>
#include <schema.h>
#include <util.h>
#include <ovsdb_table.h>

#define F(x) "/tmp/target_dummy_wm_" x

static ovsdb_table_t table_Wifi_VIF_State;
static const struct target_radio_ops *g_ops;
static int g_cmd_stdin[2];
static int g_cmd_stdout[2];
static ev_child g_cmd_child;
static ev_io g_cmd_io;
static FILE *g_cmd;

static void
wm2_dummy_cmd_cb(EV_P_ ev_child *c, int events)
{
    LOGI("cmd finished, terminating %d", c->rstatus);
    assert(c->rstatus == 0);
    ev_break(EV_A_ EVBREAK_ALL);
}

static void
wm2_dummy_cmd_io_cb(EV_P_ ev_io *io, int events)
{
    char buf[16*1024] = {0};
    char *ptr = buf;
    char *p;
    char *cmd;

    if (fgets(buf, sizeof(buf), g_cmd) == NULL) {
        ev_io_stop(EV_A_ io);
        return;
    }

    while ((p = strrchr(buf, '\n')))
        *p = 0;

    cmd = strsep(&ptr, " ");
    if (WARN_ON(!cmd))
        return;

    if (!strcmp(cmd, "dpp_announcement")) {
        struct target_dpp_chirp_obj c = {0};

        c.ifname = strsep(&ptr, " ");
        c.mac_addr = strsep(&ptr, " ");
        c.sha256_hex = strsep(&ptr, " ");
        g_ops->op_dpp_announcement(&c);
    }

    if (!strcmp(cmd, "client")) {
        struct schema_Wifi_Associated_Clients c = {0};
        const char *vif;
        bool assoc;

        vif = strsep(&ptr, " ");
        assoc = atoi(strsep(&ptr, " "));
        SCHEMA_SET_STR(c.mac, strsep(&ptr, " "));
        SCHEMA_SET_STR(c.key_id, strsep(&ptr, " "));
        //SCHEMA_SET_STR(c.dpp_netaccesskey_hex, strsep(&ptr, " \n"));
        SCHEMA_SET_STR(c.state, "idle");

        c._partial_update = true;
        g_ops->op_client(&c, vif, assoc);
    }

    if (!strcmp(cmd, "client_flush")) {
        const char *vif;

        vif = strsep(&ptr, " ");

        g_ops->op_flush_clients(vif);
    }

    if (!strcmp(cmd, "dpp_enrollee")) {
        struct target_dpp_conf_enrollee e = {0};

        e.ifname = strsep(&ptr, " ");
        e.sta_mac_addr = strsep(&ptr, " ");
        e.sta_netaccesskey_sha256_hex = strsep(&ptr, " ");

        g_ops->op_dpp_conf_enrollee(&e);
    }

    if (!strcmp(cmd, "dpp_network")) {
        struct target_dpp_conf_network n = {0};

        n.ifname = strsep(&ptr, " ");
        n.akm = atoi(strsep(&ptr, " "));
        n.ssid_hex = strsep(&ptr, " ");
        n.psk_hex = strsep(&ptr, " ");
        n.pmk_hex = strsep(&ptr, " ");
        n.dpp_netaccesskey_hex = strsep(&ptr, " ");
        n.dpp_connector = strsep(&ptr, " ");
        n.dpp_csign_hex = strsep(&ptr, " ");

        LOGI("dpp: network: %s/%d/%s/%s/%s/%s/%s/%s",
             n.ifname, n.akm, n.ssid_hex,
             n.psk_hex, n.pmk_hex,
             n.dpp_netaccesskey_hex,
             n.dpp_connector,
             n.dpp_csign_hex);

        g_ops->op_dpp_conf_network(&n);
    }

    if (!strcmp(cmd, "dpp_failed")) {
        g_ops->op_dpp_conf_failed();
    }

    if (!strcmp(cmd, "connect")) {
        struct schema_Wifi_VIF_State vstate = {0};
        const char *phy = strsep(&ptr, " ");
        const char *vif = strsep(&ptr, " ");
        const char *bssid = strsep(&ptr, " ");
        const char *channel = strsep(&ptr, " ");

        ovsdb_table_select_one(&table_Wifi_VIF_State, "if_name", vif, &vstate);
        vstate.associated_clients_present = false;
        vstate.vif_config_present = false;
        vstate._partial_update = true;
        SCHEMA_SET_STR(vstate.parent, bssid);
        SCHEMA_SET_INT(vstate.channel, atoi(channel));

        g_ops->op_vstate(&vstate, phy);
    }
}

__attribute__((constructor))
static void
wm2_dummy_target_dummy_init(void)
{
    const char *cmd = getenv("DUMMY_WM_CMD");
    char buf[4096];
    int rc;

    if (!cmd) return;
    snprintf(buf, sizeof(buf), "%s init", cmd);
    LOGI("dummy: running %s", buf);
    rc = system(buf);
    LOGD("%s: system() returned %d", __func__, rc);
}

bool wm2_dummy_target_radio_init(const struct target_radio_ops *ops)
{
    const char *cmd = getenv("DUMMY_WM_CMD");

    if (cmd && strlen(cmd) > 0) {
        const int trace = 0;
        int pid;

        assert(pipe(g_cmd_stdin) == 0);
        assert(pipe(g_cmd_stdout) == 0);

        g_cmd = fdopen(g_cmd_stdout[0], "r");
        assert(g_cmd != NULL);
        setvbuf(g_cmd, NULL, _IONBF, 0);

        pid = fork();
        assert(pid >= 0);

        if (pid == 0) {
            close(0); assert(dup(g_cmd_stdin[0]) == 0);
            close(1); assert(dup(g_cmd_stdout[1]) == 1);
            exit(execlp(cmd, cmd, "test", NULL) == 0 ? 0 : 1);
            assert(0); /* never reached */
        }

        ev_io_init(&g_cmd_io, wm2_dummy_cmd_io_cb, g_cmd_stdout[0], EV_READ);
        ev_io_start(EV_DEFAULT_ &g_cmd_io);

        ev_child_init(&g_cmd_child, wm2_dummy_cmd_cb, pid, trace);
        ev_child_start(EV_DEFAULT_ &g_cmd_child);
    }

    g_ops = ops;
    OVSDB_TABLE_INIT(Wifi_VIF_State, if_name);

    return atoi(file_geta(F("radio_init")) ?: "1");
}

bool wm2_dummy_target_radio_config_init2(void)
{
    return atoi(file_geta(F("radio_config_init2")) ?: "0");
}

bool wm2_dummy_target_radio_config_need_reset(void)
{
    return atoi(file_geta(F("radio_config_need_reset")) ?: "0");
}

bool wm2_dummy_target_radio_config_set2(const struct schema_Wifi_Radio_Config *rconf,
                                        const struct schema_Wifi_Radio_Config_flags *changed)
{
    struct schema_Wifi_Radio_State rstate = {0};

    LOGI("phy: %s: configuring", rconf->if_name);

#define CPY_INT(n) SCHEMA_CPY_INT(rstate.n, rconf->n)
#define CPY_STR(n) SCHEMA_CPY_STR(rstate.n, rconf->n)
    rstate._partial_update = true;

    CPY_INT(enabled);
    CPY_INT(channel);
    CPY_INT(dfs_demo);
    CPY_STR(country);
    CPY_STR(if_name);
    CPY_STR(freq_band);
    CPY_STR(ht_mode);
    CPY_STR(hw_mode);
#undef CPY_INT
#undef CPY_STR

    g_ops->op_rstate(&rstate);

    return true;
}

bool wm2_dummy_target_vif_config_set2(const struct schema_Wifi_VIF_Config *vconf,
                                      const struct schema_Wifi_Radio_Config *rconf,
                                      const struct schema_Wifi_Credential_Config *cconfs,
                                      const struct schema_Wifi_VIF_Config_flags *changed,
                                      int num_cconfs)
{
    struct schema_Wifi_VIF_State vstate = {0};

    LOGI("vif: %s: configuring", vconf->if_name);

#define CPY_INT(n) SCHEMA_CPY_INT(vstate.n, vconf->n)
#define CPY_STR(n) SCHEMA_CPY_STR(vstate.n, vconf->n)
#define CPY_SET(n) SCHEMA_FIELD_COPY_SET(vconf, &vstate, n)
#define CPY_MAP(n) SCHEMA_FIELD_COPY_MAP(vconf, &vstate, n)
    vstate._partial_update = true;

    SCHEMA_SET_INT(vstate.channel, rconf->channel ?: 1);
    SCHEMA_SET_STR(vstate.mac, "00:11:22:33:44:55"); // FIXME
    CPY_INT(ap_bridge);
    CPY_INT(btm);
    CPY_INT(dynamic_beacon);
    CPY_INT(enabled);
    CPY_INT(ft_mobility_domain);
    CPY_INT(ft_psk);
    CPY_INT(group_rekey);
    CPY_INT(mcast2ucast);
    CPY_INT(radius_srv_port);
    CPY_INT(rrm);
    CPY_INT(wpa);
    CPY_INT(wps);
    CPY_INT(wps_pbc);
    CPY_STR(bridge);
    CPY_STR(dpp_connector);
    CPY_STR(dpp_csign_hex);
    CPY_STR(dpp_netaccesskey_hex);
    CPY_STR(if_name);
    CPY_STR(min_hw_mode);
    CPY_STR(mode);
    CPY_STR(multi_ap);
    CPY_STR(parent);
    CPY_STR(radius_srv_addr);
    CPY_STR(radius_srv_secret);
    CPY_STR(ssid);
    CPY_STR(ssid_broadcast);
    CPY_STR(wps_pbc_key_id);
    CPY_SET(mac_list);
    CPY_SET(wpa_key_mgmt);
    CPY_MAP(security);
    CPY_MAP(wpa_psks);
#undef CPY_INT
#undef CPY_STR

    g_ops->op_vstate(&vstate, rconf->if_name);

    return true;
}

bool wm2_dummy_target_dpp_supported(void)
{
    return atoi(file_geta(F("dpp_supported")) ?: "1");
}

bool wm2_dummy_target_dpp_config_set(const struct schema_DPP_Config *config)
{
    return atoi(file_geta(F("dpp_config")) ?: "1");
}

bool wm2_dummy_target_dpp_key_get(struct target_dpp_key *key)
{
    key->type = atoi(file_geta(F("dpp_key_curve")) ?: "0");
    STRSCPY_WARN(key->hex, file_geta(F("dpp_key_hex")) ?: "");

    return atoi(file_geta(F("dpp_key")) ?: "0");
}

bool wm2_dummy_target_desired(void)
{
    return strlen(getenv("DUMMY_WM_CMD") ?: "") > 0;
}
