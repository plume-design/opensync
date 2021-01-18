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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>

#include "log.h"
#include "util.h"
#include "const.h"
#include "ovsdb.h"
#include "schema.h"
#include "ovsdb_table.h"
#include "json_util.h"

#include "osp_led.h"
#include "pm.h"


#define PM_LED_TS_FILE          "/tmp/pm.led.ts"


static ovsdb_table_t table_AWLAN_Node;
static ovsdb_table_t table_Manager;

static ev_timer led_tmr_connecting;
static ev_timer led_tmr_connectfail;



static int pm_write_tmp(const char *file, const char *data)
{
    int fp = -1;
    int rv = -1;

    fp = open(file, O_WRONLY | O_CREAT | O_TRUNC);
    if (fp < 0) {
        goto err;
    }

    rv = write(fp, data, strlen(data));
    if (rv != (int)strlen(data)) {
        goto err;
    }

    rv = 0;

err:
    if (fp >= 0) {
        close(fp);
    }

    return rv;
}

static int pm_read_tmp(const char *file, char *data, unsigned int len)
{
    int fp = -1;
    int rv = -1;

    fp = open(file, O_RDONLY);
    if (fp < 0) {
        goto err;
    }

    rv = read(fp, data, len);
    if (rv < 0) {
        goto err;
    }

    rv = len;

err:
    if (fp >= 0) {
        close(fp);
    }

    return rv;
}

static void pm_led_read_ts_file(uint64_t *ts)
{
    char data[128] = { 0 };

    *ts = (uint64_t)-1;

    if (pm_read_tmp(PM_LED_TS_FILE, data, sizeof(data)) < 0) {
        return;
    }

    if (sscanf(data, "%llu", ts) != 1) {
        return;
    }
}

static void pm_led_write_ts_file(uint64_t ts)
{
    char data[128];

    snprintf(data, sizeof(data)-1, "%llu", ts);
    data[sizeof(data)-1] = '\0';

    pm_write_tmp(PM_LED_TS_FILE, data);
}


static void pm_led_write_state_to_ovs(enum osp_led_state state)
{
    struct schema_AWLAN_Node awlan_node;
    char *filter[] = { "+", SCHEMA_COLUMN(AWLAN_Node, led_config), NULL };

    MEMZERO(awlan_node);
    SCHEMA_KEY_VAL_APPEND(awlan_node.led_config, "state", osp_led_state_to_str(state));

    if (!ovsdb_table_update_where_f(&table_AWLAN_Node, NULL, &awlan_node, filter)) {
        LOGE("LEDM: Unable to update LED state to OVSDB");
    }
}


static int pm_led_update_led_config(struct schema_AWLAN_Node *awlan_node)
{
    enum osp_led_state state = OSP_LED_ST_LAST;
    uint32_t priority = OSP_LED_PRIORITY_DEFAULT;
    const char *val;
    bool clear = false;
    int rv = -1;

    val = SCHEMA_KEY_VAL_NULL(awlan_node->led_config, "state");
    if (val != NULL)
    {
        state = osp_led_str_to_state(val);
        if (state == OSP_LED_ST_LAST) {
            LOGE("LEDM: Invalid LED state: %s", val);
            return -1;
        }

        val = SCHEMA_KEY_VAL_NULL(awlan_node->led_config, "priority");
        if (val != NULL) {
            sscanf(val, "%u", &priority);
        }

        val = SCHEMA_KEY_VAL_NULL(awlan_node->led_config, "clear");
        if ((val != NULL) && (!strcmp(val, "true"))) {
            clear = true;
        }
    }

    if (state == OSP_LED_ST_LAST) {
        return -1;
    }

    if (clear == false) {
        rv = osp_led_set_state(state, priority);
        if (rv != 0) {
            LOGE("LEDM: Could not set LED state: %d:%u", state, priority);
            return -1;
        }
    }
    else {
        rv = osp_led_clear_state(state);
        if (rv != 0) {
            LOGE("LEDM: Could not clear LED state: %d", state);
            return -1;
        }
    }

    return rv;
}


static int pm_led_update_connected(bool connected)
{
    LOGD("LEDM: %s; connected=%s", __func__, (connected == true) ? "true" : "false");

    if (connected)
    {
        ev_timer_stop(EV_DEFAULT, &led_tmr_connecting);
        ev_timer_stop(EV_DEFAULT, &led_tmr_connectfail);

        unlink(PM_LED_TS_FILE);
    }
    else
    {
        /* don't restart timers or rewrite TS file if it already exists */
        if (access(PM_LED_TS_FILE, F_OK) == 0) {
            return 0;
        }

        if (CONFIG_PM_LED_CONNECTING_TIMEOUT > 0)
        {
            ev_timer_stop(EV_DEFAULT, &led_tmr_connecting);
            ev_timer_set(&led_tmr_connecting, CONFIG_PM_LED_CONNECTING_TIMEOUT, 0.0);
            ev_timer_start(EV_DEFAULT, &led_tmr_connecting);
        }

        if (CONFIG_PM_LED_CONNECT_FAIL_TIMEOUT > 0)
        {
            ev_timer_stop(EV_DEFAULT, &led_tmr_connectfail);
            ev_timer_set(&led_tmr_connectfail, CONFIG_PM_LED_CONNECT_FAIL_TIMEOUT, 0.0);
            ev_timer_start(EV_DEFAULT, &led_tmr_connectfail);
        }

        pm_led_write_ts_file((uint64_t)time(NULL));
    }

    return 0;
}

static void callback_AWLAN_Node(
        ovsdb_update_monitor_t *mon,
        struct schema_AWLAN_Node *old_rec,
        struct schema_AWLAN_Node *awlan_node)
{
    LOGD("LEDM: %s", __func__);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, led_config))) {
        pm_led_update_led_config(awlan_node);
    }
}

static void callback_Manager(
        ovsdb_update_monitor_t *mon,
        struct schema_Manager *old_rec,
        struct schema_Manager *manager)
{

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        // if Manager table is deleted it is assumed
        // that the manager is in disconnected state
        manager->is_connected = false;
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Manager, is_connected))) {
        pm_led_update_connected(manager->is_connected);
    }
}


static bool pm_led_ovsdb_init(void)
{
    char *led_config_filter[]   = {"+", SCHEMA_COLUMN(AWLAN_Node, led_config), NULL};
    char *is_connected_filter[] = {"+", SCHEMA_COLUMN(Manager, is_connected), NULL};

    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
    OVSDB_TABLE_INIT_NO_KEY(Manager);

    OVSDB_TABLE_MONITOR_F(AWLAN_Node, led_config_filter);
    OVSDB_TABLE_MONITOR_F(Manager,    is_connected_filter);

    return true;
}


static void pm_led_tmr_connecting_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    pm_led_write_state_to_ovs(OSP_LED_ST_CONNECTING);
}

static void pm_led_tmr_connectfail_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    pm_led_write_state_to_ovs(OSP_LED_ST_CONNECTFAIL);
}

bool pm_led_init(void)
{
    int led_cnt;
    uint64_t ts;
    enum osp_led_state state;

    LOGN("Initializing LEDM");

    if (osp_led_init(&led_cnt) != 0) {
        return false;
    }

    if (!pm_led_ovsdb_init()) {
        return false;
    }


    /* Initialize, but don't run, both timers */
    ev_timer_init(&led_tmr_connecting, pm_led_tmr_connecting_cb, 0.0, 0.0);
    ev_timer_init(&led_tmr_connectfail, pm_led_tmr_connectfail_cb, 0.0, 0.0);


    pm_led_read_ts_file(&ts);
    if (ts != (uint64_t)-1)
    {
        int64_t diff = time(NULL) - ts;

        uint64_t rem_discon = (uint64_t)-1;
        uint64_t rem_connfail = (uint64_t)-1;

        if (CONFIG_PM_LED_CONNECTING_TIMEOUT > 0)
        {
            if (diff > CONFIG_PM_LED_CONNECTING_TIMEOUT) {
                rem_discon = 0;
            }
            else {
                rem_discon = CONFIG_PM_LED_CONNECTING_TIMEOUT - diff;
            }
        }

        if (CONFIG_PM_LED_CONNECT_FAIL_TIMEOUT > 0)
        {
            if (diff > CONFIG_PM_LED_CONNECT_FAIL_TIMEOUT) {
                rem_connfail = 0;
            }
            else {
                rem_connfail = CONFIG_PM_LED_CONNECT_FAIL_TIMEOUT - diff;
            }
        }

        if (rem_discon != (uint64_t)-1)
        {
            ev_timer_stop(EV_DEFAULT, &led_tmr_connecting);
            ev_timer_set(&led_tmr_connecting, rem_discon, 0.0);
            ev_timer_start(EV_DEFAULT, &led_tmr_connecting);
        }
        if (rem_connfail != (uint64_t)-1)
        {
            ev_timer_stop(EV_DEFAULT, &led_tmr_connectfail);
            ev_timer_set(&led_tmr_connectfail, rem_connfail, 0.0);
            ev_timer_start(EV_DEFAULT, &led_tmr_connectfail);
        }
    }

    osp_led_get_state(&state, NULL);
    return (osp_led_set_state(state, OSP_LED_PRIORITY_DEFAULT) == 0) ? true : false;
}
