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

#include <openssl/sha.h>
#include <openssl/evp.h>

#include "evx.h"
#include "json_util.h"
#include "log.h"
#include "memutil.h"
#include "osp_ps.h"
#include "ovsdb.h"
#include "ovsdb_sync.h"
#include "util.h"

#include "psm.h"

#define PSM_ROW_KEY_LEN ((SHA256_DIGEST_LENGTH * 2) + 1)

struct psm_ovsdb_row_key
{
    char    rk_key[PSM_ROW_KEY_LEN];
};

/*
 * The in memory representation of a single row. Each row has an associated key
 * (which is calculated from the data).
 *
 * The pr_uuid field is valid only when there's an associated table in OVSDB. A
 * row with an empty UUID is considered "orphaned" -- scheduled for deletion.
 *
 * The pr_data is valid only when there's pending data to be written to
 * persistent storage.
 *
 * During a sync operation the various fields are interpreted as follows:
 *
 *  - if pr_data is not NULL, the data is written to persistent storage and
 *    and the pr_data field is freed and set to NULL
 *  - if pr_uuid is empty it is assumed that the row was deleted from OVSDB
 *    therefore it will be removed from memory and persistent storage
 */
struct psm_ovsdb_row
{
    struct psm_ovsdb_row_key    pr_key;             /* Row key */
    ovs_uuid_t                  pr_uuid;            /* Associated UUID */
    json_t                     *pr_data;            /* Data to be written or NULL if none */
    ds_tree_node_t              pr_uuid_tnode;
    ds_tree_node_t              pr_key_tnode;
};

static bool psm_ovsdb_row_store_load(void);
static ev_debounce_fn_t psm_ovsdb_row_sync;
static ds_key_cmp_t psm_ovsdb_row_key_cmp;
static const char *psm_ovsdb_row_key_str(struct psm_ovsdb_row_key *rk);
static void psm_ovsdb_row_key_from_json(struct psm_ovsdb_row_key *key, const char *table, json_t *row);
static bool psm_ovsdb_row_key_from_str(struct psm_ovsdb_row_key *key, const char *str);
static bool psm_ovsdb_row_uuid_get(ovs_uuid_t *uuid, json_t *row);
static json_t *psm_ovsdb_row_filter(const char *table, json_t *row);

static ev_debounce g_psm_ovsdb_row_sync_debounce;

static ds_tree_t g_psm_ovsdb_row_uuid_list = DS_TREE_INIT(
        ds_str_cmp,
        struct psm_ovsdb_row,
        pr_key_tnode);

static ds_tree_t g_psm_ovsdb_row_key_list = DS_TREE_INIT(
        ds_str_cmp,
        struct psm_ovsdb_row,
        pr_uuid_tnode);

/*
 * ===========================================================================
 *  Public functions
 * ===========================================================================
 */
bool psm_ovsdb_row_init(void)
{
    if (!psm_ovsdb_row_store_load())
    {
        LOG(ERR, "Error loading store: %s", PSM_STORE);
        return false;
    }

    ev_debounce_init2(
            &g_psm_ovsdb_row_sync_debounce,
            &psm_ovsdb_row_sync,
            PSM_DEBOUNCE_TIME * PSM_DEBOUNCE_INIT_FACTOR,
            PSM_DEBOUNCE_TIME_MAX * PSM_DEBOUNCE_INIT_FACTOR);

    ev_debounce_start(EV_DEFAULT, &g_psm_ovsdb_row_sync_debounce);

    return true;
}

/*
 * Process an updated or inserted row and save it to persistent storage.
 *
 * The algorithm is roughly as follows:
 *
 *  - calculate row key from row data (SHA256 hash over filtered data)
 *  - lookup by uuid and if a row with the same uuid is found:
 *      - compare key values, if they are the same return
 *      - orphan the current entry (unset uuid) so a sync operation will remove
 *        it from persistent storage and memory
 *  - if os_persist is false, return now
 *  - lookup by key and if a row with the same key is found:
 *      - if the row uuid is empty (orphaned entry), assign the uuid to this
 *        row and return
 *      - compare uuid values, if they are different, report a duplicate row
 *        warning
 *  - add row with give uuid and key to the cache and schedule a resync
 */
bool psm_ovsdb_row_update(const char *table, json_t *row)
{
    struct psm_ovsdb_row_key key;
    struct psm_ovsdb_row *pr;
    ovs_uuid_t uuid;
    json_t *jos_persist;

    bool os_persist = false;
    bool retval = false;
    json_t *row_filtered = NULL;
    bool do_sync = false;

    /* Extract the uuid from the row JSON data */
    if (!psm_ovsdb_row_uuid_get(&uuid, row))
    {
        LOG(WARN, "Row data from table %s doesn't contain the column `_uuid`: %s",
                table,
                json_dumps_static(row, 0));
        goto exit;
    }

    row_filtered = psm_ovsdb_row_filter(table, row);
    if (row_filtered == NULL)
    {
        LOG(ERR, "Unable to filter row: %s", json_dumps_static(row, 0));
        goto exit;
    }

    /* Extract the `os_persist` value */
    jos_persist = json_object_get(row, PSM_TABLE_KEY);
    os_persist = json_is_boolean(jos_persist) && json_is_true(jos_persist);

    /* Calculate row key */
    psm_ovsdb_row_key_from_json(&key, table, row_filtered);

    pr = ds_tree_find(&g_psm_ovsdb_row_uuid_list, &uuid);
    if (pr != NULL)
    {
        if (psm_ovsdb_row_key_cmp(&pr->pr_key, &key) == 0)
        {
            retval = true;
            goto exit;
        }
        /*
         * The row has been updated, just orphan the current entry. A resync
         * operation will take care of removing it.
         */
        LOG(INFO, "Updated row with uuid: %s. Deleting current data.", pr->pr_uuid.uuid);
        ds_tree_remove(&g_psm_ovsdb_row_uuid_list, pr);
        memset(&pr->pr_uuid, 0, sizeof(pr->pr_uuid));
        do_sync = true;
    }

    if (!os_persist)
    {
        retval = true;
        goto exit;
    }

    pr = ds_tree_find(&g_psm_ovsdb_row_key_list, &key);
    if (pr != NULL)
    {
        if (pr->pr_uuid.uuid[0] == '\0')
        {
            LOG(INFO, "Assigning cached data to uuid: %s", uuid.uuid);
            /* Cached data already exists and is currently unassigned */
            memcpy(&pr->pr_uuid, &uuid, sizeof(pr->pr_uuid));
            ds_tree_insert(&g_psm_ovsdb_row_uuid_list, pr, &pr->pr_uuid);
        }
        else if (ds_str_cmp(&pr->pr_uuid, &uuid) != 0)
        {
            LOG(WARN, "Duplicate rows detected (%s and %s). Only one will be stored.",
                    pr->pr_uuid.uuid, uuid.uuid);
        }
        else
        {
            /*
             * Same uuid and same key -- in theory this should never happen.
             * In the future we can skip a resync operation and exit immediately.
             */
            LOG(NOTICE, "Identical row update with uuid: %s", uuid.uuid);
        }

        retval = true;
        goto exit;
    }

    LOG(INFO, "Adding new data for uuid: %s", uuid.uuid);
    /* Add new entry */
    pr = CALLOC(1, sizeof(*pr));
    memcpy(&pr->pr_key, &key, sizeof(pr->pr_key));
    memcpy(&pr->pr_uuid, &uuid, sizeof(pr->pr_uuid));

    /*
     * Construct the row data, this consists of a JSON in the following format:
     * {
     *    "table": "table_name",
     *    "row": { row_data }
     * }
     *
     * The row field is just the `row_filtered` variable, but with an acquired
     * reference (hence the json_object_set() instead of json_object_set_new()).
     */
    pr->pr_data = json_object();
    json_object_set_new(pr->pr_data, "table", json_string(table));
    json_object_set(pr->pr_data, "row", row_filtered);

    ds_tree_insert(&g_psm_ovsdb_row_key_list, pr, &pr->pr_key);
    ds_tree_insert(&g_psm_ovsdb_row_uuid_list, pr, &pr->pr_uuid);

    do_sync = true;
    retval = true;

exit:
    if (do_sync)
    {
        ev_debounce_start(EV_DEFAULT, &g_psm_ovsdb_row_sync_debounce);
    }

    json_decref(row_filtered);
    return retval;
}

/*
 * Delete a row from persistent storage and memory cache
 */
bool psm_ovsdb_row_delete(const char *table, json_t *row)
{
    struct psm_ovsdb_row *pr;
    ovs_uuid_t uuid;

    /* Extract the uuid from the row JSON data */
    if (!psm_ovsdb_row_uuid_get(&uuid, row))
    {
        LOG(WARN, "Row data from table %s doesn't contain the column `_uuid`: %s",
                table,
                json_dumps_static(row, 0));
        return false;
    }

    pr = ds_tree_find(&g_psm_ovsdb_row_uuid_list, &uuid);
    if (pr == NULL)
    {
        return true;
    }

    LOG(INFO, "Deleting row with uuid: %s.", pr->pr_uuid.uuid);
    ds_tree_remove(&g_psm_ovsdb_row_uuid_list, pr);
    memset(&pr->pr_uuid, 0, sizeof(pr->pr_uuid));

    ev_debounce_start(EV_DEFAULT, &g_psm_ovsdb_row_sync_debounce);

    return true;
}

/*
 * Restore data from persistent storage to OVSDB
 */
bool psm_ovsdb_row_restore(void)
{
    struct psm_ovsdb_row *pr;
    json_error_t jerr;
    const char *table;
    size_t sdata_sz;
    const char *key;
    json_t *jrow;
    osp_ps_t *ps;
    void *inext;
    void *iter;

    char *sdata = NULL;
    json_t *jdata = NULL;

    ps = osp_ps_open(PSM_STORE, OSP_PS_PRESERVE | OSP_PS_READ);
    if (ps == NULL)
    {
        /* Store does not exists yet -- nothing to load */
        return true;
    }

    ds_tree_foreach(&g_psm_ovsdb_row_key_list, pr)
    {
        /* Insert the row into OVSDB */
        sdata_sz = osp_ps_get(ps, psm_ovsdb_row_key_str(&pr->pr_key), NULL, 0);
        if (sdata_sz <= 0)
        {
            LOG(WARN, "Error getting data size for key %s.", psm_ovsdb_row_key_str(&pr->pr_key));
            continue;
        }

        sdata = REALLOC(sdata, sdata_sz + 1);
        if (osp_ps_get(ps, psm_ovsdb_row_key_str(&pr->pr_key), sdata, sdata_sz) <= 0)
        {
            LOG(WARN, "Error loading data for key %s.", psm_ovsdb_row_key_str(&pr->pr_key));
            continue;
        }

        json_decref(jdata);
        jdata = json_loads(sdata, 0, &jerr);
        if (jdata == NULL)
        {
            LOG(WARN, "Error parsing store data (error %s): %s", jerr.text, sdata);
            continue;
        }

        table = json_string_value(json_object_get(jdata, "table"));
        jrow = json_object_get(jdata, "row");
        if (table == NULL || jrow == NULL)
        {
            LOG(WARN, "Invalid data format in store: %s", sdata);
            continue;
        }

        /*
         * Filter unsupported columns -- use iterators as
         * json_object_foreach_safe() is not supported on older Jansson versions
         */
        iter = json_object_iter(jrow);
        for (inext = json_object_iter_next(jrow, iter);
                iter != NULL;
                iter = inext, inext = json_object_iter_next(jrow, iter))
        {
            key = json_object_iter_key(iter);

            if (psm_ovsdb_schema_column_exists(table, key)) continue;

            LOG(WARN, "Unable to restore value for column %s:%s. It doesn't exist in the schema.",
                    table,
                    key);

            json_object_del(jrow, key);
        }

        /*
         * ovsdb_sync_insert() steals the reference to jrow. This is not really
         * desired in this scenario since jrow is part of jdata, therefore
         * increase the reference count by 1 to prevent stealing
         */
        json_incref(jrow);
        if (ovsdb_sync_insert(table, jrow, &pr->pr_uuid) != 1)
        {
            memset(&pr->pr_uuid, 0, sizeof(pr->pr_uuid));
            LOG(ERR, "Error inserting ROW into OVSDB: %s", sdata);
            continue;
        }

        LOG(INFO, "Restore table %s, row %s.", table, pr->pr_uuid.uuid);
    }

    FREE(sdata);
    json_decref(jdata);
    osp_ps_close(ps);

    return true;
}

/*
 * ===========================================================================
 *  Private row-level functions
 * ===========================================================================
 */

/*
 * Populate the row cache using data from the persistent storage. The elements
 * in the "orphaned" state until data is read from OVSDB.
 */
bool psm_ovsdb_row_store_load(void)
{
    struct psm_ovsdb_row *pr;
    const char *key;
    osp_ps_t *ps;

    ps = osp_ps_open(PSM_STORE, OSP_PS_PRESERVE | OSP_PS_READ);
    if (ps == NULL)
    {
        /* Store does not exists yet -- nothing to load */
        return true;
    }

    osp_ps_foreach(ps, key)
    {
        struct psm_ovsdb_row_key rk;

        if (!psm_ovsdb_row_key_from_str(&rk, key))
        {
            LOG(ERR, "Found invalid key in store: %s", key);
            continue;
        }

        /* Populate the memory cache */
        pr = ds_tree_find(&g_psm_ovsdb_row_key_list, &rk);
        if (pr != NULL) continue;

        pr = CALLOC(1, sizeof(*pr));
        memcpy(&pr->pr_key, &rk, sizeof(pr->pr_key));

        ds_tree_insert(&g_psm_ovsdb_row_key_list, pr, &pr->pr_key);
    }

    osp_ps_close(ps);

    return true;
}

/*
 * Scan the row cache and sync entries with persistent storage
 *
 * If a row has an empty uuid, it is considered orphaned and is deleted from
 * persistent storage.
 *
 * If a row has the pr_data field set it is considered dirty. Write the data
 * to persistent storage and free it from memory.
 *
 * After a sync operation, the memory cache will consist only of entries which
 * have valid uuids and have no associated data (pr_data).
 */
void psm_ovsdb_row_sync(struct ev_loop *loop, ev_debounce *w, int revent)
{
    (void)loop;
    (void)revent;

    ds_tree_iter_t iter;
    struct psm_ovsdb_row *pr;
    char *strdata;
    osp_ps_t *ps;

    /* Reset the debounce timer values */
    ev_debounce_set2(w, PSM_DEBOUNCE_TIME, PSM_DEBOUNCE_TIME_MAX);

    ps = osp_ps_open(PSM_STORE, OSP_PS_RDWR | OSP_PS_PRESERVE);
    if (ps == NULL)
    {
        LOG(ERR, "(sync) Unable to open store: %s", PSM_STORE);
        return;
    }

    ds_tree_foreach_iter(&g_psm_ovsdb_row_key_list, pr, &iter)
    {
        /* Remove record */
        if (pr->pr_uuid.uuid[0] == '\0')
        {
            LOG(DEBUG, "DEL persistent row: %s", psm_ovsdb_row_key_str(&pr->pr_key));
            osp_ps_set(ps, psm_ovsdb_row_key_str(&pr->pr_key), NULL, 0);

            ds_tree_iremove(&iter);
            if (pr->pr_data != NULL)
            {
                json_decref(pr->pr_data);
            }
            FREE(pr);
        }
        else if (pr->pr_data != NULL)
        {
            strdata = json_dumps(
                    pr->pr_data,
                    JSON_COMPACT | JSON_SORT_KEYS | JSON_ENSURE_ASCII);
            if (strdata == NULL)
            {
                LOG(ERR, "(sync) Error converting JSON to string for row: %s",
                        pr->pr_uuid.uuid);
                continue;
            }

            LOG(DEBUG, "ADD persistent row: %s -> %s", psm_ovsdb_row_key_str(&pr->pr_key), strdata);
            osp_ps_set(ps, psm_ovsdb_row_key_str(&pr->pr_key), strdata, strlen(strdata) + 1);

            json_free(strdata);
            json_decref(pr->pr_data);
            pr->pr_data = NULL;
        }
    }

    osp_ps_close(ps);
}

/*
 * ===========================================================================
 *  Key functions
 * ===========================================================================
 */

/*
 * Calculate row key from @p table and @row.
 *
 * The has key is calculated using the table name and the row data:
 *
 * TABLE_NAME\0ROW_DATA\0
 *
 * ROW_DATA is generated using json_dumps() with the JSON_COMPACT,
 * JSON_SORT_KEYS and JSON_ENSURE_ASCII flags. This seems to match the output
 * of `jq -acS` quite well and the generated string seems to be stable (both
 * jansson an jq always produce the same output given the same input).
 */
void psm_ovsdb_row_key_from_json(struct psm_ovsdb_row_key *key, const char *table, json_t *row)
{
    const char *row_data;
#if OPENSSL_VERSION_NUMBER >= 0x030000000  // 3.0.0
    EVP_MD_CTX *md_ctx = EVP_MD_CTX_create();
    const EVP_MD *md = EVP_sha256();
#else
    SHA256_CTX sha_ctx;
#endif
    uint8_t sha_buf[SHA256_DIGEST_LENGTH];

    memset(key, 0, sizeof(*key));

    /* Generate a compact representation of the row data and add it to the hash */
    row_data = json_dumps(row, JSON_COMPACT | JSON_SORT_KEYS | JSON_ENSURE_ASCII);

#if OPENSSL_VERSION_NUMBER >= 0x030000000  // 3.0.0
    EVP_DigestInit_ex(md_ctx, md, NULL);

    /* Include the trailing \0 in the hash */
    EVP_DigestUpdate(md_ctx, table, strlen(table) + 1);
    EVP_DigestUpdate(md_ctx, row_data, strlen(row_data) + 1);

    EVP_DigestFinal_ex(md_ctx, sha_buf, 0);
    EVP_MD_CTX_destroy(md_ctx);
#else
    SHA256_Init(&sha_ctx);

    /* Include the trailing \0 in the hash */
    SHA256_Update(&sha_ctx, table, strlen(table) + 1);
    SHA256_Update(&sha_ctx, row_data, strlen(row_data) + 1);

    SHA256_Final(sha_buf, &sha_ctx);
#endif
    json_memdbg_free((void *)row_data);

    bin2hex(sha_buf, sizeof(sha_buf), key->rk_key, sizeof(key->rk_key));
}

bool psm_ovsdb_row_key_from_str(struct psm_ovsdb_row_key *key, const char *str)
{
    if (STRSCPY(key->rk_key, str) < ((ssize_t)sizeof(key->rk_key) - 1))
    {
        return false;
    }

    return true;
}

int psm_ovsdb_row_key_cmp(const void *_a, const void *_b)
{
    const struct psm_ovsdb_row_key *a = _a;
    const struct psm_ovsdb_row_key *b = _b;

    return strcmp(a->rk_key, b->rk_key);
}

const char *psm_ovsdb_row_key_str(struct psm_ovsdb_row_key *rk)
{
    return rk->rk_key;
}

/*
 * ===========================================================================
 *  Utility functions
 * ===========================================================================
 */

/*
 * Take a JSON representation of a row as returned by OVSDB and filter out
 * unwanted columns such as _uuid, _version, ephemeral columns, references and
 * default values for various types (empty sets, empty strings, empty maps...)
 */
json_t *psm_ovsdb_row_filter(const char *table, json_t *row)
{
    json_t *row_filtered;
    const char *column;
    const char *type;
    json_t *data;
    void *iter;
    void *inext;

    row_filtered = json_copy(row);

    json_object_del(row_filtered, "_uuid");
    json_object_del(row_filtered, "_version");

    /*
     * Remove references and UUID values
     * json_object_foreach_safe() is not available in jansson 2.7, use iterators
     */
    iter = json_object_iter(row_filtered);
    for (inext = json_object_iter_next(row_filtered, iter);
            iter != NULL;
            iter = inext, inext = json_object_iter_next(row_filtered, iter))
    {
        /*
         * Filter out certain column types:
         * - empty strings: ""
         * - empty sets: ["set", []]
         * - empty maps: ["map", []]
         * - uuids: ["uuid", ...]
         * - uuid sets: ["set", [ ["uuid", ...] ] ]
         */
        column = json_object_iter_key(iter);
        data = json_object_iter_value(iter);

        /* Check for ephemeral columns */
        if (psm_ovsdb_schema_column_is_ephemeral(table, column))
        {
            goto delete;
        }

        /* Check for empty strings */
        const char *str = json_string_value(data);
        if (str != NULL && str[0] == '\0')
        {
            goto delete;
        }

        if (!json_is_array(data)) continue;

        type = json_string_value(json_array_get(data, 0));
        if (type == NULL) continue;

        /* Check for empty sets or sets containing uuids */
        if (strcmp(type, "set") == 0)
        {
            const char *inner_type;
            json_t *jset;

            jset = json_array_get(data, 1);
            if (jset == NULL) continue;

            if (json_array_size(jset) == 0)
            {
                goto delete;
            }

            /* Check for embedded uuid types */
            inner_type = json_string_value(json_array_get(json_array_get(jset, 0), 0));
            if (inner_type != NULL && strcmp(inner_type, "uuid") == 0)
            {
                goto delete;
            }
        }
        /* Check for empty maps */
        else if (strcmp(type, "map") == 0)
        {
            json_t *jmap = json_array_get(data, 1);
            if (jmap != NULL && json_array_size(jmap) == 0)
            {
                goto delete;
            }
        }
        /* Check for uuid types */
        else if (strcmp(type, "uuid") == 0)
        {
            goto delete;
        }

        continue;
delete:
        /* Delete column */
        json_object_del(row_filtered, column);
    }

    return row_filtered;
}

/*
 * Extract the uuid from the row data
 */
bool psm_ovsdb_row_uuid_get(ovs_uuid_t *uuid, json_t *row)
{
    json_t *juuid;
    const char *val;

    juuid = json_object_get(row, "_uuid");
    if (juuid == NULL || !json_is_array(juuid)) return false;

    val = json_string_value(json_array_get(juuid, 0));
    if (val == NULL || strcmp(val, "uuid") != 0) return false;

    val = json_string_value(json_array_get(juuid, 1));
    if (val == NULL) return false;

    memset(uuid, 0, sizeof(*uuid));
    STRSCPY(uuid->uuid, val);

    return true;
}
