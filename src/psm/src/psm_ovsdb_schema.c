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

#include "ds_tree.h"
#include "json_util.h"
#include "log.h"
#include "memutil.h"
#include "ovsdb_sync.h"
#include "ovsdb_update.h"

#include "psm.h"

struct psm_ovsdb_schema_table
{
    char                       *st_table;       /* Table name */
    ds_tree_t                   st_column_list; /* List of columns */
    ovsdb_update_monitor_t      st_mon;         /* Table monitor object */
    ds_tree_node_t              st_tnode;       /* Tree node */
};

struct psm_ovsdb_schema_column
{
    char                       *sc_column;      /* Column name */
    bool                        sc_ephemeral;   /* True if column is ephemeral */
    ds_tree_node_t              sc_tnode;       /* Tree node */
};

static bool psm_ovsdb_schema_parse(json_t *schema);
static ovsdb_update_cbk_t psm_ovsdb_schema_table_monitor;

static ds_tree_t g_psm_ovsdb_schema_table_list = DS_TREE_INIT(
        ds_str_cmp,
        struct psm_ovsdb_schema_table,
        st_tnode);

/*
 * ===========================================================================
 *  Public interface
 * ===========================================================================
 */

/*
 * Get the schema and parse it. If monitor is set to true, all tables that
 * contain the `os_persist` column will be monitored for updates.
 */
bool psm_ovsdb_schema_init(bool monitor)
{
    struct psm_ovsdb_schema_table *ptable;

    bool retval = false;
    json_t *schema = NULL;

    json_t *jp = json_array();
    json_array_append_new(jp, json_string("Open_vSwitch"));
    schema = ovsdb_method_send_s(MT_GET_SCHEMA, jp);
    if (schema == NULL)
    {
        LOG(ERR, "Error downloading schema from OVSDB server.");
        goto exit;
    }

    if (!psm_ovsdb_schema_parse(schema))
    {
        goto exit;
    }

    if (!monitor)
    {
        retval = true;
        goto exit;
    }

    /* Start monitoring tables */
    ds_tree_foreach(&g_psm_ovsdb_schema_table_list, ptable)
    {
        LOG(INFO, "Monitoring table: %s", ptable->st_table);
        ovsdb_update_monitor(&ptable->st_mon, psm_ovsdb_schema_table_monitor, ptable->st_table, 0);
    }

    retval = true;

exit:
    json_decref(schema);
    return retval;
}

/*
 * Return true whether table:column exists in the schema definition
 */
bool psm_ovsdb_schema_column_exists(const char *table, const char *column)
{
    struct psm_ovsdb_schema_table *ptable;
    struct psm_ovsdb_schema_column *pcolumn;

    ptable = ds_tree_find(&g_psm_ovsdb_schema_table_list, (void *)table);
    if (ptable == NULL) return false;

    pcolumn = ds_tree_find(&ptable->st_column_list, (void *)column);
    if (pcolumn == NULL) return false;

    return true;
}

/*
 * Return true whether table:column is an ephemeral column
 *
 * This function returns false if the column doesn't exist
 */
bool psm_ovsdb_schema_column_is_ephemeral(const char *table, const char *column)
{
    struct psm_ovsdb_schema_table *ptable;
    struct psm_ovsdb_schema_column *pcolumn;

    ptable = ds_tree_find(&g_psm_ovsdb_schema_table_list, (void *)table);
    if (ptable == NULL) return false;

    pcolumn = ds_tree_find(&ptable->st_column_list, (void *)column);
    if (pcolumn == NULL) return false;

    return pcolumn->sc_ephemeral;
}


/*
 * ===========================================================================
 *  Private functions
 * ===========================================================================
 */
/*
 * This callback is called when the schema is downloaded
 */
bool psm_ovsdb_schema_parse(json_t *schema)
{
    struct psm_ovsdb_schema_table *ptable;
    struct psm_ovsdb_schema_column *pcolumn;
    const char *table_name;
    json_t *table_list;
    json_t *version;
    json_t *table;
    json_t *columns;
    json_t *os_persist;
    const char *key;
    json_t *val;

    version = json_object_get(schema, "version");
    if (version == NULL)
    {
        LOG(ERR, "Invalid schema format -- version field missing.");
        return false;
    }

    LOG(INFO, "Schema version %s", json_string_value(version));

    table_list = json_object_get(schema, "tables");
    if (table_list == NULL || !json_is_object(table_list))
    {
        LOG(ERR, "Invalid schema format -- tables field missing or not an object.");
        return false;
    }

    json_object_foreach(table_list, table_name, table)
    {
        columns = json_object_get(table, "columns");
        if (columns == NULL)
        {
            LOG(WARN, "Table %s is missing the `columns` field.", table_name);
            continue;
        }

        /* Filter out tables that do not have the `os_persist` flag */
        os_persist = json_object_get(columns, PSM_TABLE_KEY);
        if (os_persist == NULL)
        {
            continue;
        }

        ptable = CALLOC(1, sizeof(*ptable));
        ptable->st_table = STRDUP(table_name);
        ds_tree_init(&ptable->st_column_list, ds_str_cmp, struct psm_ovsdb_schema_column, sc_tnode);

        /* Scan table columns and add it to the table's list of columns */
        json_object_foreach(columns, key, val)
        {
            pcolumn = CALLOC(1, sizeof(*pcolumn));
            pcolumn->sc_column = STRDUP(key);
            pcolumn->sc_ephemeral = json_boolean_value(json_object_get(val, "ephemeral"));
            ds_tree_insert(&ptable->st_column_list, pcolumn, pcolumn->sc_column);
        }

        ds_tree_insert(&g_psm_ovsdb_schema_table_list, ptable, ptable->st_table);
    }

    return true;
}

void psm_ovsdb_schema_table_monitor(ovsdb_update_monitor_t *self)
{
    if (self->mon_json_new != NULL)
    {
        LOG(DEBUG, "Update row: %s: %s", self->mon_table,
                json_dumps_static(self->mon_json_new, 0));

        if (!psm_ovsdb_row_update(self->mon_table, self->mon_json_new))
        {
            LOG(ERR, "Error saving row in table %s: %s",
                    self->mon_table,
                    json_dumps_static(self->mon_json_new, 0));
        }
    }
    else
    {
        LOG(DEBUG, "Delete row: %s: %s", self->mon_table,
                json_dumps_static(self->mon_json_old, 0));

        if (!psm_ovsdb_row_delete(self->mon_table, self->mon_json_old))
        {
            LOG(ERR, "Error deleting row in table %s: %s",
                    self->mon_table,
                    json_dumps_static(self->mon_json_new, 0));
        }
    }
}
