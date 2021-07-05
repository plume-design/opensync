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

#include "ds_tree.h"
#include "log.h"
#include "memutil.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "os_util.h"

#include "wano.h"
#include "wano_wan_config.h"

struct wano_wan_config_cache
{
    ovs_uuid_t              wcc_uuid;
    struct wano_wan_config  wcc_wan_config;
    ds_tree_node_t          wcc_tnode;
};

/*
 * Per interface WAN configuration status
 */
struct wano_wan_config_ifstatus
{
    char                            wcs_ifname[C_IFNAME_LEN];
    enum wano_wan_config_status     wcs_status[WC_STATUS_MAX];
    ds_tree_node_t                  wcs_tnode;
};

static void callback_WAN_Config(
        ovsdb_update_monitor_t *self,
        struct schema_WAN_Config *old,
        struct schema_WAN_Config *new);

static bool wano_wan_config_from_schema(struct wano_wan_config *wc, struct schema_WAN_Config *schema);
struct wano_wan_config_cache *wano_wan_config_find(enum wano_wan_config_type type);
static bool wano_wan_config_schema_set(struct schema_WAN_Config *schema);
static bool wano_wan_config_schema_del(struct schema_WAN_Config *schema);
static const char *wano_wan_config_other_config_get(struct schema_WAN_Config *schema, const char *key);
static bool wano_wan_config_status_update(enum wano_wan_config_type type);

static ovsdb_table_t table_WAN_Config;

static ds_tree_t g_wano_wan_config_cache = DS_TREE_INIT(
        ds_str_cmp,
        struct wano_wan_config_cache,
        wcc_tnode);

static ds_tree_t g_wano_wan_config_iflist = DS_TREE_INIT(
        ds_str_cmp,
        struct wano_wan_config_ifstatus,
        wcs_tnode);

/*
 * ===========================================================================
 *  Public functions
 * ===========================================================================
 */
bool wano_wan_config_init(void)
{
    OVSDB_TABLE_INIT_NO_KEY(WAN_Config);
    if (!OVSDB_TABLE_MONITOR_F(WAN_Config, C_VPACK("-", "_version", "os_persist", "status")))
    {
        return false;
    }

    return true;
}

bool wano_wan_config_get(struct wano_wan_config *wc, enum wano_wan_config_type type)
{
    struct wano_wan_config_cache *wcc = wano_wan_config_find(type);
    if (wcc == NULL) return false;

    memcpy(wc, &wcc->wcc_wan_config, sizeof(*wc));

    return true;
}

struct wano_wan_config_cache *wano_wan_config_find(enum wano_wan_config_type type)
{
    struct wano_wan_config_cache *wcc;

    struct wano_wan_config_cache *retval = NULL;

    /*
     * Scan current list of WAN configuration and return the entry with the
     * highest priority
     */
    ds_tree_foreach(&g_wano_wan_config_cache, wcc)
    {
        /* Filter by type and priority */
        if (wcc->wcc_wan_config.wc_type != type) continue;
        if (retval != NULL &&
                (retval->wcc_wan_config.wc_priority > wcc->wcc_wan_config.wc_priority)) continue;

        retval = wcc;
    }

    return retval;
}

/*
 * ===========================================================================
 *  Private functions
 * ===========================================================================
 */
static void callback_WAN_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_WAN_Config *old,
        struct schema_WAN_Config *new)
{
    bool wano_restart = false;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            wano_restart = wano_wan_config_schema_set(new);
            break;

        case OVSDB_UPDATE_MODIFY:
            wano_restart = wano_wan_config_schema_set(new);
            break;

        case OVSDB_UPDATE_DEL:
            wano_restart = wano_wan_config_schema_del(old);
            break;

        default:
            LOG(ERR, "wan_config: WAN_Config monitor update error.");
            break;
    }

    if (wano_restart)
    {
        wano_ppline_restart_all();
    }
}

bool wano_wan_config_from_schema(struct wano_wan_config *wc, struct schema_WAN_Config *schema)
{
    memset(wc, 0, sizeof(*wc));
    wc->wc_priority = schema->priority_exists ? schema->priority : -1;
    wc->wc_enable = schema->enable;

    if (strcmp(schema->type, "pppoe") == 0)
    {
        const char *username;
        const char *password;

        username = wano_wan_config_other_config_get(schema, "username");
        password = wano_wan_config_other_config_get(schema, "password");
        if (username == NULL || password == NULL)
        {
            LOG(ERR, "wan_config: PPPoE config is missing `username` or `password` settings.");
            return false;
        }

        wc->wc_type = WC_TYPE_PPPOE;
        STRSCPY(wc->wc_type_pppoe.wc_username, username);
        STRSCPY(wc->wc_type_pppoe.wc_password, password);
    }
    else if (strcmp(schema->type, "vlan") == 0)
    {
        const char *svlan = wano_wan_config_other_config_get(schema, "vlan_id");
        long vlan;

        if (!os_strtoul((char *)svlan, &vlan, 0))
        {
            LOG(ERR, "wan_config: Invalid VLAN value: %s", svlan);
            return false;
        }

        if (vlan < C_VLAN_MIN || vlan > C_VLAN_MAX)
        {
            LOG(ERR, "wan_config: VLAN value out of range: %ld", vlan);
            return false;
        }

        wc->wc_type = WC_TYPE_VLAN;
        wc->wc_type_vlan.wc_vlanid = vlan;
    }
    else if (strcmp(schema->type, "static_ipv4") == 0)
    {
        const char *sipaddr;
        const char *snetmask;
        const char *sgateway;
        const char *sprimary_dns;
        const char *ssecondary_dns;
        osn_ip_addr_t ipaddr;
        osn_ip_addr_t netmask;
        osn_ip_addr_t gateway;
        osn_ip_addr_t primary_dns;
        osn_ip_addr_t secondary_dns;

        sipaddr = wano_wan_config_other_config_get(schema, "ip");
        snetmask = wano_wan_config_other_config_get(schema, "subnet");
        sgateway = wano_wan_config_other_config_get(schema, "gateway");
        sprimary_dns = wano_wan_config_other_config_get(schema, "primary_dns");
        ssecondary_dns = wano_wan_config_other_config_get(schema, "secondary_dns");

        if (sipaddr == NULL || !osn_ip_addr_from_str(&ipaddr, sipaddr))
        {
            LOG(ERR, "wan_config: Missing or invalid `ip` setting: %s",
                    sipaddr == NULL ? "(null)" : sipaddr);
            return false;
        }

        if (snetmask == NULL || !osn_ip_addr_from_str(&netmask, snetmask))
        {
            LOG(ERR, "wan_config: Missing or invalid `subnet` setting: %s",
                    sipaddr == NULL ? "(null)" : sipaddr);
            return false;
        }

        if (sgateway == NULL || !osn_ip_addr_from_str(&gateway, sgateway))
        {
            LOG(ERR, "wan_config: Missing or invalid `gateway` setting: %s",
                    sgateway == NULL ? "(null)" : sgateway);
            return false;
        }

        if (sprimary_dns == NULL || !osn_ip_addr_from_str(&primary_dns, sprimary_dns))
        {
            LOG(ERR, "wan_config: Missing or invalid `primary_dns` setting: %s",
                    sgateway == NULL ? "(null)" : sgateway);
            return false;
        }

        if (ssecondary_dns == NULL || !osn_ip_addr_from_str(&secondary_dns, ssecondary_dns))
        {
            LOG(DEBUG, "wan_config: Missing or invalid `secondary_dns` setting: %s",
                    ssecondary_dns == NULL ? "(null)" : ssecondary_dns);
            secondary_dns = OSN_IP_ADDR_INIT;
        }

        wc->wc_type = WC_TYPE_STATIC_IPV4;
        wc->wc_type_static_ipv4.wc_ipaddr = ipaddr;
        wc->wc_type_static_ipv4.wc_netmask = netmask;
        wc->wc_type_static_ipv4.wc_gateway = gateway;
        wc->wc_type_static_ipv4.wc_primary_dns = primary_dns;
        wc->wc_type_static_ipv4.wc_secondary_dns = secondary_dns;
    }
    else
    {
        LOG(ERR, "wan_config: Unknown WAN type: %s", schema->type);
        return false;
    }

    return true;
}

bool wano_wan_config_schema_set(struct schema_WAN_Config *schema)
{
    struct wano_wan_config_cache *wcc;

    wcc = ds_tree_find(&g_wano_wan_config_cache, schema->_uuid.uuid);
    if (wcc == NULL)
    {
        wcc = CALLOC(1, sizeof(*wcc));
        memcpy(&wcc->wcc_uuid, &schema->_uuid, sizeof(wcc->wcc_uuid));
        ds_tree_insert(&g_wano_wan_config_cache, wcc, wcc->wcc_uuid.uuid);
    }

    if (!wano_wan_config_from_schema(&wcc->wcc_wan_config, schema))
    {
        /* Do not cache invalid entries */
        wano_wan_config_schema_del(schema);
        return false;
    }

    return true;
}

bool wano_wan_config_schema_del(struct schema_WAN_Config *schema)
{
    struct wano_wan_config_cache *wcc;

    wcc = ds_tree_find(&g_wano_wan_config_cache, schema->_uuid.uuid);
    if (wcc == NULL)
    {
        return false;
    }

    ds_tree_remove(&g_wano_wan_config_cache, wcc);
    FREE(wcc);

    return true;
}

/*
 * Retrieve a single key value from `other_config`
 */
const char *wano_wan_config_other_config_get(struct schema_WAN_Config *schema, const char *key)
{
    int ii;

    for (ii = 0; ii < schema->other_config_len; ii++)
    {
        if (strcmp(schema->other_config_keys[ii], key) == 0)
        {
            return schema->other_config[ii];
        }
    }

    return NULL;
}

void wano_wan_config_status_add(const char *ifname)
{
    struct wano_wan_config_ifstatus *wcs;

    wcs = ds_tree_find(&g_wano_wan_config_iflist, (void *)ifname);
    if (wcs != NULL)
    {
        memset(&wcs->wcs_status, 0, sizeof(wcs->wcs_status));
        return;
    }

    wcs = CALLOC(1, sizeof(*wcs));
    STRSCPY(wcs->wcs_ifname, ifname);
    ds_tree_insert(&g_wano_wan_config_iflist, wcs, wcs->wcs_ifname);
}

void wano_wan_config_status_del(const char *ifname)
{
    struct wano_wan_config_ifstatus *wcs;

    wcs = ds_tree_find(&g_wano_wan_config_iflist, (void *)ifname);
    if (wcs == NULL) return;

    ds_tree_remove(&g_wano_wan_config_iflist, wcs);
    FREE(wcs);
}

bool wano_wan_config_status_set(
        const char *ifname,
        enum wano_wan_config_type type,
        enum wano_wan_config_status status)
{
    struct wano_wan_config_ifstatus *wcs;

    wcs = ds_tree_find(&g_wano_wan_config_iflist, (void *)ifname);
    if (wcs == NULL)
    {
        LOG(WARN, "wano: Interface %s not eligible for WAN status updates.", ifname);
        return false;
    }

    wcs->wcs_status[type] = status;

    return wano_wan_config_status_update(type);
}

bool wano_wan_config_status_update(enum wano_wan_config_type type)
{
    struct schema_WAN_Config schema;
    struct wano_wan_config_cache *wcc;
    struct wano_wan_config_ifstatus *ifs;
    int rc;

    enum wano_wan_config_status status;

    wcc = wano_wan_config_find(type);
    if (wcc == NULL)
    {
        /* No active configuration found, nothing to do */
        return true;
    }

    /* Scan interfaces, figure out what status to set. The algorithm is as
     * follows:
     *
     * - if any interface has a "success" status, set status to "success"
     * - if there are interfaces without states (NONE), set status to "empty"
     * - if all interfaces have an "error" status, set the status to "error"
     */
    status = WC_STATUS_ERROR;
    ds_tree_foreach(&g_wano_wan_config_iflist, ifs)
    {
        if (ifs->wcs_status[type] == WC_STATUS_SUCCESS)
        {
            status = WC_STATUS_SUCCESS;
            break;
        }
        else if (ifs->wcs_status[type] == WC_STATUS_NONE)
        {
            status = WC_STATUS_NONE;
        }
    }

    memset(&schema, 0, sizeof(schema));
    schema._partial_update = true;

    switch (status)
    {
        case WC_STATUS_NONE:
            /* Use an empty set */
            SCHEMA_UNSET_FIELD(schema.status);
            break;

        case WC_STATUS_SUCCESS:
            SCHEMA_SET_STR(schema.status, "success");
            break;

        case WC_STATUS_ERROR:
            SCHEMA_SET_STR(schema.status, "error");
            break;
    }

    /*
     * Set status of current row
     */
    rc = ovsdb_table_update_where(
            &table_WAN_Config,
            ovsdb_where_uuid("_uuid", wcc->wcc_uuid.uuid),
            &schema);
    if (rc <= 0)
    {
        return false;
    }

    /*
     * Celar status of all other rows of the same type
     */
    SCHEMA_UNSET_FIELD(schema.status);
    (void)ovsdb_table_update_where(
            &table_WAN_Config,
            ovsdb_tran_cond(OCLM_UUID, "_uuid", OFUNC_NEQ, wcc->wcc_uuid.uuid),
            &schema);

    return true;
}

