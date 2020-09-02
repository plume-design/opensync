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

#include <libgen.h>
#include <dirent.h>

#include "const.h"
#include "log.h"
#include "ovsdb.h"
#include "schema.h"
#include "ovsdb_table.h"
#include "json_util.h"

#include "oms.h"
#include "oms_report.h"

#include "osp_ps.h"
#include "osp_objm.h"
#include "osp_dl.h"

#include "pm.h"
#include "pm_objm.h"

/*
 * Generate the PJS structures
 */
#include "pm_objm_pjs.h"
#include "pjs_gen_h.h"

#include "pm_objm_pjs.h"
#include "pjs_gen_c.h"

static ovsdb_table_t table_Object_Store_Config;
static ovsdb_table_t table_AWLAN_Node;
//static struct pm_objm_ctx_t objm_ctx;

struct ev_async     ev_install_async;

// Global list of installed pm_objects
static struct pm_objm_store db;


static void pm_ctx_to_oms_config(struct oms_config_entry *entry, struct pm_objm_ctx_t *ctx);
static void pm_ctx_to_oms_state(struct oms_state_entry *entry, struct pm_objm_ctx_t *ctx);
static void oms_state_to_pm_ctx(struct pm_objm_ctx_t *ctx, struct oms_state_entry *entry);
static void oms_state_to_oms_config(struct oms_state_entry *c_entry, struct oms_config_entry *s_entry);


/******************************************************************************
 * Support functions for interacting with persistent storage
 *****************************************************************************/

// Load object database from presistent storage
static bool pm_objm_ps_load(struct pm_objm_store *st)
{
    pjs_errmsg_t perr;
    ssize_t strsz;

    bool retval = false;
    osp_ps_t *ps = NULL;
    json_t *json = NULL;
    char *str = NULL;

    memset(st, 0, sizeof(*st));

    ps = osp_ps_open(PM_OBJM_STORAGE, OSP_PS_RDWR);
    if (ps == NULL)
    {
        LOG(ERR, "objm: Unable to open \"%s\" store.",
                PM_OBJM_STORAGE);
        goto exit;
    }

    // Load and parse the object structure
    strsz = osp_ps_get(ps, PM_OBJM_KEY, NULL, 0);
    if (strsz < 0)
    {
        LOG(ERR, "objm: Error fetching \"%s\" key size.",
                PM_OBJM_KEY);
        retval = false;
        goto exit;
    }
    else if (strsz == 0)
    {
        // The record does not exist yet
        retval = true;
        goto exit;
    }

    // Fetch the "store" data
    str = malloc((size_t)strsz);
    if (osp_ps_get(ps, PM_OBJM_KEY, str, (size_t)strsz) != strsz)
    {
        LOG(ERR, "objm: Error retrieving persistent \"%s\" key.",
                PM_OBJM_KEY);
        goto exit;
    }

    // Convert it to JSON
    json = json_loads(str, 0, NULL);
    if (json == NULL)
    {
        LOG(ERR, "objm: Error parsing JSON: %s", str);
        goto exit;
    }

    // Convert it to C struct
    if (!pm_objm_store_from_json(st, json, false, perr))
    {
        memset(st, 0, sizeof(*st));
        LOG(ERR, "objm: Error parsing pm_objm_store record: %s", perr);
        goto exit;
    }

    retval = true;

exit:
    if (str != NULL) free(str);
    if (json != NULL) json_decref(json);
    if (ps != NULL) osp_ps_close(ps);

    return retval;
}

// Store the object database to persistent storage
static bool pm_objm_ps_save(struct pm_objm_store *st)
{
    pjs_errmsg_t perr;
    ssize_t strsz;

    bool retval = false;
    osp_ps_t *ps = NULL;
    json_t *json = NULL;
    char *str = NULL;

    // Open persistent storage in read-write mode
    ps = osp_ps_open(PM_OBJM_STORAGE, OSP_PS_RDWR);
    if (ps == NULL)
    {
        LOG(ERR, "objm: Error opening \"%s\" persistent store.",
                PM_OBJM_STORAGE);
        goto exit;
    }

    // Convert the structure to JSON
    json = pm_objm_store_to_json(st, perr);
    if (json == NULL)
    {
        LOG(ERR, "objm: Error converting pm_objm_store structure to JSON: %s", perr);
        goto exit;
    }

    LOG(DEBUG, "objm_store records len= %d", st->obj_records_len);
    {
        int ii;
        for (ii = 0; ii < st->obj_records_len; ii++)
        {
            LOG(DEBUG, "objm: obj[%d].name: %s", ii, st->obj_records[ii].name);
            LOG(DEBUG, "objm: obj[%d].version: %s", ii, st->obj_records[ii].version);
        }
    }

    // Convert the reboot structure to string
    str = json_dumps(json, JSON_COMPACT);
    if (str == NULL)
    {
        LOG(ERR, "objm: Error converting JSON to string.");
        goto exit;
    }

    // Store the string representation to peristent storage
    strsz = (ssize_t)strlen(str) + 1;
    if (osp_ps_set(ps, PM_OBJM_KEY, str, (size_t)strsz) < strsz)
    {
        LOG(ERR, "objm: Error storing object records: %s", str);
        goto exit;
    }

    retval = true;

exit:
    if (str != NULL) json_free(str);
    if (json != NULL) json_decref(json);
    if (ps != NULL) osp_ps_close(ps);

    return retval;
}

static bool pm_objm_ps_add(struct pm_objm_store *st, char *name, char *version, bool fw_integrated)
{
    // Append object to database
    int dbl = st->obj_records_len;

    if (dbl == ARRAY_LEN(st->obj_records))
    {
        LOG(ERR, "objm: Max size of saved objects reached: %d", dbl);
    }

    STRSCPY(st->obj_records[dbl].name, name);
    STRSCPY(st->obj_records[dbl].version, version);
    st->obj_records[dbl].fw_integrated = fw_integrated;
    st->obj_records_len = dbl + 1;

    return pm_objm_ps_save(st);
}
/*
 *  Remove object from object database
 *
 *  Object database is implemented as array. Cleanest way to remove an element and not have
 *  any free spots in array is to remove mentioned element and move last element to removed
 *  element position. This is possible since order of element does not matter.
 *
 * */
static bool pm_objm_ps_remove(struct pm_objm_store *st, char *name, char *version)
{
    int i;
    int last;
    bool found = false;

    LOG(DEBUG, "objm: remove %s version %s from db", name, version);

    for (i = 0; i < st->obj_records_len; i++)
    {
        if (strcmp(st->obj_records[i].name, name) == 0 &&
            strcmp(st->obj_records[i].version, version) == 0)
        {
            found = true;
            break;
        }
    }

    if (!found)
    {
        LOG(ERR, "objm: Object %s:%s not found", name, version);
        return false;
    }

    // Move last element to removed element position
    last = st->obj_records_len - 1;
    STRSCPY_WARN(st->obj_records[i].name, st->obj_records[last].name);
    STRSCPY_WARN(st->obj_records[i].version, st->obj_records[last].version);
    st->obj_records[i].fw_integrated = st->obj_records[last].fw_integrated;
    st->obj_records_len -= 1;

    pm_objm_ps_save(st);
    return true;
}

// Check if object name and version is preintegrated in fw or not.
static bool pm_objm_ps_is_fw_integrated(struct pm_objm_store *st, char *name, char *version)
{
    int i;
    bool found = false;

    LOG(DEBUG, "objm: Check if %s version %s is preintegrated", name, version);

    for (i = 0; i < st->obj_records_len; i++)
    {
        if (strcmp(st->obj_records[i].name, name) == 0 &&
            strcmp(st->obj_records[i].version, version) == 0)
        {
            found = true;
            break;
        }
    }

    if (!found)
    {
        LOG(ERR, "objm: Object %s:%s not found", name, version);
        return false;
    }

    return st->obj_records[i].fw_integrated;
}

static bool pm_objm_obj_exist(struct pm_objm_store *st, char *name, char *version)
{
    int i;

    for (i = 0; i < st->obj_records_len; i++)
    {
        if (strcmp(st->obj_records[i].name, name) == 0 &&
            strcmp(st->obj_records[i].version, version) == 0)
        {
            return true;
        }
    }
    return false;
}
/******************************************************************************
 *  Support functions for interacting with OVSDB tables
 *****************************************************************************/

static bool pm_objm_ovsdb_insert_objstore_config(struct pm_objm_ctx_t *d_ctx)
{
    struct schema_Object_Store_Config store;

    if (d_ctx->fw_integrated)
    {
        LOG(DEBUG, "objm: Skip inserting fw integrated object to Object_Store_Config table: %s", d_ctx->name);
        return true;
    }

    memset(&store, 0, sizeof(store));
    store._partial_update = true;
    SCHEMA_SET_STR(store.name, d_ctx->name);
    SCHEMA_SET_STR(store.version, d_ctx->version);

    return ovsdb_table_insert(&table_Object_Store_Config, &store);
}

static bool pm_objm_get_mqtt_topic(char *buf, size_t bufsize)
{
    struct schema_AWLAN_Node *aw_node;
    void *aw_node_p;

    int count;
    int i;
    bool ret = false;

    aw_node_p = ovsdb_table_select_where(&table_AWLAN_Node,
                                         NULL,
                                         &count);
    if (count != 1)
    {
        LOG(ERR, "objm: AWLAN_Node has %d rows - expected 1", count);
        ret = false;
        goto cleanup;
    }

    aw_node = (struct schema_AWLAN_Node*)aw_node_p;

    for (i = 0; i < aw_node->mqtt_topics_len; i++)
    {
        if (strcmp(aw_node->mqtt_topics_keys[i], PM_OBJM_MQTT) == 0)
        {
            strncpy(buf, aw_node->mqtt_topics[i], bufsize);
            ret = true;
            goto cleanup;
        }
    }
cleanup:
    if (aw_node_p) free(aw_node_p);
    return ret;
}

/******************************************************************************
 * Install of package functions
 *****************************************************************************/

static bool install(struct pm_objm_ctx_t *d_ctx)
{
    struct oms_config_entry c_entry;
    struct oms_state_entry  s_entry;

    if (!d_ctx->fw_integrated)
    {
        // If object is fw_integrated don't use osp_objm_install function since
        // object is already preinstalled.
        if (!osp_objm_install(d_ctx->dl_path, d_ctx->name, d_ctx->version))
        {
            LOG(ERR, "objm: Install failed");
            STRSCPY_WARN(d_ctx->status, PM_OBJS_INSTALL_FAILED);
            goto install_failed;
        }
    }

    LOG(INFO, "objm: Successful installation of object: name: %s version: %s preintegrated: %s",
              d_ctx->name,
              d_ctx->version,
              d_ctx->fw_integrated ? "true" : "false");

    if (!pm_objm_ps_add(&db, d_ctx->name, d_ctx->version, d_ctx->fw_integrated))
    {
        LOG(ERR, "objm: Unable to store object info to database");
        STRSCPY_WARN(d_ctx->status, PM_OBJS_INSTALL_FAILED);
        goto install_failed;
    }

    STRSCPY_WARN(d_ctx->status, PM_OBJS_INSTALLED);

    // Insert to Object_Store_State
    pm_ctx_to_oms_state(&s_entry, d_ctx);
    oms_update_state_entry(&s_entry);

    // Update OMS_Config table
    pm_ctx_to_oms_config(&c_entry, d_ctx);
    oms_add_config_entry(&c_entry);

    return true;

install_failed:
    // Insert to Object_Store_State
    pm_ctx_to_oms_state(&s_entry, d_ctx);
    oms_update_state_entry(&s_entry);
    return false;
}

static void install_async(EV_P_ ev_async *w, int revents)
{
    struct pm_objm_ctx_t *d_ctx = w->data;

    // Clear url and timeout regardless of success or failure of download
    STRSCPY_WARN(d_ctx->url, "");
    d_ctx->timeout = 0;

    install(d_ctx);
    free(d_ctx);
}

static int install_integrated_objects(char *path)
{
    DIR *udir;
    FILE *fd;
    struct pm_objm_ctx_t file_ctx;
    struct dirent *next_file;
    int obj_count = 0;
    char *name_buf;
    char info_path[128];
    char *line = NULL;
    size_t len = 0;

    LOG(DEBUG, "objm: Checking %s for pre-integrated objects", path);
    udir = opendir(path);

    if (udir != NULL)
    {
        while ((next_file = readdir(udir)) != NULL)
        {
            if (next_file->d_type == DT_REG)
            {
                memset(&file_ctx, 0, sizeof(struct pm_objm_ctx_t));
                name_buf = NULL;

                LOG(DEBUG, "objm: About to install %s", next_file->d_name);
                if (strstr(next_file->d_name, ".info") == NULL)
                {
                    LOG(DEBUG, "objm: %s not a info file", next_file->d_name);
                    continue;
                }

                name_buf = strdup(next_file->d_name);

                // Open info file of the package
                sprintf(info_path, "%s/%s", path, name_buf);
                free(name_buf);
                LOG(DEBUG, "objm: Reading info file %s", info_path);
                fd = fopen(info_path, "r");
                if (fd == NULL)
                {
                    LOG(ERR, "objm: no info file found %s", info_path);
                    continue;
                }

                while (getline(&line, &len, fd) != -1)
                {
                    if (strstr(line, "name") != NULL)
                    {
                        sscanf(line, "name:%s", file_ctx.name);
                    }

                    if (strstr(line, "version") != NULL)
                    {
                        sscanf(line, "version:%s", file_ctx.version);
                    }
                }
                fclose(fd);

                file_ctx.fw_integrated = true;

                if (install(&file_ctx) == false)
                {
                    LOG(ERR, "objm: %s failed to install", next_file->d_name);
                    continue;
                }
                LOG(NOTICE, "objm: %s installed %s", file_ctx.name, file_ctx.version);
                obj_count++;
            }
        }
        closedir(udir);
    }
    return obj_count;
}

/******************************************************************************
 * Removal of package functions
 *****************************************************************************/

static void object_remove(struct pm_objm_ctx_t *d_ctx)
{
    struct oms_config_entry c_entry;
    struct oms_state_entry s_entry;

    // Check if object is preintegrated
    if (pm_objm_ps_is_fw_integrated(&db, d_ctx->name, d_ctx->version))
    {
        LOG(INFO, "Object is preintegrated - skip removing %s %s", d_ctx->name, d_ctx->version);
        return;
    }

    // Remove complete "path" folder trough osp_objm_remove api
    if (!osp_objm_remove(d_ctx->name, d_ctx->version))
    {
        LOG(ERR, "objm: remove from storage failed");
        return;
    }
    // Remove Object information from persistant storage
    pm_objm_ps_remove(&db, d_ctx->name, d_ctx->version);

    // Remove entry from OMS_Config for final user to process
    pm_ctx_to_oms_state(&s_entry, d_ctx);
    oms_delete_state_entry(&s_entry);

    // Remove entry from OMS_Config for final user to process
    pm_ctx_to_oms_config(&c_entry, d_ctx);
    oms_delete_config_entry(&c_entry);
    LOG(INFO, "objm: Successful removal of object: %s version: %s",
               d_ctx->name,
               d_ctx->version);
}


/******************************************************************************
 * Download of package functions
 *****************************************************************************/

static void cb_dl(const enum osp_dl_status status, void *ctx)
{
    struct oms_state_entry s_entry;
    struct pm_objm_ctx_t *d_ctx = (struct pm_objm_ctx_t*)ctx;

    LOG(DEBUG, "objm: (%s) status: %d", __func__, status);
    if (status != OSP_DL_OK)
    {
        LOG(ERR, "Download failed");
        STRSCPY_WARN(d_ctx->status, PM_OBJS_DOWNLOAD_FAILED);
        // Insert to Object_Store_State
        pm_ctx_to_oms_state(&s_entry, d_ctx);
        oms_update_state_entry(&s_entry);

        free(d_ctx);
        return;
    }
    STRSCPY_WARN(d_ctx->status, PM_OBJS_DOWNLOAD_DONE);

    // Insert to Object_Store_State
    pm_ctx_to_oms_state(&s_entry, d_ctx);
    oms_update_state_entry(&s_entry);

    // Call ev_async to start install
    ev_install_async.data = d_ctx;
    ev_async_send(EV_DEFAULT, &ev_install_async);
}

static void start_download(struct schema_Object_Store_Config *new)
{
    struct oms_state_entry s_entry;
    // Fill ctx struct
    struct pm_objm_ctx_t *d_ctx;
    d_ctx = malloc(sizeof(struct pm_objm_ctx_t));

    d_ctx->fw_integrated = false;
    d_ctx->timeout = new->dl_timeout;
    STRSCPY_WARN(d_ctx->url, new->dl_url);
    STRSCPY_WARN(d_ctx->name, new->name);
    STRSCPY_WARN(d_ctx->version, new->version);
    sprintf(d_ctx->dl_path, "%s/%s", CONFIG_PM_OBJM_DOWNLOAD_DIR, basename(d_ctx->url));
    STRSCPY_WARN(d_ctx->status, PM_OBJS_DOWNLOAD_STARTED);

    if (!osp_dl_download(d_ctx->url, CONFIG_PM_OBJM_DOWNLOAD_DIR, d_ctx->timeout, cb_dl, d_ctx))
    {
        LOG(ERR, "objm: failed to start osp_dl_download api");
        STRSCPY_WARN(d_ctx->status, PM_OBJS_DOWNLOAD_FAILED);
    }

    // Insert to Object_Store_State
    pm_ctx_to_oms_state(&s_entry, d_ctx);
    oms_add_state_entry(&s_entry);
}


/******************************************************************************
 * OVSDB monitor callback functions
 *****************************************************************************/

// Registered callback for Node_Config events
static void callback_Object_Store_Config(ovsdb_update_monitor_t *mon,
                    struct schema_Object_Store_Config *old_rec,
                    struct schema_Object_Store_Config *new)
{
    struct pm_objm_ctx_t rm_ctx;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            // If dl_url is empty assume loading object info from
            // persistance storage (at boot), no action needed.
            if (strcmp(new->dl_url, "") == 0)
            {
                break;
            }
            // Check if object already exists
            if (pm_objm_obj_exist(&db, new->name, new->version))
            {
                LOG(DEBUG, "objm: ignore (%s) object %s,%s already exists", __func__, new->name, new->version);
                break;
            }

            start_download(new);
            break;

        case OVSDB_UPDATE_MODIFY:
            // Install new version
            if (strcmp(old_rec->version, new->version) == 0)
            {
                // Ignore update version did not change
                LOG(DEBUG, "objm: ignoring (%s) modify - version not changed", __func__);
                break;
            }
            start_download(new);
            break;

        case OVSDB_UPDATE_DEL:
            // Remove Object information from Object_Store_State table
            STRSCPY_WARN(rm_ctx.name, old_rec->name);
            STRSCPY_WARN(rm_ctx.version, old_rec->version);
            object_remove(&rm_ctx);
            break;

        default:
            LOG(ERR, "Monitor update error.");
            return;
    }
}


/******************************************************************************
 * OMS lib support functions
 *****************************************************************************/
static void oms_state_cb(struct oms_state_entry *entry, int event)
{
    struct pm_objm_ctx_t d_ctx;
    struct oms_config_entry c_entry;
    char mqtt_topic[128];

    LOG(DEBUG, "objm: (%s) event: %d name: %s version: %s state: %s",
         __func__,
         event,
         entry->object,
         entry->version,
         entry->state);

    switch (event)
    {
        case OVSDB_UPDATE_NEW:
            break;
        case OVSDB_UPDATE_MODIFY:
            if (strcmp(entry->state, PM_OBJS_OBSOLETE) == 0)
            {
                oms_state_to_pm_ctx(&d_ctx, entry);
                object_remove(&d_ctx);
            }
            else if (strcmp(entry->state, PM_OBJS_LOAD_FAILED) == 0)
            {
                // Remove entry from OMS_Config so final user won't try to process it again
                LOG(DEBUG, "objm: %s failed to load. Removing from OMS_Config", entry->object);
                oms_state_to_oms_config(entry, &c_entry);
                oms_delete_config_entry(&c_entry);
                oms_free_config_entry(&c_entry);
            }

            break;
        case OVSDB_UPDATE_DEL:
            strcpy(entry->state, PM_OBJS_REMOVED);
            break;
    }


    if (strcmp(entry->state, PM_OBJS_OBSOLETE)        == 0 ||
        strcmp(entry->state, PM_OBJS_ACTIVE)          == 0 ||
        strcmp(entry->state, PM_OBJS_DOWNLOAD_FAILED) == 0 ||
        strcmp(entry->state, PM_OBJS_INSTALL_FAILED)  == 0 ||
        strcmp(entry->state, PM_OBJS_LOAD_FAILED)     == 0 ||
        strcmp(entry->state, PM_OBJS_REMOVED)         == 0 ||
        strcmp(entry->state, PM_OBJS_ERROR)           == 0 )
    {
        // Send mqtt report
        if (!pm_objm_get_mqtt_topic(mqtt_topic, 128))
        {
            LOG(ERR, "objm: no mqtt topic '%s' found in AWLAN_Node", PM_OBJM_MQTT);
            return;
        }
        LOG(INFO, "objm: Sending mqtt report to %s", mqtt_topic);
        oms_report_send_report(mqtt_topic);
    }
}

static bool oms_report_cb(struct oms_state_entry *entry)
{
    bool ret = true;

    LOG(DEBUG, "objm (%s): object: %s, version: %s, state: %s, reporting: %s",
        __func__, entry->object, entry->version, entry->state, ret ? "true" : "false");

    return ret;
}

static void pm_ctx_to_oms_config(struct oms_config_entry *entry, struct pm_objm_ctx_t *ctx)
{
    entry->object = strdup(ctx->name);
    entry->version = strdup(ctx->version);
}

static void oms_state_to_pm_ctx(struct pm_objm_ctx_t *ctx, struct oms_state_entry *entry)
{
    STRSCPY_WARN(ctx->name, entry->object);
    STRSCPY_WARN(ctx->version, entry->version);
    STRSCPY_WARN(ctx->status, entry->state);
}

static void pm_ctx_to_oms_state(struct oms_state_entry *entry, struct pm_objm_ctx_t *ctx)
{
    entry->object = strdup(ctx->name);
    entry->version = strdup(ctx->version);
    entry->state = strdup(ctx->status);
    entry->fw_integrated = ctx->fw_integrated;
}

static void oms_state_to_oms_config(struct oms_state_entry *s_entry, struct oms_config_entry *c_entry)
{
    c_entry->object = strdup(s_entry->object);
    c_entry->version = strdup(s_entry->version);
}


/******************************************************************************
 * Init functions
 *****************************************************************************/

static bool pm_objm_ovsdb_init(void)
{
    LOG(DEBUG, "objm: Enable ovsdb monitor of Object_Store_Config");
    OVSDB_TABLE_INIT_NO_KEY(Object_Store_Config);
    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
    OVSDB_TABLE_MONITOR(Object_Store_Config, false);
    return true;
}

bool pm_objm_init(void)
{
    struct oms_ovsdb_set oms_set;
    struct oms_config_entry c_entry;
    struct oms_state_entry s_entry;

    static struct pm_objm_ctx_t objm_ctx;

    LOG(NOTICE, "objm: Initializing Object Upgrade");

    ev_async_init(&ev_install_async, install_async);
    ev_install_async.data = &objm_ctx;
    ev_async_start(EV_DEFAULT, &ev_install_async);

    if (!pm_objm_ovsdb_init()) {
        return false;
    }

    oms_init_manager();
    memset(&oms_set, 0, sizeof(oms_set));
    oms_set.monitor_config = false;
    oms_set.monitor_state = true;
    oms_set.monitor_awlan = true;
    oms_set.accept_id = NULL;
    oms_set.config_cb = NULL;
    oms_set.state_cb = oms_state_cb;
    oms_set.report_cb = oms_report_cb;

    oms_ovsdb_init(&oms_set);

    // Load database of installed object from presistent storage
    memset(&db, 0, sizeof(struct pm_objm_store));
    if (pm_objm_ps_load(&db) == false)
    {
        LOG(ERR, "objm: failed to load db from ps");
        return false;
    }

    if (db.obj_records_len == 0)
    {
        LOG(NOTICE, "objm: No objects in persistant database - checking integrated packages");
        if (install_integrated_objects(CONFIG_PM_OBJM_INTEGRATED_DIR) > 0)
        {
            // Reload database after installing preintegrated packages
            if (pm_objm_ps_load(&db) == false)
            {
                LOG(ERR, "objm: failed to load db from ps");
                return false;
            }
        }
    }

    // Copy database to ovs table (Object_Store_Config)
    int ii;
    for (ii = 0; ii < db.obj_records_len; ii++)
    {
        LOG(DEBUG, "objm: pm_obj[%d].name: %s", ii, db.obj_records[ii].name);
        LOG(DEBUG, "objm: pm_obj[%d].version: %s", ii, db.obj_records[ii].version);
        LOG(DEBUG, "objm: pm_obj[%d].fw_integrated: %d", ii, db.obj_records[ii].fw_integrated);
        STRSCPY_WARN(objm_ctx.name, db.obj_records[ii].name);
        STRSCPY_WARN(objm_ctx.version, db.obj_records[ii].version);
        objm_ctx.fw_integrated = db.obj_records[ii].fw_integrated;
        STRSCPY_WARN(objm_ctx.status, PM_OBJS_INSTALLED);
        // Insert to Object_Store_Config for cloud
        pm_objm_ovsdb_insert_objstore_config(&objm_ctx);
        // Insert to Object_Store_State
        pm_ctx_to_oms_state(&s_entry, &objm_ctx);
        oms_add_state_entry(&s_entry);
        // Insert to OMS_Config for final user of object
        pm_ctx_to_oms_config(&c_entry, &objm_ctx);
        oms_add_config_entry(&c_entry);
    }

    LOG(INFO, "objm: %d objects info loaded from persistent storage to ovsdb table Object_Storage", db.obj_records_len);

    return true;
}
