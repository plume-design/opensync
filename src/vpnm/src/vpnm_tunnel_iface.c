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

#include "vpnm.h"
#include "osn_types.h"
#include "osn_ipsec.h"
#include "osn_tunnel_iface.h"
#include "memutil.h"
#include "log.h"
#include "util.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "ds_tree.h"

#define SCHEMA_STR_STATUS_DISABLED  "disabled"
#define SCHEMA_STR_STATUS_ENABLED   "enabled"
#define SCHEMA_STR_STATUS_ERROR     "error"

/*
 * Keep track of Tunnel_Interface configs
 */
struct vpnm_tunnel_iface
{
    char                 ti_name[C_MAXPATH_LEN];  /* Interface name */
    osn_tunnel_iface_t  *ti_tunnel_iface;         /* Corresponding OSN layer
                                                   * tunnel interface object. */

    ds_tree_node_t ti_tnode;
};

enum vpnm_tunnel_iface_status
{
    VPNM_TUNNEL_IFACE_STATUS_DISABLED,
    VPNM_TUNNEL_IFACE_STATUS_ENABLED,
    VPNM_TUNNEL_IFACE_STATUS_ERROR,
};

/*
 * The OVSDB table Tunnel_Interface we're handling here.
 */
static ovsdb_table_t table_Tunnel_Interface;

/* Keeping track of Tunnel_Interface configs */
static ds_tree_t vpnm_tunnel_iface_list = DS_TREE_INIT(ds_str_cmp, struct vpnm_tunnel_iface, ti_tnode);

static bool vpnm_tunnel_iface_ovsdb_status_update(const char *if_name, enum vpnm_tunnel_iface_status new_status);

static struct vpnm_tunnel_iface *vpnm_tunnel_iface_new(const char *if_name)
{
    struct vpnm_tunnel_iface *vpnm_tif;

    vpnm_tif = CALLOC(1, sizeof(*vpnm_tif));
    STRSCPY(vpnm_tif->ti_name, if_name);

    /* Create OSN layer tunnel interface instance: */
    vpnm_tif->ti_tunnel_iface = osn_tunnel_iface_new(if_name);
    if (vpnm_tif->ti_tunnel_iface == NULL)
    {
        LOG(ERR, "vpnm_tunnel_iface: %s: Error creating OSN tunnel interface context", if_name);
        FREE(vpnm_tif);
        return NULL;
    }

    ds_tree_insert(&vpnm_tunnel_iface_list, vpnm_tif, vpnm_tif->ti_name);

    return vpnm_tif;
}

static bool vpnm_tunnel_iface_del(struct vpnm_tunnel_iface *vpnm_tif)
{
    bool rv = true;

    /* Delete OSN layer tunnel interface instance: */
    if (!osn_tunnel_iface_del(vpnm_tif->ti_tunnel_iface))
    {
        LOG(ERROR, "vpnm_tunnel_iface: %s: Error destroying OSN tunnel interface", vpnm_tif->ti_name);
        rv = false;
    }

    ds_tree_remove(&vpnm_tunnel_iface_list, vpnm_tif);
    FREE(vpnm_tif);
    return rv;
}

static struct vpnm_tunnel_iface *vpnm_tunnel_iface_get(const char *if_name)
{
    return ds_tree_find(&vpnm_tunnel_iface_list, if_name);
}

static enum osn_tunnel_iface_type util_tunnel_iftype_from_schemastr(const char *if_type)
{
    if (strcmp(if_type, "vti") == 0)
        return OSN_TUNNEL_IFACE_TYPE_VTI;
    else if (strcmp(if_type, "vti6") == 0)
        return OSN_TUNNEL_IFACE_TYPE_VTI6;
    else if (strcmp(if_type, "ip6tnl") == 0)
        return OSN_TUNNEL_IFACE_TYPE_IP6TNL;
    else
        return OSN_TUNNEL_IFACE_TYPE_NOT_SET;
}

static enum osn_tunnel_iface_mode util_tunnel_ifmode_from_schemastr(const char *mode)
{
    if (strcmp(mode, "any") == 0)
        return OSN_TUNNEL_IFACE_MODE_ANY;
    else if (strcmp(mode, "ipip6") == 0)
        return OSN_TUNNEL_IFACE_MODE_IPIP6;
    else if (strcmp(mode, "ip6ip6") == 0)
        return OSN_TUNNEL_IFACE_MODE_IP6IP6;
    else
        return OSN_TUNNEL_IFACE_MODE_NOT_SET;
}

/*
 * Set tunnel interface config from schema to OSN.
 */
static bool vpnm_tunnel_iface_config_set(
        struct vpnm_tunnel_iface *vpnm_tif,
        const struct schema_Tunnel_Interface *conf)
{
    osn_ipany_addr_t local_endpoint;
    osn_ipany_addr_t remote_endpoint;
    bool rv = true;

    if (conf->if_type_exists)
    {
        rv &= osn_tunnel_iface_type_set(vpnm_tif->ti_tunnel_iface, util_tunnel_iftype_from_schemastr(conf->if_type));
    }

    /* Local and remote endpoint addresses: */
    if (conf->local_endpoint_addr_exists && conf->remote_endpoint_addr_exists)
    {
        if (!osn_ipany_addr_from_str(&local_endpoint, conf->local_endpoint_addr))
        {
            LOG(ERR, "vpnm_tunnel_iface: Error parsing Tunnel Interface local_endpoint_addr");
            return false;
        }
        if (!osn_ipany_addr_from_str(&remote_endpoint, conf->remote_endpoint_addr))
        {
            LOG(ERR, "vpnm_tunnel_iface: Error parsing Tunnel Interface remote_endpoint_addr");
            return false;
        }

        rv &= osn_tunnel_iface_endpoints_set(vpnm_tif->ti_tunnel_iface, local_endpoint, remote_endpoint);
    }

    /* XFRM mark, if set: */
    if (conf->key_exists)
    {
        rv &= osn_tunnel_iface_key_set(vpnm_tif->ti_tunnel_iface, conf->key);
    }
    else
    {
        rv &= osn_tunnel_iface_key_set(vpnm_tif->ti_tunnel_iface, 0);
    }

    if (conf->mode_exists)
    {
        rv &= osn_tunnel_iface_mode_set(vpnm_tif->ti_tunnel_iface, util_tunnel_ifmode_from_schemastr(conf->mode));
    }
    else
    {
        rv &= osn_tunnel_iface_mode_set(vpnm_tif->ti_tunnel_iface, OSN_TUNNEL_IFACE_MODE_NOT_SET);
    }

    if (conf->dev_if_name_exists)
    {
        rv &= osn_tunnel_iface_dev_set(vpnm_tif->ti_tunnel_iface, conf->dev_if_name);
    }
    else
    {
        rv &= osn_tunnel_iface_dev_set(vpnm_tif->ti_tunnel_iface, "");
    }

    /* Enable/disable: */
    if (conf->enable_exists && conf->enable)
    {
        rv &= osn_tunnel_iface_enable_set(vpnm_tif->ti_tunnel_iface, true);
    }
    else
    {
        rv &= osn_tunnel_iface_enable_set(vpnm_tif->ti_tunnel_iface, false);
    }

    return rv;
}

/*
 * Apply the tunnel interface config to OSN.
 */
static bool vpnm_tunnel_iface_apply(struct vpnm_tunnel_iface *vpnm_tif)
{
    return osn_tunnel_iface_apply(vpnm_tif->ti_tunnel_iface);
}

/*
 * OVSDB monitor update callback for Tunnel_Interface
 */
void callback_Tunnel_Interface(
        ovsdb_update_monitor_t *mon,
        struct schema_Tunnel_Interface *old,
        struct schema_Tunnel_Interface *new)
{
    struct vpnm_tunnel_iface *vpnm_tif;
    bool rv;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            /* Insert case */
            LOG(INFO, "vpnm_tunnel_iface: %s: Tunnel_Interface update: NEW row", new->if_name);
            vpnm_tif = vpnm_tunnel_iface_new(new->if_name);
            break;

        case OVSDB_UPDATE_MODIFY:
            /* Update case */
            LOG(INFO, "vpnm_tunnel_iface: %s: Tunnel_Interface update: MODIFY row", new->if_name);

            if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Tunnel_Interface, status)))
            {
                /* Ignore OVSDB update callbacks for column 'status' as those are initiated by us */
                return;
            }

            vpnm_tif = vpnm_tunnel_iface_get(new->if_name);

            break;

        case OVSDB_UPDATE_DEL:
            LOG(INFO, "vpnm_tunnel_iface: %s: Tunnel_Interface update: DELETE row", new->if_name);
            vpnm_tif = vpnm_tunnel_iface_get(new->if_name);
            if (vpnm_tif == NULL)
            {
                LOG(ERROR, "vpnm_tunnel_iface: %s: Cannot delete tunnel interface: not found", new->if_name);
                return;
            }

            if (!vpnm_tunnel_iface_del(vpnm_tif))
            {
                LOG(ERROR, "vpnm_tunnel_iface: %s: Error deleting tunnel interface", new->if_name);
                return;
            }
            return;

        default:
            LOG(ERROR, "vpnm_tunnel_iface: Monitor update error.");
            return;
    }

    if (mon->mon_type == OVSDB_UPDATE_NEW ||
        mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        if (vpnm_tif == NULL)
        {
            LOG(ERROR, "vpnm_tunnel_iface: %s: Could not obtain tunnel interface handle", new->if_name);
            return;
        }

        /* Set tunnel interface config: */
        if (!vpnm_tunnel_iface_config_set(vpnm_tif, new))
        {
            vpnm_tunnel_iface_ovsdb_status_update(new->if_name, VPNM_TUNNEL_IFACE_STATUS_ERROR);
            LOG(ERROR, "vpnm_tunnel_iface: %s: Error setting Tunnel_Interface configuration", new->if_name);
            return;
        }

        /* Apply the config to OSN: */
        rv = vpnm_tunnel_iface_apply(vpnm_tif);
        if (!new->enable_exists || !new->enable)
        {
            vpnm_tunnel_iface_ovsdb_status_update(new->if_name, VPNM_TUNNEL_IFACE_STATUS_DISABLED);
        }
        else
        {
            if (rv)
            {
                vpnm_tunnel_iface_ovsdb_status_update(new->if_name, VPNM_TUNNEL_IFACE_STATUS_ENABLED);
            }
            else
            {
                vpnm_tunnel_iface_ovsdb_status_update(new->if_name, VPNM_TUNNEL_IFACE_STATUS_ERROR);
            }
        }

        if (!rv)
        {
            LOG(ERROR, "vpnm_tunnel_iface: %s: Error applying Tunnel Interface config.", new->if_name);
            return;
        }
        LOG(NOTICE, "vpnm_tunnel_iface: %s: applied tunnel interface config", new->if_name);
    }
}

/* Update the tunnel interface status in OVSDB. */
static bool vpnm_tunnel_iface_ovsdb_status_update(const char *if_name, enum vpnm_tunnel_iface_status new_status)
{
    struct schema_Tunnel_Interface schema_tun_iface;

    memset(&schema_tun_iface, 0, sizeof(schema_tun_iface));
    schema_tun_iface._partial_update = true;

    if (new_status == VPNM_TUNNEL_IFACE_STATUS_DISABLED)
    {
        STRSCPY(schema_tun_iface.status, SCHEMA_STR_STATUS_DISABLED);
    }
    else if (new_status == VPNM_TUNNEL_IFACE_STATUS_ENABLED)
    {
        STRSCPY(schema_tun_iface.status, SCHEMA_STR_STATUS_ENABLED);
    }
    else
    {
        STRSCPY(schema_tun_iface.status, SCHEMA_STR_STATUS_ERROR);
    }

    schema_tun_iface.status_exists = true;
    schema_tun_iface.status_present = true;

    if (!ovsdb_table_update_where(
            &table_Tunnel_Interface,
            ovsdb_where_simple(SCHEMA_COLUMN(Tunnel_Interface, if_name), if_name),
            &schema_tun_iface))
    {
        LOG(ERR, "vpnm_tunnel_iface: %s: Error updating Tunnel_Interface status", if_name);
        return false;
    }

    return true;
}


bool vpnm_tunnel_iface_init(void)
{
    LOG(INFO, "Initializing VPNM Tunnel_Interface monitoring.");

    OVSDB_TABLE_INIT(Tunnel_Interface, if_name);
    OVSDB_TABLE_MONITOR(Tunnel_Interface, false);

    return true;
}
