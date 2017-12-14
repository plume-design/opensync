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

#include <stdbool.h>
#include <string.h>
#include <jansson.h>

#include "log.h"
#include "json_util.h"
#include "ovsdb.h"
#include "ovsdb_priv.h"
#include "ovsdb_table.h"

#define MODULE_ID LOG_MODULE_ID_OVSDB

// HELPERS

json_t* ovsdb_where_simple(const char *column, const char *value)
{
    if (!column || !*column || !value || !*value) {
        LOG(WARNING, "%s: invalid selection column:%s value:%s",
                __FUNCTION__, column, value);
    }
    return ovsdb_tran_cond(OCLM_STR, (char*)column, OFUNC_EQ, (char*)value);
}

json_t* ovsdb_where_uuid(const char *column, const char *uuid)
{
    return ovsdb_tran_cond(OCLM_UUID, (char*)column, OFUNC_EQ, (char*)uuid);
}

json_t* ovsdb_mutation(char *column, json_t *mutation, json_t *value)
{
    json_t * js;

    js = json_array();
    json_array_append_new(js, json_string(column));
    json_array_append_new(js, mutation);
    json_array_append_new(js, value);

    return js;
}

// parse update result, return count or -1 on error
// offset can be used with multiple operations to select a specific result
// result ref is not decreased automatically with this function
int ovsdb_get_update_result_count_off(json_t *result, char *table, char *oper, int offset)
{
    // success: result: [{}, {"count": 1}]
    // error: result: [{}, {"error": ...}]
    int count = -1;
    int offt = 0;
    json_t *jstatus = NULL;
    json_t *jerr = NULL;
    json_t *jcount = NULL;
    json_t *jfirst;

    if (result == NULL)
    {
        LOG(ERR, "Table %s %s result: NULL", table, oper);
        goto out;
    }

    /* Check if the first element is an empty array, skip it in that case */
    jfirst = json_array_get(result, 0);
    if (json_is_object(jfirst) && json_object_size(jfirst) == 0)
    {
        offt = 1;
    }

    LOG(DEBUG, "Table %s %s result: %s off: %d", table, oper, json_dumps_static(result, 0), offset);
    jstatus = json_array_get(result, offset + offt);
    if (!jstatus) {
        LOG(ERR, "Table %s %s: no status", table, oper);
        goto out;
    }
    jerr = json_object_get(jstatus, "error");
    if (jerr) {
        char errstr[256] = "";
        json_gets(jerr, errstr, sizeof(errstr), JSON_ENCODE_ANY);
        LOG(ERR, "Table %s %s error: %s status: %s", table, oper, errstr,
                json_dumps_static(jstatus, JSON_ENCODE_ANY));
        goto out;
    }
    jcount = json_object_get(jstatus, "count");
    if (!jcount) {
        LOG(ERR, "Table %s %s error: no count", table, oper);
        goto out;
    }
    count = json_integer_value(jcount);
    LOG(DEBUG, "Table %s %s count: %d", table, oper, count);
out:
    return count;

}

// return count and decrease ref of result
int ovsdb_get_update_result_count(json_t *result, char *table, char *oper)
{
    int count = ovsdb_get_update_result_count_off(result, table, oper, 0);
    json_decref(result);
    return count;
}

// parse insert result, return true on success and store new uuid if not NULL
bool ovsdb_get_insert_result_uuid(json_t *result, char *table, char *oper, ovs_uuid_t *uuid)
{
    const char *str_uuid;
    size_t idx;

    bool success = false;
    json_t *jstatus = NULL;
    json_t *jerr = NULL;
    json_t *juuid = NULL;

    if (result == NULL)
    {
        LOG(ERR, "Table %s %s result: NULL", table, oper);
        goto out;
    }

    if (!json_is_array(result))
    {
        LOG(ERR, "Table %s %s result: not an array.", table, oper);
        goto out;
    }

    /*
     * Scan the result array and find first non-empty object
     */
    for (idx = 0; idx < json_array_size(result); idx++)
    {
        jstatus = json_array_get(result, idx);

        if (!json_is_object(jstatus)) continue;
        if (json_object_size(jstatus) == 0) continue;

        break;
    }

    if (idx >= json_array_size(result) || jstatus == NULL)
    {
        LOG(ERR, "Table %s %s: no status: %s", table, oper, json_dumps_static(result, 0));
        goto out;
    }

    jerr = json_object_get(jstatus, "error");
    if (jerr) {
        LOG(ERR, "Table %s %s error: %s", table, oper, json_dumps_static(jerr, 0));
        goto out;
    }
    juuid = json_object_get(jstatus, "uuid");
    if (!juuid) {
        LOG(ERR, "Table %s %s: no uuid", table, oper);
        goto out;
    }
    str_uuid = json_string_value(json_array_get(juuid, 1));
    LOG(DEBUG, "Table %s %s uuid: %s", table, oper, str_uuid);
    if (uuid)
    {
        strlcpy(uuid->uuid, str_uuid, sizeof(uuid->uuid));
        LOG(DEBUG, "%s %s: stored uuid: %s", table, oper, uuid->uuid);
    }
    success = true;

out:
    json_decref(result);
    return success;
}


// SELECT
// if where is NULL, select ALL rows in table

json_t* ovsdb_sync_select_where(char *table, json_t *where)
{
    json_t *jrows = NULL;
    json_t *result = NULL;

    result = ovsdb_tran_call_s(table, OTR_SELECT, where, NULL);
    if (!result)
    {
        LOG(DEBUG, "%s: null result", __FUNCTION__);
        return NULL;
    }
    LOG(DEBUG, "query result: %s", json_dumps_static(result, 0));
    jrows = json_object_get(json_array_get(result, 0), "rows");
    json_decref(result);
    if (!jrows || json_array_size(jrows) < 1)
    {
        LOG(DEBUG, "%s: no result", __FUNCTION__);
        return NULL;
    }
    LOG(DEBUG, "%s return: %s", __FUNCTION__, json_dumps_static(jrows, 0));

    return jrows;
}

json_t* ovsdb_sync_select(char *table, char *column, char *value)
{
    json_t *where = ovsdb_where_simple(column, value);
    return ovsdb_sync_select_where(table, where);
}

// 'where' should match a single row.
// return count matched, -1 on error
int ovsdb_sync_get_uuid_and_count(char *table, json_t *where, ovs_uuid_t *uuid)
{
    int count = -1;
    json_t *jrows = NULL;
    json_t *juuid = NULL;
    const char *str_uuid = NULL;
    ovs_uuid_t uuid1;

    if (!uuid) uuid = &uuid1;
    // select
    jrows = ovsdb_sync_select_where(table, where);
    // count
    count = json_array_size(jrows);
    if (count == 0)
    {
        LOG(TRACE, "get uuid %s where '%s': no records found",
                table, json_dumps_static(where, 0));
        goto out;
    }
    if (count > 1)
    {
        LOG(ERR, "get uuid %s where '%s' unexpected count: %d",
                table, json_dumps_static(where, 0), count);
        goto out;
    }
    // extract uuid
    juuid = json_object_get(json_array_get(jrows, 0), "_uuid");
    str_uuid = json_string_value(json_array_get(juuid, 1));
    if (!juuid || !str_uuid || !*str_uuid)
    {
        LOG(ERR, "get uuid %s where '%s' count: %d no uuid",
                table, json_dumps_static(where, 0), count);
        count = -1; // mark failure
        goto out;
    }
    LOG(TRACE, "get uuid %s where '%s' count: %d uuid: %s",
            table, json_dumps_static(where, 0), count, str_uuid);
out:
    strlcpy(uuid->uuid, str_uuid ? str_uuid : "", sizeof(uuid->uuid));
    json_decref(jrows);
    return count;
}

// 'where' should match a single row.
bool ovsdb_sync_get_uuid_where(char *table, json_t *where, ovs_uuid_t *uuid)
{
    return 1 == ovsdb_sync_get_uuid_and_count(table, where, uuid);
}

// 'where' should match a single row.
bool ovsdb_sync_get_uuid(char *table, char *column, char *value, ovs_uuid_t *uuid)
{
    json_t *where = ovsdb_where_simple(column, value);
    return ovsdb_sync_get_uuid_where(table, where, uuid);
}

// INSERT

bool ovsdb_sync_insert(char *table, json_t *row, ovs_uuid_t *uuid)
{
    json_t *result = NULL;

    LOG(DEBUG, "Table %s insert: %s", table, json_dumps_static(row, 0));
    result = ovsdb_tran_call_s(table, OTR_INSERT, NULL, row);
    // errors handled by:
    return ovsdb_get_insert_result_uuid(result, table, "insert", uuid);
}


// DELETE

// return count or -1 on error
int ovsdb_sync_delete_where(char *table, json_t *where)
{
    int rc;

    json_t *result = NULL;

    LOG(DEBUG, "Table %s delete where %s", table, json_dumps_static(where, 0));
    result = ovsdb_tran_call_s(table, OTR_DELETE, where, NULL);
    // errors handled by:
    rc =  ovsdb_get_update_result_count(result, table, "delete");
    if (rc == 0)
    {
        // warn if no match - nothing deleted
        LOG(WARNING, "OVSDB: Delete from table '%s' no match", table);
    }

    return rc;
}


// UPDATE

// return count or -1 on error
// if where is NULL, update ALL rows in table
int ovsdb_sync_update_where(char *table, json_t *where, json_t *row)
{
    int rc;

    json_t *result = NULL;

    LOG(DEBUG, "Table %s update: %s", table, json_dumps_static(row, 0));
    result = ovsdb_tran_call_s(table, OTR_UPDATE, where, row);
    // successfull result: [{}, {"count": 1}]
    // errors handled by:
    rc =  ovsdb_get_update_result_count(result, table, "update");
    if (rc == 0)
    {
        // warn if no match - nothing updated
        LOG(DEBUG, "OVSDB: Update table '%s' where '%s': no match",
                table, json_dumps_static(where, 0));
    }

    return rc;
}


// return count
int ovsdb_sync_update(char *table, char *column, char *value, json_t *row)
{
    json_t *where = ovsdb_where_simple(column, value);
    return ovsdb_sync_update_where(table, where, row);
}

// 'where' should match a single row.
// return count matched or -1 on error
int ovsdb_sync_update_one_get_uuid(char *table, json_t *where, json_t *row, ovs_uuid_t *uuid)
{
    int count;

    json_incref(where);
    count = ovsdb_sync_get_uuid_and_count(table, where, uuid);
    if (count != 1) {
        json_decref(where);
        json_decref(row);
        return count;
    }
    count = ovsdb_sync_update_where(table, where, row);
    if (count != 1)
    {
        // previous select returned 1, count should still be 1
        LOG(ERR, "Table %s update one where '%s' unexpected count: %d",
            table, json_dumps_static(where, 0), count);
    }
    return count;
}

// UPSERT

// uuid is optional, 'where' should match a single row
bool ovsdb_sync_upsert_where(char *table, json_t *where, json_t *row, ovs_uuid_t *uuid)
{
    int count;

    json_incref(row);
    count = ovsdb_sync_update_one_get_uuid(table, where, row, uuid);
    if (count == 1)
    {
        json_decref(row);
        return true;
    }
    else if (count == 0)
    {
        LOG(TRACE, "upsert %s: update where '%s': no match - performing insert",
                table, json_dumps_static(where, 0));
        return ovsdb_sync_insert(table, row, uuid);
    }
    else
    {
        LOG(ERR, "%s: unexpected count: %d", __FUNCTION__, count);
        json_decref(row);
    }
    return false;
}


bool ovsdb_sync_upsert(char *table, char *column, char *value, json_t *row, ovs_uuid_t *uuid)
{
    if (!row) return false;
    json_t *where = ovsdb_where_simple(column, value);
    return ovsdb_sync_upsert_where(table, where, row, uuid);
}


// MUTATE

// return count or -1 on error
int ovsdb_sync_mutate_uuid_set(char *table,
        json_t *where, char *column, ovsdb_tro_t op, char *uuid)
{
    json_t * js_uuid = ovsdb_tran_uuid_json(uuid);
    json_t * js;
    json_t * js_mutations;

    LOG(INFO, "Mutate: %s %s %d %s", table, column, op, uuid);

    js = ovsdb_mutation(column,
            ovsdb_tran_operation(op),
            ovsdb_tran_array_to_set(js_uuid, false));

    js_mutations = json_array();
    json_array_append_new(js_mutations, js);

    json_t* result = ovsdb_tran_call_s(
            table,
            OTR_MUTATE,
            where,
            js_mutations);

    return ovsdb_get_update_result_count(result, table, "mutate");
}


// WITH PARENT


bool ovsdb_sync_insert_with_parent(char *table, json_t *row, ovs_uuid_t *uuid,
        char *parent_table, json_t *parent_where, char *parent_column)
{
    json_t *tran;
    json_t *result;
    bool success;
    bool count;
    LOG(DEBUG, "Table %s insert with parent: %s %s row: %s", table,
            parent_table, parent_column, json_dumps_static(row, 0));
    tran = ovsdb_tran_insert_with_parent(table, row,
            parent_table, parent_where, parent_column);
    result = ovsdb_method_send_s(MT_TRANS, tran);
    // result contains both statuses, for insert and mutate:
    // [{}, {"uuid": ["uuid", "88755b78-d56d-414f-92d8-01bd47937da4"]}, {}, {"count": 1}]
    count = ovsdb_get_update_result_count_off(result, table, "insert_w_parent", 2);
    success = ovsdb_get_insert_result_uuid(result, table, "insert_w_parent", uuid);
    json_decref(result);
    if (!success || count != 1)
    {
        LOG(ERR, "%s: invalid result: insert: %s count: %d", __FUNCTION__,
                success?"success":"failure", count);
        return false;
    }
    return true;
}


bool ovsdb_sync_upsert_with_parent(char *table,
        json_t *where, json_t *row, ovs_uuid_t *uuid,
        char *parent_table, json_t *parent_where, char *parent_column)
{
    int count;
    ovs_uuid_t my_uuid;
    ovs_uuid_t *up_uuid = uuid ? uuid : &my_uuid; // required for mutate

    json_incref(row);
    count = ovsdb_sync_update_one_get_uuid(table, where, row, up_uuid);
    if (count == 1)
    {
        json_decref(row);
        // should an upsert also mutate parent even when updating?
        // it should not be required, as the insert ought to take
        // care of that, but always updating parent is probably safer
        count = ovsdb_sync_mutate_uuid_set(parent_table,
            parent_where, parent_column, OTR_INSERT, up_uuid->uuid);
        if (count != 1)
        {
            LOG(ERR, "%s: unexpected mutate count: %d != 1", __FUNCTION__, count);
            return false;
        }
        return true;
    }
    else if (count != 0)
    {
        LOG(ERR, "%s: unexpected count: %d != 0", __FUNCTION__, count);
        json_decref(row);
        return false;
    }
    // if count == 0 do insert
    return ovsdb_sync_insert_with_parent(table, row, uuid,
            parent_table, parent_where, parent_column);
}


// return count or -1 on error
int ovsdb_sync_delete_with_parent(char *table, json_t *where,
        char *parent_table, json_t *parent_where, char *parent_column)
{
    json_t *result = NULL;

    LOG(DEBUG, "Table %s delete where %s", table, json_dumps_static(where, 0));
    result = ovsdb_delete_with_parent_res_s(table, where,
           parent_table, parent_where, parent_column);
    return ovsdb_get_update_result_count(result, table, "delete_w_parent");
}


