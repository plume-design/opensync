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
#include "ovsdb_sync.h"
#include "schema.h"
#include "ovsdb_table.h"
#include "json_util.h"
#include "module.h"
#include "os_util.h"

#include "osp_led.h"
#include "kconfig.h"


#define PM_LED_TS_FILE              "/tmp/pm.led.ts"
#define PM_LED_MAX_PATTERN_ELEMENTS 10000
#define PM_LED_MAX_PATTERN_EL_LEN   100


MODULE(pm_led, pm_led_init, pm_led_fini);


static ovsdb_table_t table_AWLAN_Node;
static ovsdb_table_t table_Manager;
static ovsdb_table_t table_LED_Config;

static ev_timer led_tmr_connecting;
static ev_timer led_tmr_connectfail;

#if CONFIG_LEGACY_LED_STATE_MAPPING
static enum osp_led_state led_translate_mode = OSP_LED_ST_LAST;



/**
 * Provides backwards compatibility to translate a LED "mode" into a "state"
 * which is understandable to OSP layer functions.
 */
static enum osp_led_state pm_led_translate_led_config(struct schema_AWLAN_Node *awlan_node)
{
    const char *mode;

    mode = SCHEMA_KEY_VAL_NULL(awlan_node->led_config, "mode");
    if (mode != NULL) {
        if (!strcmp(mode, "off")) {
            return OSP_LED_ST_CONNECTED;
        }
        else if (!strcmp(mode, "breathe")) {
            return OSP_LED_ST_CONNECTING;
        }
        else if (!strcmp(mode, "pattern")) {
            return OSP_LED_ST_OPTIMIZE;
        }
        else {
            LOGE("LEDM: Could not translate from mode %s to state", mode);
            return OSP_LED_ST_IDLE;
        }
    }

    LOGE("LEDM: Could not translate from mode to state");
    return OSP_LED_ST_IDLE;
}
#endif

static int pm_write_tmp(const char *file, const char *data)
{
    int fp = -1;
    int rv = -1;

    fp = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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

/**
 * Needed for backwards compatibility to handle entries into AWLAN_Node::led_config
 * instead of LED_Config. It will parse the entry into AWLAN_Node, attempt to
 * translate legacy "mode" into "state" if necessary and then update LED_Config
 * with an operation equivalent to directly using OSP layer functions.
 */
static int pm_led_update_led_config_legacy(struct schema_AWLAN_Node *awlan_node)
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
    else if (kconfig_enabled(CONFIG_LEGACY_LED_STATE_MAPPING))
    {
        state = pm_led_translate_led_config(awlan_node);
        if (state == OSP_LED_ST_IDLE) {
            return -1;
        }

        LOGN("LEDM: Previous LED state translated from mode: %s", osp_led_state_to_str(state));
        if (led_translate_mode != OSP_LED_ST_LAST) {
            /* Instead of changing state directly with OSP API, update LED_Config which handles state update */
            rv = osp_led_ovsdb_delete_led_config(led_translate_mode, OSP_LED_POSITION_DEFAULT);
            if (rv != 0) {
                LOGI("LEDM: Could not clear translated LED state: %d", led_translate_mode);
            }
        }
        led_translate_mode = state;
    }

    if (state == OSP_LED_ST_LAST) {
        LOGE("LEDM: Invalid LED state or mode: %s", val);
        return -1;
    }

    if (clear == false) {
        /* Instead of changing state directly with OSP API, update LED_Config which handles state update */
        rv = osp_led_ovsdb_add_led_config(state, priority, OSP_LED_POSITION_DEFAULT);
        if (rv != 0) {
            LOGI("LEDM: Could not set LED state: %s: priority:%u", osp_led_state_to_str(state), priority);
            return -1;
        }
    }
    else
    {
        /* Instead of changing state directly with OSP API, update LED_Config which handles state update */
        rv = osp_led_ovsdb_delete_led_config(state, OSP_LED_POSITION_DEFAULT);
        if (rv != 0) {
            LOGI("LEDM: Could not clear LED state: %s", osp_led_state_to_str(state));
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

/**
 * Parses a string containing multiple LED pattern elements into an array of
 * `osp_led_pattern_el` structs.
 * The input string format should be: "1000,255;255;255,0 1000,255;255;255,0"
 * with up to PM_LED_MAX_PATTERN_ELEMENTS elements. Returns the number of elements
 * parsed or a negative value on error.
 */
static int pm_led_parse_pattern(char *pattern, struct osp_led_pattern_el *pattern_els)
{
    int count = 0;
    char tmp[PM_LED_MAX_PATTERN_ELEMENTS * PM_LED_MAX_PATTERN_EL_LEN];
    char *token;
    long parsed_val;

    if (strlen(pattern) == 0) {
        LOGE("LEDM: LED pattern is empty");
        return -1;
    }

    /* Do not modify the original string but instead create a copy */
    STRSCPY(tmp, pattern);
    token = strtok(tmp, ",; ");

    while (true)
    {
        if (count == PM_LED_MAX_PATTERN_ELEMENTS) {
            LOGE("LEDM: Count of pattern elements exceeds maximum value: %d", count);
            return -1;
        }

        /*
         * Parse element into a `osp_led_pattern_el` struct. The element must have
         * the exact format "1000,255;255;255,0". The order of values corresponds
         * to the order of fields in the `osp_led_pattern_el` struct.
         */
        if (!os_strtoul(token, &parsed_val, 0)) {
            LOGE("LEDM: Error parsing 'duration': '%s'", token);
            return -1;
        }
        pattern_els[count].duration = (uint16_t)parsed_val;

        token = strtok(NULL, ",; ");
        if (!os_strtoul(token, &parsed_val, 0)) {
            LOGE("LEDM: Error parsing 'color.r': '%s'", token);
            return -1;
        }
        pattern_els[count].color.r = (uint8_t)parsed_val;
        token = strtok(NULL, ",; ");
        if (!os_strtoul(token, &parsed_val, 0)) {
            LOGE("LEDM: Error parsing 'color.g': '%s'", token);
            return -1;
        }
        pattern_els[count].color.g = (uint8_t)parsed_val;
        token = strtok(NULL, ",; ");
        if (!os_strtoul(token, &parsed_val, 0)) {
            LOGE("LEDM: Error parsing 'color.b': '%s'", token);
            return -1;
        }
        pattern_els[count].color.b = (uint8_t)parsed_val;

        token = strtok(NULL, ",; ");
        if (!os_strtoul(token, &parsed_val, 0)) {
            LOGE("LEDM: Error parsing 'fade': '%s'", token);
            return -1;
        }
        pattern_els[count].fade = (uint16_t)parsed_val;

        /* Treat the pattern as incorrectly formatted if fade is longer than entire duration */
        if (pattern_els[count].fade > pattern_els[count].duration) {
            LOGE("LEDM: Fade [%ums] is longer than entire duration [%ums]",
                pattern_els[count].fade, pattern_els[count].duration);
            return -1;
        }

        /* If element was parsed correctly increment count by 1 */
        count++;

        /* Return from function if there are no more elements to parse */
        token = strtok(NULL, ",; ");
        if (token == NULL) return count;
    }

    LOGE("LEDM: Incorrect pattern format: '%s'", pattern);
    return -1;
}

static void pm_led_clear_related_states(enum osp_led_state state, uint8_t position)
{
    if (state == OSP_LED_ST_CONNECTED)
    {
        LOGD("LEDM: Clearing related states: %s, %s",
            osp_led_state_to_str(OSP_LED_ST_CONNECTING),
            osp_led_state_to_str(OSP_LED_ST_CONNECTFAIL));
        osp_led_ovsdb_delete_led_config(OSP_LED_ST_CONNECTING, position);
        osp_led_ovsdb_delete_led_config(OSP_LED_ST_CONNECTFAIL, position);
    }
    else if (state == OSP_LED_ST_CONNECTING)
    {
        LOGD("LEDM: Clearing related states: %s, %s",
            osp_led_state_to_str(OSP_LED_ST_CONNECTED),
            osp_led_state_to_str(OSP_LED_ST_CONNECTFAIL));
        osp_led_ovsdb_delete_led_config(OSP_LED_ST_CONNECTED, position);
        osp_led_ovsdb_delete_led_config(OSP_LED_ST_CONNECTFAIL, position);
    }
    else if (state == OSP_LED_ST_CONNECTFAIL)
    {
        LOGD("LEDM: Clearing related states: %s, %s",
            osp_led_state_to_str(OSP_LED_ST_CONNECTED),
            osp_led_state_to_str(OSP_LED_ST_CONNECTING));
        osp_led_ovsdb_delete_led_config(OSP_LED_ST_CONNECTED, position);
        osp_led_ovsdb_delete_led_config(OSP_LED_ST_CONNECTING, position);
    }
}

static int pm_led_update_led_config(
        ovsdb_update_monitor_t *mon,
        struct schema_LED_Config *old_rec,
        struct schema_LED_Config *led_config)
{
    enum osp_led_state state;
    uint32_t priority;
    uint8_t position;
    char *pattern;
    struct osp_led_pattern_el pattern_els[PM_LED_MAX_PATTERN_ELEMENTS];
    enum osp_led_state old_state;
    int pattern_els_count = 0;
    int rv = -1;

    LOGI("LEDM: Updating LED_Config: state:%s operation:%s", led_config->name, ovsdb_update_type_to_str(mon->mon_type));

    state = osp_led_str_to_state(led_config->name);
    priority = (uint32_t)led_config->priority;
    if (led_config->position != OSP_LED_POSITION_DEFAULT
        && led_config->position >= CONFIG_OSP_LED_COUNT
        && mon->mon_type != OVSDB_UPDATE_DEL)
    {
        LOGE("LEDM: Position [%d] greater than or equal to total LED count [%d]", led_config->position, CONFIG_OSP_LED_COUNT);
        ovsdb_table_delete_where(&table_LED_Config, ovsdb_where_uuid("_uuid", mon->mon_uuid));
    }

    position = (uint8_t)led_config->position;
    pattern = (char *)led_config->pattern;
    if (state == OSP_LED_ST_CLOUD
        && mon->mon_type != OVSDB_UPDATE_DEL)
    {
        pattern_els_count = pm_led_parse_pattern(pattern, pattern_els);
        if (pattern_els_count < 0) {
            LOGI("LEDM: Could not parse custom LED pattern: %s", pattern);
            ovsdb_table_delete_where(&table_LED_Config, ovsdb_where_uuid("_uuid", mon->mon_uuid));
        }
        else {
            LOGD("LEDM: Successfully parsed %d pattern elements", pattern_els_count);
        }
    }

    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        rv = osp_led_set_state(state, priority, position, pattern_els, pattern_els_count);
        if (rv != 0) {
            LOGE("LEDM: Could not set new LED state: %s priority: %u", led_config->name, priority);
            return -1;
        }
    }
    else if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        rv = osp_led_clear_state(state, position);
        if (rv != 0) {
            LOGE("LEDM: Could not clear LED state: %s", led_config->name);
            return -1;
        }
    }
    else if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        /*
         * If the field "name" is modified, the old record will contain the state which
         * needs to be cleared.
         * If the field "name" is not modified, by OVSDB design the old record will not
         * contain the "name" field. In this case, the old state must still be cleared and
         * set again since other parameters (ie. pattern) may have changed.
         */
        old_state = osp_led_str_to_state(old_rec->name);
        if (old_state == OSP_LED_ST_LAST) {
            LOGD("LEDM: Updated LED name is empty:'%s', state did not change", old_rec->name);
            old_state = state;
        }

        rv = osp_led_clear_state(old_state, position);
        if (rv != 0) {
            LOGE("LEDM: Could not clear old LED state: %s", osp_led_state_to_str(old_state));
            return -1;
        }

        rv = osp_led_set_state(state, priority, position, pattern_els, pattern_els_count);
        if (rv != 0) {
            LOGE("LEDM: Could not update LED state: %s priority: %u", led_config->name, priority);
            return -1;
        }
    }

    if (mon->mon_type == OVSDB_UPDATE_NEW || mon->mon_type == OVSDB_UPDATE_MODIFY) {
        pm_led_clear_related_states(state, position);
    }

    return rv;
}

/**
 * Deprecated method for updating LED config is by updating AWLAN_Node column led_config.
 * For newer versions, it's recommended to use LED_Config instead.
 */
static void callback_AWLAN_Node(
        ovsdb_update_monitor_t *mon,
        struct schema_AWLAN_Node *old_rec,
        struct schema_AWLAN_Node *awlan_node)
{
    LOGD("LEDM: %s", __func__);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, led_config))) {
        pm_led_update_led_config_legacy(awlan_node);
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

static void callback_LED_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_LED_Config *old_rec,
        struct schema_LED_Config *led_config)
{
    LOGD("LEDM: %s", __func__);

    pm_led_update_led_config(mon, old_rec, led_config);
}

static bool pm_led_ovsdb_init(void)
{
    char *led_config_filter[]   = {"+", SCHEMA_COLUMN(AWLAN_Node, led_config), NULL};
    char *is_connected_filter[] = {"+", SCHEMA_COLUMN(Manager, is_connected), NULL};

    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
    OVSDB_TABLE_INIT_NO_KEY(Manager);
    OVSDB_TABLE_INIT_NO_KEY(LED_Config);

    OVSDB_TABLE_MONITOR_F(AWLAN_Node, led_config_filter);
    OVSDB_TABLE_MONITOR_F(Manager,    is_connected_filter);
    OVSDB_TABLE_MONITOR(LED_Config, false);

    return true;
}


static void pm_led_tmr_connecting_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    osp_led_ovsdb_add_led_config(OSP_LED_ST_CONNECTING, OSP_LED_PRIORITY_DEFAULT, OSP_LED_POSITION_DEFAULT);
}

static void pm_led_tmr_connectfail_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    osp_led_ovsdb_add_led_config(OSP_LED_ST_CONNECTFAIL, OSP_LED_PRIORITY_DEFAULT, OSP_LED_POSITION_DEFAULT);
}

void pm_led_init(void *data)
{
    uint64_t ts;
    enum osp_led_state state;

    LOGN("Initializing LEDM");

    if (osp_led_init() != 0) {
        return;
    }

    if (!pm_led_ovsdb_init()) {
        return;
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

    /* Set default LED states */
    for (int pos = 0; pos < CONFIG_OSP_LED_COUNT; pos++) {
        osp_led_get_state(&state, NULL, pos);
        osp_led_set_state(state, OSP_LED_PRIORITY_DEFAULT, pos, NULL, 0);
    }
}

void pm_led_fini(void *data)
{
    LOGN("Deinitializing LEDM");
}
