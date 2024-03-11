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

#include "fm.h"

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "ovsdb.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_update.h"
#include "schema.h"

#define MODULE_ID LOG_MODULE_ID_OVSDB

#define FM_OVSDB_MAX_KEY_LEN 32
#define FM_OVSDB_MODULE "FM"
#define FM_OVSDB_LOG_KEY_STATE "FMLOG_options"
#define FM_OVSDB_LOG_FULL "flash_ramoops"
#define FM_OVSDB_LOG_FLASH_ONLY "flash_only"
#define FM_OVSDB_LOG_RAMOOPS_ONLY "ramoops_only"
#define FM_OVSDB_LOG_OFF "off"

/* Default configuration */
#ifdef CONFIG_FM_LOG_TO_RAMOOPS
/* ramoops enabled */
#ifdef CONFIG_FM_LOG_TO_FLASH
/* ramoops and flash enabled */
#define FM_OVSDB_LOG_DEFAULT_STATE FM_OVSDB_LOG_FULL
#else
/* only ramoops enabled */
#define FM_OVSDB_LOG_DEFAULT_STATE FM_OVSDB_LOG_RAMOOPS_ONLY
#endif /* CONFIG_FM_LOG_TO_FLASH */
#else
/* ramoops disabled */
#ifdef CONFIG_FM_LOG_TO_FLASH
/* only flash enabled */
#define FM_OVSDB_LOG_DEFAULT_STATE FM_OVSDB_LOG_FLASH_ONLY
#else
/* flash and ramoops disabled */
#define FM_OVSDB_LOG_DEFAULT_STATE FM_OVSDB_LOG_OFF
#endif /* CONFIG_FM_LOG_TO_FLASH */
#endif /* CONFIG_FM_LOG_TO_RAMOOPS */

#ifdef CONFIG_FM_USE_PERSIST
#define FM_OVSDB_LOG_DEFAULT_PERSIST_STATE true
#else
#define FM_OVSDB_LOG_DEFAULT_PERSIST_STATE false
#endif /* CONFIG_FM_USE_PERSIST */

static ovsdb_table_t table_Node_Config;
static ovsdb_table_t table_Node_State;

static int fm_ovsdb_set_Node_log_state(const char *val, const bool persist, const bool persist_exists);
static bool fm_is_node_config_used(struct schema_Node_Config *node);
static int fm_calculate_configuration(const char *val, fm_log_type_t *options);
static int fm_update_logging(const char *val);
static int fm_get_Node_State(struct schema_Node_State *nstate);
static void fm_handle_Node_Config_Update(ovsdb_update_monitor_t *mon, struct schema_Node_Config *rec);

static int fm_ovsdb_set_Node_log_state(const char *val, const bool persist, const bool persist_exists)
{
    struct schema_Node_State node_state;
    int ret;

    MEMZERO(node_state);
    STRSCPY_WARN(node_state.module, FM_OVSDB_MODULE);
    STRSCPY_WARN(node_state.key, FM_OVSDB_LOG_KEY_STATE);
    STRSCPY_WARN(node_state.value, val);
    node_state.persist_exists = persist_exists;
    node_state.persist = persist;

    ret = ovsdb_table_upsert_where(
            &table_Node_State,
            ovsdb_where_simple(SCHEMA_COLUMN(Node_State, key), FM_OVSDB_LOG_KEY_STATE),
            &node_state,
            false);

    if (ret != 1)
    {
        return ret;
    }

    return 0;
}

static bool fm_is_node_config_used(struct schema_Node_Config *node)
{
    return !strcmp(node->module, FM_OVSDB_MODULE) && !strcmp(node->key, FM_OVSDB_LOG_KEY_STATE);
}

static int fm_calculate_configuration(const char *val, fm_log_type_t *options)
{
    int ret = 0;

    if (!strcmp(val, FM_OVSDB_LOG_FULL))
    {
        options->fm_log_flash = true;
        options->fm_log_ramoops = true;
    }
    else if (!strcmp(val, FM_OVSDB_LOG_FLASH_ONLY))
    {
        options->fm_log_flash = true;
    }
    else if (!strcmp(val, FM_OVSDB_LOG_RAMOOPS_ONLY))
    {
        options->fm_log_ramoops = true;
    }
    else
    {
        LOGW("Log option not supported: %s", val);
    }

    return ret;
}

static int fm_update_logging(const char *val)
{
    int ret = 0;
    fm_log_type_t options = {.fm_log_flash = false, .fm_log_ramoops = false};

    ret = fm_calculate_configuration(val, &options);
    if (ret)
    {
        goto exit;
    }

    fm_set_logging(options);

exit:
    return ret;
}

static int fm_get_Node_State(struct schema_Node_State *nstate)
{
    json_t *where;
    json_t *cond;

    where = json_array();

    cond = ovsdb_tran_cond_single("module", OFUNC_EQ, FM_OVSDB_MODULE);
    json_array_append_new(where, cond);

    cond = ovsdb_tran_cond_single("key", OFUNC_EQ, FM_OVSDB_LOG_KEY_STATE);
    json_array_append_new(where, cond);

    return ovsdb_table_select_one_where(&table_Node_State, where, nstate);
}

static void fm_handle_Node_Config_Update(ovsdb_update_monitor_t *mon, struct schema_Node_Config *rec)
{
    struct schema_Node_State nstate;
    bool value_changed;
    bool persist_changed;
    int ret;

    if (!fm_is_node_config_used(rec))
    {
        return;
    }

    memset(&nstate, 0, sizeof(nstate));
    value_changed = true;
    persist_changed = true;

    if (!rec->value_exists)
    {
        LOGW("Node_Config invalid value state");
        return;
    }

    ret = fm_get_Node_State(&nstate);
    if (ret)
    {
        value_changed = !nstate.value_exists || strcmp(rec->value, nstate.value);
        persist_changed = rec->persist_exists && (!nstate.persist_exists || rec->persist != nstate.persist);
    }

    if (value_changed)
    {
        ret = fm_update_logging(rec->value);
        if (ret != 0)
        {
            LOGW("Set new log state returned error %d", ret);
            return;
        }

        LOGI("Set new log state: %s", rec->value);
    }

    if (value_changed || persist_changed)
    {
        ret = fm_set_persistent(rec->persist_exists && rec->persist, rec->value, strlen(rec->value) + 1);
        if (ret < 0)
        {
            LOGW("Set persistent state returned error %d", ret);
            return;
        }

        LOGI("Set new persist state: exist %d, state %d", rec->persist_exists, rec->persist_exists ? rec->persist : 0);
    }

    if (value_changed || persist_changed)
    {
        ret = fm_ovsdb_set_Node_log_state(rec->value, rec->persist, rec->persist_exists);
        if (ret != 0)
        {
            LOGW("Set log state in OVS returned error %d", ret);
        }
    }
}

int fm_ovsdb_set_default_log_state(void)
{
    char buf[100];
    char *fm_state;
    int ret;
    bool persist;

    memset(&buf, 0, sizeof(buf));
    ret = fm_get_persistent(buf, sizeof(buf) - 1);

    if (ret != 0)
    {
        LOGI("Set default log state: %s", FM_OVSDB_LOG_DEFAULT_STATE);
        fm_state = FM_OVSDB_LOG_DEFAULT_STATE;
        persist = FM_OVSDB_LOG_DEFAULT_PERSIST_STATE;
    }
    else
    {
        LOGI("Set persistent log_state: %s ", buf);
        fm_state = buf;
        persist = true;
    }

    ret = fm_update_logging(fm_state);
    if (ret != 0)
    {
        LOGW("Update log state returned error %d", ret);
        goto exit;
    }

    ret = fm_ovsdb_set_Node_log_state(fm_state, persist, true);
    if (ret != 0)
    {
        LOGW("Set default log state in OVS returned error %d", ret);
    }

exit:
    return ret;
}

void callback_Node_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Node_Config *old_rec,
        struct schema_Node_Config *rec)
{
    LOGD("%s mon_type = %d", __func__, mon->mon_type);

    switch (mon->mon_type)
    {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            LOGI("Node_Config, row removed [%s, %s]", old_rec->key, old_rec->value);
            if (fm_is_node_config_used(old_rec))
            {
                WARN_ON(fm_ovsdb_set_default_log_state());
            }
            break;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            LOGI("Node_Config, row added/updated [%s, %s]", rec->key, rec->value);
            fm_handle_Node_Config_Update(mon, rec);
            break;
    }
    return;
}

int fm_ovsdb_init(void)
{
    int ret = 0;

    LOGI("Initializing Log Rotate (FM) OVSDB tables");

    OVSDB_TABLE_INIT_NO_KEY(Node_Config);
    OVSDB_TABLE_INIT_NO_KEY(Node_State);

    OVSDB_TABLE_MONITOR(Node_Config, false);

    ret = fm_ovsdb_set_default_log_state();
    if (ret)
    {
        LOGE("Initializing Log Rotate (FM) OVSDB tables: set default state returned error %d", ret);
        ret = -1;
    }

    return ret;
}
