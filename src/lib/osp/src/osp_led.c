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
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "log.h"
#include "util.h"
#include "const.h"
#include "osp_led.h"
#include "ovsdb.h"
#include "ovsdb_sync.h"
#include "schema.h"
#include "ovsdb_table.h"

#define OSP_LED_STATE_STR_MAX_LEN   128             /**< Max length of LED state string */

static ovsdb_table_t table_LED_Config;
static bool led_config_initialized = false;

static const char* led_state_str[OSP_LED_ST_LAST] =
{
    [OSP_LED_ST_IDLE]           = "idle",
    [OSP_LED_ST_ERROR]          = "error",
    [OSP_LED_ST_CONNECTED]      = "connected",
    [OSP_LED_ST_CONNECTING]     = "connecting",
    [OSP_LED_ST_CONNECTFAIL]    = "connectfail",
    [OSP_LED_ST_WPS]            = "wps",
    [OSP_LED_ST_OPTIMIZE]       = "optimize",
    [OSP_LED_ST_LOCATE]         = "locate",
    [OSP_LED_ST_HWERROR]        = "hwerror",
    [OSP_LED_ST_THERMAL]        = "thermal",
    [OSP_LED_ST_BTCONNECTABLE]  = "btconnectable",
    [OSP_LED_ST_BTCONNECTING]   = "btconnecting",
    [OSP_LED_ST_BTCONNECTED]    = "btconnected",
    [OSP_LED_ST_BTCONNECTFAIL]  = "btconnectfail",
    [OSP_LED_ST_UPGRADING]      = "upgrading",
    [OSP_LED_ST_UPGRADED]       = "upgraded",
    [OSP_LED_ST_UPGRADEFAIL]    = "upgradefail",
    [OSP_LED_ST_HWTEST]         = "hwtest",
    [OSP_LED_ST_IOT_ALARM]      = "iotalarm",
    [OSP_LED_ST_CLOUD]          = "cloud",
};

static struct led_ctx g_leds_ctx[CONFIG_OSP_LED_COUNT];


const char* osp_led_state_to_str(enum osp_led_state state)
{
    if ((state < 0) || (state >= OSP_LED_ST_LAST))
        return "";

    return led_state_str[state];
}

enum osp_led_state osp_led_str_to_state(const char *str)
{
    int i;

    for (i = 0; i < OSP_LED_ST_LAST; i++)
    {
        if (!strcmp(str, led_state_str[i]))
        {
            return (enum osp_led_state)i;
        }
    }

    return OSP_LED_ST_LAST;
}

static void osp_led_init_led_config(void)
{
    OVSDB_TABLE_INIT_NO_KEY(LED_Config);

    led_config_initialized = true;
}

int osp_led_ovsdb_add_led_config(enum osp_led_state state, uint32_t priority, uint8_t position)
{
    bool rv;
    struct schema_LED_Config led_config = { 0 };

    if (!led_config_initialized)
    {
        osp_led_init_led_config();
    }

    STRSCPY(led_config.name, osp_led_state_to_str(state));
    led_config.priority = priority;
    led_config.position = position;
    rv = ovsdb_table_insert(&table_LED_Config, &led_config);
    if (!rv) {
        LOGD("LEDM: Unable to insert row into LED_Config state:%s, priority:%u, position:%u",
            osp_led_state_to_str(state), priority, position);
        return -1;
    }

    return 0;
}

int osp_led_ovsdb_delete_led_config(enum osp_led_state state, uint8_t position)
{
    json_t *where;
    json_t *cond;
    char name[OSP_LED_STATE_STR_MAX_LEN];
    int rv;

    if (!led_config_initialized)
    {
        osp_led_init_led_config();
    }

    where = json_array();
    STRSCPY(name, osp_led_state_to_str(state));
    cond = ovsdb_tran_cond_single(SCHEMA_COLUMN(LED_Config, name),
                                  OFUNC_EQ,
                                  name);
    json_array_append_new(where, cond);
    cond = ovsdb_tran_cond_single_json(SCHEMA_COLUMN(LED_Config, position),
                                       OFUNC_EQ,
                                       json_integer(position));
    json_array_append_new(where, cond);

    rv = ovsdb_table_delete_where(&table_LED_Config, where);
    if (rv < 1) {
        LOGD("LEDM: Unable to delete row from LED_Config state:%s", name);
        return -1;
    }

    return 0;
}

enum osp_led_state osp_led_ovsdb_get_active_led_state(uint8_t position)
{
    json_t *where;
    json_t *cond;
    int count = 0;
    struct schema_LED_Config *led_config = NULL;
    uint32_t high_prio = OSP_LED_PRIORITY_DEFAULT;
    enum osp_led_state high_prio_state = OSP_LED_ST_IDLE;

    if (!led_config_initialized)
    {
        osp_led_init_led_config();
    }

    where = json_array();
    cond = ovsdb_tran_cond_single_json(SCHEMA_COLUMN(LED_Config, position),
                                       OFUNC_EQ,
                                       json_integer(position));
    json_array_append_new(where, cond);
    led_config = ovsdb_table_select_where(&table_LED_Config, where, &count);

    for (int i = 0; i < count; i++) {
        enum osp_led_state curr_state;
        uint32_t curr_prio;

        curr_state = osp_led_str_to_state(led_config[i].name);
        /* If state has default prio, get its default prio value from target layer */
        if ((uint32_t)led_config[i].priority == OSP_LED_PRIORITY_DEFAULT) {
            curr_prio = osp_led_get_state_default_prio(curr_state);
        } else {
            curr_prio = (uint32_t)led_config[i].priority;
        }

        LOGD("%s: Row name:%s, priority:%u", __func__, led_config[i].name, curr_prio);
        if (curr_prio < high_prio) {
            high_prio = curr_prio;
            high_prio_state = curr_state;
        }
    }

    LOGD("%s: Highest priority state:%d, priority:%d", __func__, (int)high_prio_state, (int)high_prio);

    return high_prio_state;
}

static void osp_led_clear_ctx_state(struct led_ctx *lctx, enum osp_led_state state)
{
    lctx->state_enab[state] = false;
    lctx->state_prio[state] = OSP_LED_PRIORITY_DISABLE;
}

static void osp_led_set_ctx_state(struct led_ctx *lctx, enum osp_led_state state, uint32_t priority)
{
    lctx->state_enab[state] = true;

    if (priority == OSP_LED_PRIORITY_DEFAULT)
        lctx->state_prio[state] = lctx->state_def_prio[state];
    else
        lctx->state_prio[state] = priority;
}

static void osp_led_set_idle_state(struct led_ctx *lctx)
{
    lctx->state_enab[OSP_LED_ST_IDLE] = true;
    lctx->state_prio[OSP_LED_ST_IDLE] = lctx->state_def_prio[OSP_LED_ST_IDLE];
    lctx->cur_state = OSP_LED_ST_IDLE;
}


static enum osp_led_state osp_led_find_high_prio_state(struct led_ctx *lctx, uint32_t *highest_priority)
{
    int i;
    uint32_t prio = OSP_LED_PRIORITY_DEFAULT;
    enum osp_led_state high_state = OSP_LED_ST_IDLE;

    for (i = 0; i < OSP_LED_ST_LAST; i++)
    {
        if ((lctx->state_enab[i] == true) && (lctx->state_prio[i] < prio))
        {
            prio = lctx->state_prio[i];
            high_state = i;
        }
    }

    if (highest_priority != NULL)
        *highest_priority = prio;

    return high_state;
}

static void osp_led_set_new_state(struct led_ctx *lctx)
{
    enum osp_led_state new_state;

    new_state = osp_led_find_high_prio_state(lctx, NULL);
    if (new_state != lctx->cur_state)
    {
        LOGD("cur_state: %s, new_state: %s",
                osp_led_state_to_str(lctx->cur_state),
                osp_led_state_to_str(new_state));

        lctx->cur_state = new_state;
        osp_led_tgt_set_state(new_state, 0, lctx->pattern_els, lctx->pattern_els_count);
    }
}

static int osp_led_init_ctx(struct led_ctx *lctx)
{
    int i;

    lctx->cur_state = OSP_LED_ST_IDLE;
    for (i = 0; i < OSP_LED_ST_LAST; i++)
    {
        lctx->state_enab[i] = false;
        lctx->state_prio[i] = OSP_LED_PRIORITY_DISABLE;
        lctx->state_def_prio[i] = OSP_LED_PRIORITY_DISABLE;
    }

    osp_led_set_idle_state(lctx);


    if (osp_led_tgt_get_def_state_priorities(lctx->state_def_prio, OSP_LED_ST_LAST) == false) {
        return -1;
    }

    /* in any case, IDLE state needs to have at least a default priority */
    if (lctx->state_def_prio[OSP_LED_ST_IDLE] == OSP_LED_PRIORITY_DISABLE) {
        lctx->state_def_prio[OSP_LED_ST_IDLE] = OSP_LED_PRIORITY_DEFAULT;
    }

    lctx->pattern_els = NULL;
    lctx->pattern_els_count = 0;

    osp_led_tgt_init();

    return 0;
}

static void osp_led_reset_ctx(struct led_ctx *lctx)
{
    int i;

    for (i = 0; i < OSP_LED_ST_LAST; i++)
        osp_led_clear_ctx_state(lctx, i);

    osp_led_set_idle_state(lctx);
    osp_led_set_new_state(lctx);
}

int osp_led_init(void)
{
    for (int i = 0; i < CONFIG_OSP_LED_COUNT; i++)
    {
        if (osp_led_init_ctx(&g_leds_ctx[i]) != 0)
        {
            return -1;
        }
    }

    return 0;
}

int osp_led_set_state(
        enum osp_led_state state,
        uint32_t priority,
        uint8_t position,
        struct osp_led_pattern_el *pattern_els,
        int pattern_els_count)
{
    struct led_ctx *lctx;

    if ((state < 0) || (state >= OSP_LED_ST_LAST)) {
        return -1;
    }

    if (position >= CONFIG_OSP_LED_COUNT) {
        return -1;
    }

    LOGD("%s: %s: %d", __func__, osp_led_state_to_str(state), priority);

    /* don't process a state if it is disabled by the target */
    lctx = &g_leds_ctx[position];
    if (lctx->state_def_prio[state] == OSP_LED_PRIORITY_DISABLE) {
        return 0;
    }

    if (state == OSP_LED_ST_CLOUD) {
        lctx->pattern_els = pattern_els;
        lctx->pattern_els_count = pattern_els_count;
    }

    osp_led_set_ctx_state(lctx, state, priority);
    osp_led_set_new_state(lctx);

    return 0;
}

int osp_led_clear_state(enum osp_led_state state, uint8_t position)
{
    struct led_ctx *lctx;

    if ((state < 0) || (state >= OSP_LED_ST_LAST) || (state == OSP_LED_ST_IDLE)) {
        return -1;
    }

    if (position >= CONFIG_OSP_LED_COUNT) {
        return -1;
    }

    LOGD("%s: %s", __func__, osp_led_state_to_str(state));

    /* don't process a state if it is disabled by the target */
    lctx = &g_leds_ctx[position];
    if (lctx->state_def_prio[state] == OSP_LED_PRIORITY_DISABLE) {
        return 0;
    }

    osp_led_clear_ctx_state(lctx, state);
    osp_led_set_new_state(lctx);

    return 0;
}

int osp_led_reset(void)
{
    for (int i = 0; i < CONFIG_OSP_LED_COUNT; i++)
    {
        osp_led_reset_ctx(&g_leds_ctx[i]);
    }
    return 0;
}

int osp_led_get_state(enum osp_led_state *state, uint32_t *priority, uint8_t position)
{
    struct led_ctx *lctx;

    if (position >= CONFIG_OSP_LED_COUNT) {
        return -1;
    }

    lctx = &g_leds_ctx[position];
    if (state) {
        *state = lctx->cur_state;
    }
    if (priority) {
        *priority = lctx->state_prio[lctx->cur_state];
    }

    return 0;
}

uint32_t osp_led_get_state_default_prio(enum osp_led_state state)
{
    /* The default state priority is the same for all positions */
    return g_leds_ctx[0].state_def_prio[state];
}
