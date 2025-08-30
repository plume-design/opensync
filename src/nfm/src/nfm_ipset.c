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

#include <jansson.h>

#include "const.h"
#include "log.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "osn_ipset.h"
#include "memutil.h"

/*
 * Mapping between the Netfilter_Ipset:type and osn_ipset_type enums
 */
const char *nfm_osn_ipset_type_str[] =
{
    [OSN_IPSET_BITMAP_IP] = "bitmap:ip",
    [OSN_IPSET_BITMAPIP_MAC] = "bitmap:ip,mac",
    [OSN_IPSET_BITMAP_PORT] = "bitmap:port",
    [OSN_IPSET_HASH_IP] = "hash:ip",
    [OSN_IPSET_HASH_MAC] = "hash:mac",
    [OSN_IPSET_HASH_IP_MAC] = "hash:ip,mac",
    [OSN_IPSET_HASH_NET] = "hash:net",
    [OSN_IPSET_HASH_NET_NET] = "hash:net,net",
    [OSN_IPSET_HASH_IP_PORT] = "hash:ip,port",
    [OSN_IPSET_HASH_NET_PORT] = "hash:net,port",
    [OSN_IPSET_HASH_IP_PORT_IP] = "hash:ip,port,ip",
    [OSN_IPSET_HASH_IP_PORT_NET] = "hash:ip,port,net",
    [OSN_IPSET_HASH_IP_MARK] = "hash:ip,mark",
    [OSN_IPSET_HASH_NET_PORT_NET] = "hash:net,port,net",
    [OSN_IPSET_HASH_NET_IFACE] = "hash:net,iface",
    [OSN_IPSET_LIST_SET] = "list:set"
};

struct nfm_ipset
{
    /** True if ipset should be read from a local file */
    bool                    ni_local;
    char                    ni_name[SCHEMA_COLUMN_SZ(Netfilter_Ipset, name)];
    /** Cached local values -- this array is usually small */
    const char            **ni_local_values;
    int                     ni_local_values_len;
    enum osn_ipset_type     ni_type;
    /** ipset create options */
    const char             *ni_options;
    /** the ipset row structure is cached by uuid */
    ovs_uuid_t              ni_uuid;
    ds_tree_node_t          ni_uuid_tnode;
    /** osn_ipset_t object associated with this row */
    osn_ipset_t            *ni_ipset;
};

static void callback_Netfilter_Ipset(
        ovsdb_update_monitor_t *mon,
        struct schema_Netfilter_Ipset *old,
        struct schema_Netfilter_Ipset *new);

static struct nfm_ipset *nfm_ipset_get(
        const ovs_uuid_t *uuid,
        const char *name,
        const char *type,
        const char *options);

static void nfm_ipset_release(const ovs_uuid_t *uuid);
static bool nfm_ipset_values_set(struct nfm_ipset *ips, const char *values[], int values_len);
static bool nfm_ipset_type_from_str(enum osn_ipset_type *out, const char *type);
static void nfm_ipset_local_values_free(struct nfm_ipset *ips);
static bool nfm_ipset_local_values_set(struct nfm_ipset *ips, const char *values[], int values_len);
static void nfm_ipset_status_set(struct nfm_ipset *ips, const char *status);
static bool nfm_ipset_objm_apply(struct nfm_ipset *ips);
static bool nfm_ipset_objm_json_parse(struct nfm_ipset *ips, json_t *jvalues);

static ovsdb_table_t table_Netfilter_Ipset;
static ds_tree_t nfm_ipset_list = DS_TREE_INIT(ds_str_cmp, struct nfm_ipset, ni_uuid_tnode);
static char nfm_ipset_objm_path[C_MAXPATH_LEN];

/*
 * Initialize the ipset subsystem
 */
bool nfm_ipset_init(void)
{
    OVSDB_TABLE_INIT(Netfilter_Ipset, name);
    OVSDB_TABLE_MONITOR_F(Netfilter_Ipset, C_VPACK("-", "_version", "status"));
    return true;
}

void callback_Netfilter_Ipset(
        ovsdb_update_monitor_t *mon,
        struct schema_Netfilter_Ipset *old,
        struct schema_Netfilter_Ipset *new)
{
    (void)old;

    struct nfm_ipset *ips;
    int ii;

    bool retval = false;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            break;

        case OVSDB_UPDATE_DEL:
            nfm_ipset_release(&new->_uuid);
            return;

        default:
            LOG(ERR, "nfm: Netfilter_Ipset monitor error.");
            return;
    }

    /* It's OK to allocate this on stack as the schema entries are limited */
    const char *values[new->values_len];
    for (ii = 0; ii < new->values_len; ii++)
    {
        values[ii] = new->values[ii];
    }

    /*
     * Acquire an IPSET object and update it's values
     */
    ips = nfm_ipset_get(
            &new->_uuid,
            new->name,
            new->type,
            new->options);
    if (ips == NULL)
    {
        LOG(ERR, "nfm_ipset: %s: Unable to obtain ipset object.", new->name);
        goto error;
    }

    if (ips->ni_local)
    {
        if (!nfm_ipset_local_values_set(ips, values, new->values_len))
        {
            LOG(ERR, "nfm_ipset: %s: Error applying local values.", new->name);
            goto error;
        }
    }
    else if (!nfm_ipset_values_set(ips, values, new->values_len))
    {
        LOG(ERR, "nfm_ipset: %s: Error applying values.", new->name);
        goto error;
    }

    LOG(NOTICE, "nfm_ipset: %s: ipset applied successfully.", ips->ni_name);

    retval = true;

error:
    nfm_ipset_status_set(ips, retval ? "success" : "error");
}


struct nfm_ipset *nfm_ipset_get(
        const ovs_uuid_t *uuid,
        const char *name,
        const char *type,
        const char *options)
{
    struct nfm_ipset *ips;

    ips = ds_tree_find(&nfm_ipset_list, (void *)uuid->uuid);
    if (ips != NULL) return ips;

    ips = CALLOC(1, sizeof(*ips));
    ips->ni_uuid = *uuid;
    STRSCPY(ips->ni_name, name);

    /*
     * If the ipset type is local, the real type will be read from a local file.
     * Otherwise try to map the schema type to the osn_ipset_type enum.
     */
    if (strcmp(type, "local") == 0)
    {
        ips->ni_local = true;
    }
    else if (!nfm_ipset_type_from_str(&ips->ni_type, type))
    {
        LOG(ERR, "nfm_ipset: %s: Unknown ipset type: %s", ips->ni_name, type);
        goto error;
    }

    ips->ni_options = strdup(options);

    ds_tree_insert(&nfm_ipset_list, ips, ips->ni_uuid.uuid);

    return ips;

error:
    if (ips->ni_options != NULL) FREE(ips->ni_options);
    if (ips != NULL) FREE(ips);
    return NULL;
}

void nfm_ipset_release(const ovs_uuid_t *uuid)
{
    struct nfm_ipset *ips;

    ips = ds_tree_find(&nfm_ipset_list, (void *)uuid->uuid);
    if (ips == NULL) return;

    if (ips->ni_ipset != NULL)
    {
        osn_ipset_del(ips->ni_ipset);
        ips->ni_ipset = NULL;
    }

    ds_tree_remove(&nfm_ipset_list, ips);
    nfm_ipset_local_values_free(ips);
    if (ips->ni_options != NULL) FREE(ips->ni_options);
    FREE(ips);
}

bool nfm_ipset_values_set(struct nfm_ipset *ips, const char *values[], int values_len)
{
    if (ips->ni_ipset == NULL)
    {
        ips->ni_ipset = osn_ipset_new(ips->ni_name, ips->ni_type, ips->ni_options);
        if (ips->ni_ipset == NULL)
        {
            LOG(ERR, "nfm_ipset: %s: Error creating OSN IPSET object.", ips->ni_name);
            return false;
        }
    }

    if (!osn_ipset_values_set(ips->ni_ipset, values, values_len))
    {
        LOG(ERR, "nfm_ipset: %s: Error setting values.", ips->ni_name);
        return false;
    }

    if (!osn_ipset_apply(ips->ni_ipset))
    {
        LOG(ERR, "nfm_ipset: %s: Error during apply.", ips->ni_name);
        return false;
    }

    return true;
}

bool nfm_ipset_type_from_str(enum osn_ipset_type *out, const char *type)
{
    int ii;

    for (ii = 0; ii < ARRAY_LEN(nfm_osn_ipset_type_str); ii++)
    {
        if (strcmp(nfm_osn_ipset_type_str[ii], type) == 0)
        {
            break;
        }
    }

    if (ii >= ARRAY_LEN(nfm_osn_ipset_type_str))
    {
        LOG(DEBUG, "nfm_ipset: Unknown IPSET type: %s", type);
        return false;
    }

    *out = ii;
    return true;
}

void nfm_ipset_local_values_free(struct nfm_ipset *ips)
{
    int ii;

    for (ii = 0; ii < ips->ni_local_values_len; ii++)
    {
        FREE(ips->ni_local_values[ii]);
    }

    FREE(ips->ni_local_values);
}

bool nfm_ipset_local_values_set(struct nfm_ipset *ips, const char *values[], int values_len)
{
    int ii;

    if (!ips->ni_local)
    {
        LOG(DEBUG, "nfm_ipset: %s: Unable to set local values to non-local ipset.", ips->ni_name);
        return false;
    }

    nfm_ipset_local_values_free(ips);

    ips->ni_local_values_len = values_len;
    ips->ni_local_values = CALLOC(values_len, sizeof(ips->ni_local_values[0]));
    for (ii = 0; ii < values_len; ii++)
    {
        ips->ni_local_values[ii] = strdup(values[ii]);
    }

    return nfm_ipset_objm_apply(ips);
}

void nfm_ipset_status_set(struct nfm_ipset *ips, const char *status)
{
    struct schema_Netfilter_Ipset schema_ipset;

    MEMZERO(schema_ipset);
    schema_ipset._partial_update = true;

    SCHEMA_SET_STR(schema_ipset.status, status);

    (void)ovsdb_table_update_where(
            &table_Netfilter_Ipset,
            ovsdb_tran_cond(OCLM_UUID, "_uuid", OFUNC_EQ, ips->ni_uuid.uuid),
            &schema_ipset);
}

bool nfm_ipset_objm_apply(struct nfm_ipset *ips)
{
    int ii;
    int rc;

    bool retval = true;

    if (!ips->ni_local)
    {
        LOG(ERR, "nfm_ipset: %s: Ipset is not type local.", ips->ni_name);
        return true;
    }

    if (nfm_ipset_objm_path[0] == '\0')
    {
        LOG(DEBUG, "nfm_ipset: %s: Object store not yet available.", ips->ni_name);
        return true;
    }

    /* Delete the current ipset - the type in the OBJM storage may have changed */
    if (ips->ni_ipset != NULL)
    {
        osn_ipset_del(ips->ni_ipset);
        ips->ni_ipset = NULL;
        /* The options will be read from the JSON file, free them */
        if (ips->ni_options != NULL) FREE(ips->ni_options);
        ips->ni_options = NULL;
    }

    for (ii = 0; ii < ips->ni_local_values_len; ii++)
    {
        char pvalues[C_MAXPATH_LEN];
        json_error_t jerr;
        json_t *jvalues;
        FILE *fvalues;

        rc = snprintf(pvalues, sizeof(pvalues), "%s/%s.json", nfm_ipset_objm_path, ips->ni_local_values[ii]);
        if (rc >= (int)sizeof(pvalues))
        {
            LOG(ERR, "nfm_ipset: %s: objm path too long: %s/%s",
                    ips->ni_name, nfm_ipset_objm_path, ips->ni_local_values[ii]);
            retval = false;
            continue;
        }

        fvalues = fopen(pvalues, "r");
        if (fvalues == NULL)
        {
            LOG(ERR, "nfm_ipset: %s: Unable to open file: %s",
                    ips->ni_name, pvalues);
            retval = false;
            continue;
        }

        jvalues = json_loadf(fvalues, 0, &jerr);
        fclose(fvalues);
        if (jvalues == NULL)
        {
            LOG(ERR, "nfm_ipset: Error loading local file %s: %s", pvalues, jerr.text);
            retval = false;
            continue;
        }

        if (!nfm_ipset_objm_json_parse(ips, jvalues))
        {
            LOG(ERR, "nfm_ipset: %s: Error processing file: %s",
                    ips->ni_name, pvalues);
        }

        json_decref(jvalues);
    }

    if (ips->ni_ipset != NULL && !osn_ipset_apply(ips->ni_ipset))
    {
        LOG(ERR, "nfm_ipset: %s: Error applying JSON values.", ips->ni_name);
        return false;
    }

    return retval;
}

bool nfm_ipset_objm_json_parse(struct nfm_ipset *ips, json_t *jvalues)
{
    enum osn_ipset_type type;
    const char *stype;
    const char *opts;
    json_t *jarr;
    json_t *jval;
    size_t ji;

    stype = json_string_value(json_object_get(jvalues, "type"));
    opts = json_string_value(json_object_get(jvalues, "options"));
    jarr = json_object_get(jvalues, "values");

    if (stype == NULL || opts == NULL || jarr == NULL || !json_is_array(jarr))
    {
        LOG(DEBUG, "nfm_ipset: %s: Error parsing JSON ipset object, missing type, options or values.", ips->ni_name);
        return false;
    }

    if (!nfm_ipset_type_from_str(&type, stype))
    {
        LOG(DEBUG, "nfm_ipset: %s: Unknown IPSET type: %s (from json).",
                ips->ni_name, stype);
        return false;
    }

    /* Create the ipset if it doesn't exist */
    if (ips->ni_ipset == NULL)
    {
        ips->ni_ipset = osn_ipset_new(ips->ni_name, type, opts);
        if (ips->ni_ipset == NULL)
        {
            LOG(ERR, "nfm_ipset: %s: Error creating OSN IPSET object (local).", ips->ni_name);
            return false;
        }

        ips->ni_type = type;
        ips->ni_options = strdup(opts);
    }
    else if (strcmp(ips->ni_options, opts) != 0)
    {
        LOG(DEBUG, "nfm_ipset: %s: Options mismatch when parsing JSON values.",
                ips->ni_name);
        return false;
    }
    else if (ips->ni_type != type)
    {
        LOG(DEBUG, "nfm_ipset: %s: Type mismatch when parsing JSON values.",
                ips->ni_name);
        return false;
    }

    const char *values[json_array_size(jarr)];
    int nval = 0;

    json_array_foreach(jarr, ji, jval)
    {
        const char *val = json_string_value(jval);
        if (val == NULL) continue;

        values[nval++] = val;
    }

    if (!osn_ipset_values_add(ips->ni_ipset, values, nval))
    {
        LOG(DEBUG, "nfm_ipset: %s: Error adding JSON values to ipset.",
                ips->ni_name);
        return false;
    }

    return true;
}

void nfm_ipset_objm_notify(const char *version, bool ready, const char *path)
{
    struct nfm_ipset *ips;

    LOG(NOTICE, "nfm_ipset: New netfilter_ipset object store: %s (ready=%d).", version, ready);

    if (version == NULL || !ready || path == NULL)
    {
        nfm_ipset_objm_path[0] = '\0';
    }
    else
    {
        STRSCPY(nfm_ipset_objm_path, path);
    }

    ds_tree_foreach(&nfm_ipset_list, ips)
    {
        if (!ips->ni_local) continue;
        nfm_ipset_objm_apply(ips);
    }
}

