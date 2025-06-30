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

/*
 * ===========================================================================
 *  Request via Node_State m/k/v to not reboot due to a critical task
 * ===========================================================================
 */
#include <dirent.h>
#include <jansson.h>

#include "const.h"
#include "log.h"
#include "module.h"
#include "os.h"
#include "dm.h"
#include "reboot_flags.h"
#include "ovsdb_table.h"
#include "ovsdb_update.h"

#define CMD_LEN (C_MAXPATH_LEN * 2 + 128)

MODULE(dm_no_reboot, dm_no_reboot_init, dm_no_reboot_fini)

static ev_stat stat_watcher;
static ovsdb_table_t table_Node_State;
static size_t g_num_files_old;
static size_t g_num_files;

static void node_state_upsert(const char *module, const char *key, const char *value, bool persist)
{
    struct schema_Node_State node_state;
    json_t *where;

    where = json_array();
    json_array_append_new(where, ovsdb_tran_cond_single("module", OFUNC_EQ, (char *)module));
    json_array_append_new(where, ovsdb_tran_cond_single("key", OFUNC_EQ, (char *)key));

    MEMZERO(node_state);
    SCHEMA_SET_STR(node_state.module, module);
    SCHEMA_SET_STR(node_state.key, key);

    if (value != NULL)
    {
        SCHEMA_SET_STR(node_state.value, value);
        if (persist) SCHEMA_SET_BOOL(node_state.persist, persist);
        ovsdb_table_upsert_where(&table_Node_State, where, &node_state, false);
    }
    else
    {
        ovsdb_table_delete_where(&table_Node_State, where);
    }
}

static void stat_cb(struct ev_loop *loop, ev_stat *w, int revent)
{
    size_t file_count = 0;
    DIR *dir;
    struct dirent *entry;
    dir = opendir(CONFIG_NO_REBOOT_DIR);
    if (dir == NULL)
    {
        LOGE("NO-REBOOT: could not open %s, not able to count files", CONFIG_NO_REBOOT_DIR);
        return;
    }
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_REG)
        {
            LOGD("NO-REBOOT: filename: %s", entry->d_name);
            file_count++;
        }
    }
    LOGI("NO-REBOOT: counted %d files", (int)file_count);
    closedir(dir);

    g_num_files_old = g_num_files;
    g_num_files = file_count;

    if ((g_num_files > 0) && (g_num_files_old == 0))
    {
        LOGI("NO-REBOOT: toggled to non-empty, requesting not to reboot");
        node_state_upsert("no_reboot", "no_reboot", "true", false);
    }
    if ((g_num_files == 0) && (g_num_files_old > 0))
    {
        LOGI("NO-REBOOT: toggled back to empty, clearing do not reboot request");
        node_state_upsert("no_reboot", "no_reboot", "false", false);
    }
}

static bool no_reboot_mkdir(char *path)
{
    char cmd[CMD_LEN];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", path);

    LOG(TRACE, "NO-REBOOT: mkdir: %s", cmd);
    if (cmd_log_check_safe(cmd))
    {
        return false;
    }
    return true;
}

void dm_no_reboot_init(void *data)
{
    if (no_reboot_mkdir(CONFIG_NO_REBOOT_DIR) == false)
    {
        LOG(ERR, "NO-REBOOT: Error creating working directory %s", CONFIG_NO_REBOOT_DIR);
        return;
    }
    LOG(INFO, "NO-REBOOT: Working directory created: %s", CONFIG_NO_REBOOT_DIR);

    no_reboot_clear_all();
    g_num_files_old = 0;
    g_num_files = 0;

    OVSDB_TABLE_INIT_NO_KEY(Node_State);

    ev_stat_init(&stat_watcher, stat_cb, CONFIG_NO_REBOOT_DIR, 0.);
    ev_stat_start(EV_DEFAULT, &stat_watcher);
}

void dm_no_reboot_fini(void *data)
{
    LOG(INFO, "NO-REBOOT: Finishing.");
    no_reboot_clear_all();
    ev_stat_stop(EV_DEFAULT, &stat_watcher);
}

bool dm_no_reboot_set(char *module)
{
    return no_reboot_set(module);
}

bool dm_no_reboot_clear(char *module)
{
    return no_reboot_clear(module);
}

bool dm_no_reboot_get(char *module)
{
    bool ret;
    ret = no_reboot_get(module);
    if (ret)
    {
        printf("True. Do not reboot request for module %s is active.\n", module);
    }
    else
    {
        printf("False. Do not reboot request for module %s is not active.\n", module);
    }
    return ret;
}

bool dm_no_reboot_clear_all(void)
{
    return no_reboot_clear_all();
}

bool dm_no_reboot_list(void)
{
    return no_reboot_dump_modules();
}
