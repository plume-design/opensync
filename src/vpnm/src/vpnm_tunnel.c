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
#include <stdbool.h>
#include <stdio.h>

#include "osn_types.h"
#include "const.h"
#include "log.h"
#include "ovsdb_table.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "schema.h"

#include "osn_vpn.h"
#include "vpnm.h"

/*
 * Keep track of VPN_Tunnel configs.
 */
struct vpnm_tunnel
{
    char           vt_name[C_MAXPATH_LEN];  /* Tunnel name */
    osn_vpn_t     *vt_vpn;                  /* Corresponding OSN layer tunnel object */
    bool           vt_enable;               /* Tunnel enable flag */

    ds_tree_node_t vt_tnode;
};

/*
 * Keep track of specific VPN types configs.
 *
 * This mainly serves to inform specific VPN types of any upper level VPN_Tunnel
 * configuration changes.
 */
struct vpnm_tunnel_cfg
{
    char                         tc_name[C_MAXPATH_LEN];  /* Tunnel name */
    notify_vpn_tunnel_chg_fn_t  *tc_notify_chg_cb;        /* Tunnel configuration change notification */

    ds_tree_node_t               tc_tnode;
};

/*
 * The OVSDB table VPN_Tunnel we're handling here.
 */
static ovsdb_table_t table_VPN_Tunnel;

/* Keeping track of VPN_Tunnel configs */
static ds_tree_t vpnm_tunnel_list = DS_TREE_INIT(ds_str_cmp, struct vpnm_tunnel, vt_tnode);

/* Keeping track of specific registered VPN tunnels configs.
 *
 * Tunnel configs of different types (IPsec, ...) may be registered as this
 * module handles the generic part of VPN tunnels handling (enable/disable,
 * VPN healthcheck, ...).
 */
static ds_tree_t vpnm_tunnel_cfg_list = DS_TREE_INIT(ds_str_cmp, struct vpnm_tunnel_cfg, tc_tnode);

void vpnm_tunnel_cfg_register(const char *tunnel_name, notify_vpn_tunnel_chg_fn_t *tunnel_chg_cb)
{
    struct vpnm_tunnel_cfg *vpn_tunnel_cfg;

    LOG(TRACE, "%s(), tunnel_name=%s", __func__, tunnel_name);

    vpn_tunnel_cfg = CALLOC(1, sizeof(*vpn_tunnel_cfg));
    STRSCPY(vpn_tunnel_cfg->tc_name, tunnel_name);
    vpn_tunnel_cfg->tc_notify_chg_cb = tunnel_chg_cb;

    ds_tree_insert(&vpnm_tunnel_cfg_list, vpn_tunnel_cfg, vpn_tunnel_cfg->tc_name);
}

void vpnm_tunnel_cfg_deregister(const char *tunnel_name)
{
    struct vpnm_tunnel_cfg *vpn_tunnel_cfg;

    LOG(TRACE, "vpnm_tunnel: %s(), tunnel_name=%s", __func__, tunnel_name);

    vpn_tunnel_cfg = ds_tree_find(&vpnm_tunnel_cfg_list, tunnel_name);
    if (vpn_tunnel_cfg != NULL)
    {
        ds_tree_remove(&vpnm_tunnel_cfg_list, vpn_tunnel_cfg);
        FREE(vpn_tunnel_cfg);
    }

    /*
     * If VPN tunnel config for this tunnel was deleted, update the
     * tunnel_state to down:
     */
    vpnm_tunnel_status_update(tunnel_name, OSN_VPN_CONN_STATE_DOWN);
}

/*
 * If there's a tunnel config registered for this tunnel, notify it
 * about a tunnel configuration change.
 */
static void vpnm_tunnel_cfg_notify_change(const char *tunnel_name)
{
    struct vpnm_tunnel_cfg *vpn_tunnel_cfg;

    LOG(TRACE, "%s: tunnel_name=%s", __func__, tunnel_name);

    vpn_tunnel_cfg = ds_tree_find(&vpnm_tunnel_cfg_list, tunnel_name);
    if (vpn_tunnel_cfg != NULL && vpn_tunnel_cfg->tc_notify_chg_cb != NULL)
    {
        LOG(DEBUG, "vpnm_tunnel: %s: notify tunnel config change", tunnel_name);

        vpn_tunnel_cfg->tc_notify_chg_cb(tunnel_name);
    }
}


static struct vpnm_tunnel *vpnm_tunnel_new(const char *tunnel_name)
{
    struct vpnm_tunnel *vpn_tunnel;

    vpn_tunnel = CALLOC(1, sizeof(*vpn_tunnel));
    STRSCPY(vpn_tunnel->vt_name, tunnel_name);

    /* Create OSN layer VPN tunnel instance: */
    vpn_tunnel->vt_vpn = osn_vpn_new(tunnel_name);

    ds_tree_insert(&vpnm_tunnel_list, vpn_tunnel, vpn_tunnel->vt_name);

    return vpn_tunnel;
}

static bool vpnm_tunnel_del(struct vpnm_tunnel *vpn_tunnel)
{
    /* Delete OSN layer VPN tunnel instance: */
    osn_vpn_del(vpn_tunnel->vt_vpn);

    ds_tree_remove(&vpnm_tunnel_list, vpn_tunnel);
    FREE(vpn_tunnel);
    return true;
}

static struct vpnm_tunnel *vpnm_tunnel_get(const char *tunnel_name)
{
    return ds_tree_find(&vpnm_tunnel_list, tunnel_name);
}

/*
 * VPNM healthcheck status update callback.
 *
 * Called by the OSN layer when there's a healthcheck status change detected.
 *
 * This function updates the VPN_Tunnel OVSDB table healthcheck_status field
 * accordingly.
 */
void vpnm_health_status_update(osn_vpn_t *osn_vpn, enum osn_vpn_health_status health_status)
{
    struct schema_VPN_Tunnel vpn_tunnel;
    const char *name;

    name = osn_vpn_name_get(osn_vpn);

    LOG(TRACE, "vpnm_tunnel: %s: healthcheck: update status to: %s",
            name, osn_vpn_health_status_to_str(health_status));

    memset(&vpn_tunnel, 0, sizeof(vpn_tunnel));
    vpn_tunnel._partial_update = true;

    STRSCPY(vpn_tunnel.healthcheck_status, osn_vpn_health_status_to_str(health_status));
    vpn_tunnel.healthcheck_status_exists = true;
    vpn_tunnel.healthcheck_status_present = true;

    if (ovsdb_table_update_where(
            &table_VPN_Tunnel,
            ovsdb_where_simple(SCHEMA_COLUMN(VPN_Tunnel, name), name),
            &vpn_tunnel) == -1)
    {
        LOG(ERR, "vpnm_tunnel: %s: Error updating VPN_Tunnel", name);
        return;
    }
}

/*
 * Set healthcheck config from schema to OSN.
 *
 * Return true if OVSDB config changed i.e. if it was actually set.
 */
static bool vpnm_tunnel_healthcheck_config_set(
        struct vpnm_tunnel *vpn_tunnel,
        const struct schema_VPN_Tunnel *conf,
        ovsdb_update_monitor_t *mon)
{
    bool config_changed = false;

    /* Enable/disable: */
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(VPN_Tunnel, healthcheck_enable)))
    {
        if (conf->healthcheck_enable_exists && conf->healthcheck_enable)
        {
            /* Enable OSN VPN healthcheck for this tunnel: */
            osn_vpn_healthcheck_enable_set(vpn_tunnel->vt_vpn, true);

            /* Register for healthcheck status changes notifications: */
            osn_vpn_healthcheck_notify_status_set(vpn_tunnel->vt_vpn, vpnm_health_status_update);
        }
        else
        {
            /* Disable OSN VPN healthcheck for this tunnel: */
            osn_vpn_healthcheck_enable_set(vpn_tunnel->vt_vpn, false);

            /* Healthcheck disabled, set healthcheck status to "na": */
            vpnm_health_status_update(vpn_tunnel->vt_vpn, OSN_VPN_HEALTH_STATUS_NA);
        }
        config_changed = true;
    }

    /* Healthcheck IP: */
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(VPN_Tunnel, healthcheck_ip)))
    {
        osn_ipany_addr_t ip = { 0 };

        if (conf->healthcheck_ip_exists)
        {
            if (osn_ipany_addr_from_str(&ip, conf->healthcheck_ip))
            {
                osn_vpn_healthcheck_ip_set(vpn_tunnel->vt_vpn, &ip);
            }
            else
            {
                LOG(ERR, "vpnm_tunnel: Error parsing IP address: %s", conf->healthcheck_ip);
            }
        }
        else
        {
            /* All-zero IP means: unset the healthcheck IP: */
            osn_vpn_healthcheck_ip_set(vpn_tunnel->vt_vpn, &ip);
        }
        config_changed = true;
    }

    /* Healthcheck interval: */
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(VPN_Tunnel, healthcheck_interval)))
    {
        if (conf->healthcheck_interval_exists)
        {
            osn_vpn_healthcheck_interval_set(vpn_tunnel->vt_vpn, conf->healthcheck_interval);
        }
        else
        {
            osn_vpn_healthcheck_interval_set(vpn_tunnel->vt_vpn, 0);
        }
        config_changed = true;
    }

    /* Healthcheck timeout: */
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(VPN_Tunnel, healthcheck_timeout)))
    {
        if (conf->healthcheck_timeout_exists)
        {
            osn_vpn_healthcheck_timeout_set(vpn_tunnel->vt_vpn, conf->healthcheck_timeout);
        }
        else
        {
            osn_vpn_healthcheck_timeout_set(vpn_tunnel->vt_vpn, 0);
        }
        config_changed = true;
    }

    /* Optional healthcheck source (source IP or source interface): */
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(VPN_Tunnel, healthcheck_src)))
    {
        if (conf->healthcheck_src_exists)
        {
            osn_vpn_healthcheck_src_set(vpn_tunnel->vt_vpn, conf->healthcheck_src);
        }
        else
        {
            osn_vpn_healthcheck_src_set(vpn_tunnel->vt_vpn, NULL);
        }
        config_changed = true;
    }

    return config_changed;
}

/*
 * Set VPN tunnel config.
 */
static bool vpnm_tunnel_config_set(
        struct vpnm_tunnel *vpn_tunnel,
        const struct schema_VPN_Tunnel *conf,
        ovsdb_update_monitor_t *mon)
{
    bool config_changed = false;

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(VPN_Tunnel, enable)))
    {
        if (conf->enable_exists && conf->enable)
        {
            vpn_tunnel->vt_enable = true;
            osn_vpn_enable_set(vpn_tunnel->vt_vpn, true);
            LOG(NOTICE, "vpnm_tunnel: %s: VPN tunnel enabled", vpn_tunnel->vt_name);
        }
        else
        {
            vpn_tunnel->vt_enable = false;
            osn_vpn_enable_set(vpn_tunnel->vt_vpn, false);
            LOG(NOTICE, "vpnm_tunnel: %s: VPN tunnel disabled", vpn_tunnel->vt_name);
        }
        config_changed = true;
    }

    return config_changed;
}

/*
 * Is this tunnel enabled?
 */
bool vpnm_tunnel_is_enabled(const char *tunnel_name)
{
    struct vpnm_tunnel *vpn_tunnel;

    vpn_tunnel = ds_tree_find(&vpnm_tunnel_list, tunnel_name);
    if (vpn_tunnel != NULL && vpn_tunnel->vt_enable)
    {
        return true;
    }
    else
    {
        return false;
    }
}

/*
 * Should be called by the specific VPN type (for instance IPsec) to set or
 * update the VPN tunnel connection status whenever it changes.
 *
 * This will update the OVSDB table VPN_Tunnel status fields accordingly.
 */
void vpnm_tunnel_status_update(const char *tunnel_name, enum osn_vpn_conn_state vpn_conn_state)
{
    struct schema_VPN_Tunnel vpn_tunnel;

    LOG(TRACE, "vpnm_tunnel: %s: update status to: %s", tunnel_name,
            osn_vpn_conn_state_to_str(vpn_conn_state));

    memset(&vpn_tunnel, 0, sizeof(vpn_tunnel));
    vpn_tunnel._partial_update = true;

    STRSCPY(vpn_tunnel.tunnel_status, osn_vpn_conn_state_to_str(vpn_conn_state));
    vpn_tunnel.tunnel_status_exists = true;
    vpn_tunnel.tunnel_status_present = true;

    if (ovsdb_table_update_where(
            &table_VPN_Tunnel,
            ovsdb_where_simple(SCHEMA_COLUMN(VPN_Tunnel, name), tunnel_name),
            &vpn_tunnel) == -1)
    {
        LOG(ERR, "vpnm_tunnel: %s: Error updating VPN_Tunnel", tunnel_name);
        return;
    }
}

static bool vpnm_tunnel_apply(struct vpnm_tunnel *vpn_tunnel)
{
    /*
     * Notify the corresponding registered specific VPN type config (for instance
     * IPsec config) handle to apply config.
     */
    vpnm_tunnel_cfg_notify_change(vpn_tunnel->vt_name);
    return true;
}

/*
 * OVSDB monitor update callback for VPN_Tunnel table.
 */
void callback_VPN_Tunnel(
        ovsdb_update_monitor_t *mon,
        struct schema_VPN_Tunnel *old,
        struct schema_VPN_Tunnel *new)
{
    struct vpnm_tunnel *vpn_tunnel;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            LOG(INFO, "vpnm_tunnel: %s: VPN_Tunnel update: NEW row", new->name);

            if (!OSN_VPN_IS_VALID_TUNNEL_NAME(new->name))
            {
                LOG(ERR, "vpnm_tunnel: %s: Invalid tunnel name", new->name);
                return;
            }

            if (vpnm_tunnel_get(new->name) != NULL)
            {
                LOG(WARN, "vpnm_tunnel: %s: A tunnel with the name already exists. Ignoring.", new->name);
                return;
            }
            vpn_tunnel = vpnm_tunnel_new(new->name);

            break;
        case OVSDB_UPDATE_MODIFY:
            LOG(INFO, "vpnm_tunnel: %s: VPN_Tunnel update: MODIFY row", new->name);

            vpn_tunnel = vpnm_tunnel_get(new->name);

            break;
        case OVSDB_UPDATE_DEL:
            LOG(INFO, "vpnm_tunnel: %s: VPN_Tunnel update: DELETE row", new->name);

            vpn_tunnel = vpnm_tunnel_get(new->name);
            if (vpn_tunnel == NULL)
            {
                LOG(ERROR, "vpnm_tunnel: %s: Cannot delete VPN tunnel: not found", new->name);
                return;
            }

            if (!vpnm_tunnel_del(vpn_tunnel))
            {
                LOG(ERROR, "vpnm_tunnel: %s: Error deleting VPN tunnel", new->name);
                return;
            }

            /*
             * A Deleted VPN_Tunnel means any configs attached to it should
             * now be disabled: notify them to apply the change:
             */
            vpnm_tunnel_cfg_notify_change(new->name);
            return;
        default:
            LOG(ERROR, "vpnm_tunnel: Monitor update error.");
            return;
    }

    if (mon->mon_type == OVSDB_UPDATE_NEW ||
        mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        if (vpn_tunnel == NULL)
        {
            LOG(ERROR, "vpnm_tunnel: %s: Could not obtain VPN tunnel handle", new->name);
            return;
        }

        /* Set tunnel config and apply it to OSN: */
        if (vpnm_tunnel_config_set(vpn_tunnel, new, mon))
        {
            vpnm_tunnel_apply(vpn_tunnel);
        }

        /* Set VPN healthcheck config and apply it to OSN: */
        if (vpnm_tunnel_healthcheck_config_set(vpn_tunnel, new, mon))
        {
            osn_vpn_healthcheck_apply(vpn_tunnel->vt_vpn);
            LOG(NOTICE, "vpnm_tunnel: %s: applied healthcheck config", new->name);
        }
    }
}

bool vpnm_tunnel_init(void)
{
    LOG(INFO, "Initializing VPNM VPN_Tunnel monitoring.");

    OVSDB_TABLE_INIT(VPN_Tunnel, name);
    OVSDB_TABLE_MONITOR(VPN_Tunnel, false);

    return true;
}
