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

#include <inttypes.h>
#include <jansson.h>

#include "ds_tree.h"
#include "evx.h"
#include "log.h"
#include "memutil.h"
#include "os_util.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"

#include "wano.h"
#include "wano_wan.h"

struct wano_wan_config_cache
{
    ovs_uuid_t                  wcc_uuid;               /* Row uuid */
    struct wano_wan_config      wcc_wan_config;         /* The WAN config */
    ds_tree_node_t              wcc_tnode_uuid;         /* Search by uuid */
    ds_tree_node_t              wcc_tnode_config;       /* Search by config */
};

struct wano_wan_status
{
    int64_t                     wws_priority;           /* Status by priority */
    enum wano_wan_config_type   wws_type;               /* WAN Type */
    enum wano_wan_config_status wws_status;             /* Last provisioning status */
    ds_tree_node_t              wws_tnode;              /* Tree node */
};

/*
 * Private functions
 */
static void callback_WAN_Config(
        ovsdb_update_monitor_t *self,
        struct schema_WAN_Config *old,
        struct schema_WAN_Config *new);

static bool wano_wan_config_from_schema(struct wano_wan_config *wc, struct schema_WAN_Config *schema);
static bool wano_wan_config_schema_set(struct schema_WAN_Config *schema);
static bool wano_wan_config_schema_del(struct schema_WAN_Config *schema);
static bool wano_wan_status_update(enum wano_wan_config_type type, int64_t priority);
static const char *wano_wan_config_other_config_get(struct schema_WAN_Config *schema, const char *key);
static void wano_wan_pause_deb_fn(struct ev_loop *loop, ev_debounce *w, int revent);
static void wano_wan_block_deb_fn(struct ev_loop *loop, ev_debounce *w, int revent);
static ds_key_cmp_t wano_wan_status_cmp;
static ds_key_cmp_t wano_wan_config_cmp;

/*
 * ===========================================================================
 *  Globals
 * ===========================================================================
 */
static ovsdb_table_t table_WAN_Config;

static ds_tree_t g_wano_wan_config_list = DS_TREE_INIT(
        wano_wan_config_cmp,
        struct wano_wan_config_cache,
        wcc_tnode_config);

static ds_tree_t g_wano_wan_config_cache = DS_TREE_INIT(
        ds_str_cmp,
        struct wano_wan_config_cache,
        wcc_tnode_uuid);

static ds_dlist_t g_wano_wan_list = DS_DLIST_INIT(wano_wan_t, ww_dnode);

static ev_debounce g_wano_wan_pause_deb;
static ev_debounce g_wano_wan_block_deb;

/*
 * ===========================================================================
 *  Internal function -- used only by WANO
 * ===========================================================================
 */
bool wano_wan_ovsdb_init(void)
{
    OVSDB_TABLE_INIT_NO_KEY(WAN_Config);
    if (!OVSDB_TABLE_MONITOR_F(WAN_Config, C_VPACK("-", "_version", "os_persist", "status")))
    {
        return false;
    }

    ev_debounce_init2(&g_wano_wan_pause_deb, wano_wan_pause_deb_fn, 3.0, 5.0);

    /*
     * The WAN configuration is being monitored asynchronously -- this means
     * that the pipelines might get started before the whole WAN configuration
     * is received (or any of it).
     *
     * Use a debounce timer to block here a little bit in order to give WANO
     * the chance to fully receive the WAN configuration before starting WAN
     * processing.
     */
    ev_debounce_init2(&g_wano_wan_block_deb, wano_wan_block_deb_fn, 2.0, 5.0);
    ev_debounce_start(EV_DEFAULT, &g_wano_wan_block_deb);

    LOG(NOTICE, "wan: Waiting on WAN configuration...");
    while (ev_is_active(&g_wano_wan_block_deb))
    {
        ev_run(EV_DEFAULT, EVRUN_ONCE);
    }
    LOG(INFO, "wan: Done.");

    return true;
}

void wano_wan_block_deb_fn(struct ev_loop *loop, ev_debounce *w, int revent)
{
    (void)loop;
    (void)w;
    (void)revent;
}

/*
 * ===========================================================================
 *  Public functions -- used by plug-ins and WANO
 * ===========================================================================
 */

/*
 * Initialize the WAN object. Insert it into the global list of WAN objects.
 *
 * The list is used to calculate the actual status of the WAN configuration
 * across several interfaces.
 */
void wano_wan_init(wano_wan_t *wan)
{
    memset(wan, 0, sizeof(*wan));
    wan->ww_next_priority = wan->ww_priority = INT64_MAX;

    ds_tree_init(
            &wan->ww_status_list,
            wano_wan_status_cmp,
            struct wano_wan_status,
            wws_tnode);

    ds_dlist_insert_tail(&g_wano_wan_list, wan);
}

void wano_wan_fini(wano_wan_t *wan)
{
    struct wano_wan_status *wss;
    ds_tree_iter_t iter;

    /* Free the status structure */
    ds_tree_foreach_iter(&wan->ww_status_list, wss, &iter)
    {
        ds_tree_iremove(&iter);
        /* Update the status */
        (void)wano_wan_status_update(wss->wws_type, wss->wws_priority);
        FREE(wss);
    }

    ds_dlist_remove(&g_wano_wan_list, wan);
}

void wano_wan_pause(wano_wan_t *wan, bool pause)
{
    wan->ww_do_pause = pause;
    /* Schedule a debounce timer for updating the global status */
    ev_debounce_start(EV_DEFAULT, &g_wano_wan_pause_deb);
}

void wano_wan_reset(wano_wan_t *ww)
{
    /*
     * Find highest priority WAN configuration and use that
     */
    ww->ww_next_priority = ww->ww_priority = INT64_MAX;
    wano_wan_next(ww);
    ww->ww_rollover = 0;
}

void wano_wan_rollover(wano_wan_t *ww)
{
    /*
     * Just set the current priority to the lowest possible. The next call
     * to wan_wan_next() will just rollover
     */
    ww->ww_next_priority = INT64_MIN;
}

int wano_wan_rollover_get(wano_wan_t *wan)
{
    return wan->ww_rollover;
}

/*
 * Take the current priority, and compute the next WAN configuration.
 *
 * The next configuration is the configuration with the next highest priority.
 * Configurations with the same priority are grouped together into a single
 * config.
 */
void wano_wan_next(wano_wan_t *ww)
{
    (void)ww;
    struct wano_wan_config_cache *wcc;

    int64_t new_priority = INT64_MIN;
    int64_t max_priority = INT64_MIN;

    ds_tree_foreach(&g_wano_wan_config_cache, wcc)
    {
        if (wcc->wcc_wan_config.wc_priority < ww->ww_next_priority &&
                wcc->wcc_wan_config.wc_priority > new_priority)
        {
            new_priority = wcc->wcc_wan_config.wc_priority;
        }

        if (wcc->wcc_wan_config.wc_priority > max_priority)
        {
            max_priority = wcc->wcc_wan_config.wc_priority;
        }
    }

    /* The list is empty, set max_priority to INT64_MAX */
    if (max_priority == INT64_MIN)
    {
        max_priority = INT64_MAX;
    }

    /* Wrap around if priority not found */
    if (new_priority == INT64_MIN)
    {
        ww->ww_rollover++;
        LOG(INFO, "wano: WAN rollover count %d", ww->ww_rollover);
        new_priority = max_priority;
    }

    LOG(NOTICE, "wano_wan: Next priority is %"PRId64" -> %"PRId64,
            ww->ww_priority,
            new_priority);

    ww->ww_next_priority = ww->ww_priority = new_priority;
}

/*
 * Check if current config is the one with the lowest priority. If so,
 * the next invocation of wano_wan_next() will result in rollover.
 */
bool wano_wan_is_last_config(const wano_wan_t *ww)
{
    struct wano_wan_config_cache *wcc;

    if (ww == NULL) { return false; }

    ds_tree_foreach(&g_wano_wan_config_cache, wcc)
    {
        if (wcc->wcc_wan_config.wc_priority < ww->ww_next_priority)
        {
            return false;
        }
    }

    return true;
}

bool wano_wan_config_get(wano_wan_t *ww, enum wano_wan_config_type type, struct wano_wan_config *wc_out)
{
    struct wano_wan_config wc_key;
    struct wano_wan_config_cache *wcc;

    if (ww == NULL) return false;

    wc_key.wc_type = type;
    wc_key.wc_priority = ww->ww_priority;

    wcc = ds_tree_find(&g_wano_wan_config_list, &wc_key);
    if (wcc == NULL) return false;

    *wc_out = wcc->wcc_wan_config;

    return true;
}

void wano_wan_status_set(
        wano_wan_t *wan,
        enum wano_wan_config_type type,
        enum wano_wan_config_status status)
{
    struct wano_wan_status wws_key;
    struct wano_wan_status *wws;

    wws_key.wws_type = type;
    wws_key.wws_priority = wan->ww_priority;

    wws = ds_tree_find(&wan->ww_status_list, &wws_key);
    if (wws == NULL)
    {
        wws = MALLOC(sizeof(*wws));
        wws->wws_type = type;
        wws->wws_priority = wan->ww_priority;
        ds_tree_insert(&wan->ww_status_list, wws, wws);
    }

    wws->wws_status = status;
    (void)wano_wan_status_update(type, wan->ww_priority);
}

/*
 * Update the status of the WAN type `type` with priority `priority`
 */
bool wano_wan_status_update(enum wano_wan_config_type type, int64_t priority)
{
    enum wano_wan_config_status status;
    struct schema_WAN_Config schema;
    struct wano_wan_status wws_key;
    struct wano_wan_status *wws;
    char *typestr;
    wano_wan_t *wan;
    json_t *where;
    int rc;

    wws_key.wws_type = type;
    wws_key.wws_priority = priority;

    /* Scan interfaces, figure out what status to set. The algorithm is as
     * follows:
     *
     * - if any interface has a "success" status, set status to "success"
     * - if there are interfaces without states (NONE), set status to "empty"
     * - if all interfaces have an "error" status, set the status to "error"
     */
    status = WC_STATUS_ERROR;
    ds_dlist_foreach(&g_wano_wan_list, wan)
    {
        if (wan->ww_is_paused) continue;

        wws = ds_tree_find(&wan->ww_status_list, &wws_key);
        if (wws == NULL)
        {
            status = WC_STATUS_NONE;
            continue;
        }

        if (wws->wws_status == WC_STATUS_SUCCESS)
        {
            status = WC_STATUS_SUCCESS;
            break;
        }
    }

    /*
     * Update OVSDB
     */
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

    typestr = "";
    switch (type)
    {
        case WC_TYPE_PPPOE:
            typestr = "pppoe";
            break;

        case WC_TYPE_VLAN:
            typestr = "vlan";
            break;

        case WC_TYPE_STATIC_IPV4:
            typestr = "static_ipv4";
            break;

        case WC_TYPE_DHCP:
            typestr = "dhcp";
    }

    where = ovsdb_where_multi(
            ovsdb_where_simple_typed(SCHEMA_COLUMN(WAN_Config, priority), &(json_int_t){priority}, OCLM_INT),
            ovsdb_where_simple_typed(SCHEMA_COLUMN(WAN_Config, type), typestr, OCLM_STR),
            NULL);

    /*
     * Set status of current row
     */
    rc = ovsdb_table_update_where(
            &table_WAN_Config,
            where,
            &schema);
    if (rc <= 0)
    {
        LOG(ERR, "wano_wan: Error updating status for WAN type %s with priority %"PRId64".",
                typestr,
                priority);
        return false;
    }

    return true;
}

void wano_wan_pause_deb_fn(struct ev_loop *loop, ev_debounce *w, int revent)
{
    (void)loop;
    (void)w;
    (void)revent;

    struct wano_wan_config_cache *wcc;
    wano_wan_t *wan;

    bool do_status_update = false;

    /* Scan list of WAN's and contemplate the action to execute */
    ds_dlist_foreach(&g_wano_wan_list, wan)
    {
        if (wan->ww_do_pause == wan->ww_is_paused) continue;
        wan->ww_is_paused = wan->ww_do_pause;
        do_status_update = true;
    }

    if (!do_status_update) return;

    /* Update the global status of all configurations */
    ds_tree_foreach(&g_wano_wan_config_list, wcc)
    {
        wano_wan_status_update(wcc->wcc_wan_config.wc_type, wcc->wcc_wan_config.wc_priority);
    }
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
    /* Restart the blocking debounce timer if its active */
    if (ev_is_active(&g_wano_wan_block_deb))
    {
        ev_debounce_start(EV_DEFAULT, &g_wano_wan_block_deb);
    }

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            (void)wano_wan_config_schema_set(new);
            break;

        case OVSDB_UPDATE_MODIFY:
            (void)wano_wan_config_schema_set(new);
            break;

        case OVSDB_UPDATE_DEL:
            (void)wano_wan_config_schema_del(old);
            break;

        default:
            LOG(ERR, "wan_config: WAN_Config monitor update error.");
            break;
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
        size_t uname_len = 0;
        size_t pword_len = 0;

        username = wano_wan_config_other_config_get(schema, "username");
        password = wano_wan_config_other_config_get(schema, "password");
        if (username == NULL || password == NULL)
        {
            LOG(ERR, "wan_config: PPPoE config is missing `username` or `password` settings.");
            return false;
        }

        uname_len = strlen(username);
        pword_len = strlen(password);

        /* Check that credentials' lengths are valid. */
        if (uname_len < 1 || uname_len > 128 || pword_len < 1 || pword_len > 128)
        {
            LOG(ERR, "wan_config: Invalid PPPoE `username` or `password`.");
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

        if (svlan == NULL)
        {
            LOG(ERR, "wan_config: VLAN config is missing the required `vlan_id` setting.");
            return false;
        }

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

        /* Check if the gateway is reachable with the current settings */
        int prefix = osn_ip_addr_to_prefix(&netmask);
        osn_ip_addr_t ipnet = ipaddr;
        osn_ip_addr_t gwnet = gateway;

        ipnet.ia_prefix = prefix;
        gwnet.ia_prefix = prefix;

        ipnet = osn_ip_addr_subnet(&ipnet);
        gwnet = osn_ip_addr_subnet(&gwnet);

        if (osn_ip_addr_cmp(&ipnet, &gwnet) != 0)
        {
            LOG(ERR, "wan_config: Static IPv4 gateway/ipaddr subnet mismatch.");
            return false;
        }

        wc->wc_type = WC_TYPE_STATIC_IPV4;
        wc->wc_type_static_ipv4.wc_ipaddr = ipaddr;
        wc->wc_type_static_ipv4.wc_netmask = netmask;
        wc->wc_type_static_ipv4.wc_gateway = gateway;
        wc->wc_type_static_ipv4.wc_primary_dns = primary_dns;
        wc->wc_type_static_ipv4.wc_secondary_dns = secondary_dns;
    }
    else if (strcmp(schema->type, "dhcp") == 0)
    {
        wc->wc_type = WC_TYPE_DHCP;
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
    }
    else
    {
        ds_tree_remove(&g_wano_wan_config_list, wcc);
        ds_tree_remove(&g_wano_wan_config_cache, wcc);
    }

    /* Ignore bad entries */
    if (!wano_wan_config_from_schema(&wcc->wcc_wan_config, schema))
    {
        FREE(wcc);
        return false;
    }

    ds_tree_insert(&g_wano_wan_config_list, wcc, &wcc->wcc_wan_config);
    ds_tree_insert(&g_wano_wan_config_cache, wcc, wcc->wcc_uuid.uuid);

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

    ds_tree_remove(&g_wano_wan_config_list, wcc);
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

int wano_wan_status_cmp(const void *_a, const void *_b)
{
    const struct wano_wan_status *a = _a;
    const struct wano_wan_status *b = _b;

    if (a->wws_type != b->wws_type)
    {
        return a->wws_type - b->wws_type;
    }

    return a->wws_priority - b->wws_priority;
}

int wano_wan_config_cmp(const void *_a, const void *_b)
{
    const struct wano_wan_config *a = _a;
    const struct wano_wan_config *b = _b;

    if (a->wc_type != b->wc_type)
    {
        return a->wc_type - b->wc_type;
    }

    return a->wc_priority - b->wc_priority;
}
