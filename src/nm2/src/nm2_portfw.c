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

/* * ===========================================================================
 *  Port Forwarding
 * ===========================================================================
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "ds_tree.h"
#include "log.h"
#include "schema.h"
#include "json_util.h"
#include "ovsdb_update.h"
#include "inet.h"
#include "nm2.h"
#include "memutil.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

#define IS_PORT_VALID(port)  ((port) > 0 && (port) <= 65535)

static ovsdb_update_monitor_t nm2_portfw_monitor;
static ovsdb_update_cbk_t nm2_portfw_monitor_fn;

/*
 * Port forwarding cached entry
 */
struct portfw_entry
{
    ovs_uuid_t                  pfw_uuid;           /* UUID of OVS Row */
    struct inet_portforward     pfw_data;           /* Port forwarding structure */
    ds_tree_node_t              pfw_tnode;          /* RB-Tree node */
};

/* Port forwarding cache list */
static ds_tree_t portfw_list = DS_TREE_INIT(ds_str_cmp, struct portfw_entry, pfw_tnode);

static bool nm2_portfw_add(struct schema_IP_Port_Forward *pschema);
static bool nm2_portfw_del(struct schema_IP_Port_Forward *pschema);

static bool nm2_inet_portforward_from_schema(
        struct inet_portforward *pportfw,
        struct schema_IP_Port_Forward *pschema);

bool nm2_portfw_init(void)
{
    /* Register for OVS IP_Port_Forward table monitor updates */
    if (!ovsdb_update_monitor(
            &nm2_portfw_monitor,
            nm2_portfw_monitor_fn,
            SCHEMA_TABLE(IP_Port_Forward),
            OMT_ALL))
    {
        LOG(ERR, "portfw: Error initializing %s monitor", SCHEMA_TABLE(IP_Port_Forward));
        return false;
    }

    return true;
}

/*
 * OVSDB monitoring functon for the IP_Port_Forward table
 */
void nm2_portfw_monitor_fn(ovsdb_update_monitor_t *self)
{
    struct schema_IP_Port_Forward schema_port_fw;
    pjs_errmsg_t perr;

    memset(&schema_port_fw, 0, sizeof(schema_port_fw));

    switch (self->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            LOG(DEBUG, "portfw: new/modify(%d) %s: %s",
                    self->mon_type == OVSDB_UPDATE_NEW,
                    SCHEMA_TABLE(IP_Port_Forward),
                    json_dumps_static(self->mon_json_new, 0));

            if (!schema_IP_Port_Forward_from_json(&schema_port_fw,
                                               self->mon_json_new, false, perr))
            {
                LOG(ERR, "portfw: %s row set: Error parsing: %s",
                         SCHEMA_TABLE(IP_Port_Forward), perr);
                return;
            }

            nm2_portfw_add(&schema_port_fw);
            break;

        case OVSDB_UPDATE_DEL:
            LOG(DEBUG, "portfw: delete %s: %s",
                    SCHEMA_TABLE(IP_Port_Forward),
                    json_dumps_static(self->mon_json_old, 0));

            if (!schema_IP_Port_Forward_from_json(&schema_port_fw,
                                               self->mon_json_old, false, perr))
            {
                LOG(ERR, "portfw: %s row delete: Error parsing: %s",
                         SCHEMA_TABLE(IP_Port_Forward), perr);
                return;
            }

            nm2_portfw_del(&schema_port_fw);
            break;

        default:
            LOG(ERR, "portfw: %s: Unhandled update notification type %d for UUID %s",
                     SCHEMA_TABLE(IP_Port_Forward),
                     self->mon_type,
                     self->mon_uuid);
            return;
    }

}

/*
 * Push port forwarding configuration from the schema structure to libinet.
 *
 * The interface *MUST* exists before populating the port forwarding schema as
 * we're unable to infer the interface type from the port forwarding table.
 *
 * If an entry with the same UUID already exists in the cache, the old entry
 * is removed before the new entry is added.
 */
bool nm2_portfw_add(struct schema_IP_Port_Forward *pschema)
{
    struct portfw_entry *pfw;
    struct nm2_iface *piface;

    piface = nm2_iface_get_by_name(pschema->src_ifname);
    if (piface == NULL)
    {
        LOG(ERR, "portfw: Unable to add port forwarding entry. Interface %s doesn't exist.",
                pschema->src_ifname);
        return false;
    }

    pfw = ds_tree_find(&portfw_list, pschema->_uuid.uuid);
    if (pfw == NULL)
    {
        /* Allocate new entry and insert it into the cache */
        pfw = CALLOC(1, sizeof(struct portfw_entry));
        STRSCPY(pfw->pfw_uuid.uuid, pschema->_uuid.uuid);
        ds_tree_insert(&portfw_list, pfw, pfw->pfw_uuid.uuid);
    }
    else
    {
        /* Entry already exists, remove configuration from interface */
        if (!inet_portforward_del(piface->if_inet, &pfw->pfw_data))
        {
            LOG(ERR, "portfw: Unable to update port forwarding entry "PRI(inet_portforward),
                    FMT(inet_portforward, pfw->pfw_data));
            return false;
        }
    }

    if (!nm2_inet_portforward_from_schema(&pfw->pfw_data, pschema))
    {
        LOG(ERR, "portfw: Unable to parse schema (add).");
        return false;
    }

    /* Push configuration to the interface */
    if (!inet_portforward_set(piface->if_inet, &pfw->pfw_data))
    {
        LOG(ERR, "portfw: Unable add port forwarding entry "PRI(inet_portforward),
                FMT(inet_portforward, pfw->pfw_data));
        return false;
    }

    /* Schedule a configuration apply */
    nm2_iface_apply(piface);

    return true;
}

/*
 * Delete port forwarding entry, this is the counterpart to nm2_portfw_set().
 */
bool nm2_portfw_del(struct schema_IP_Port_Forward *pschema)
{
    struct portfw_entry *pfw;
    struct nm2_iface *piface;

    piface = nm2_iface_get_by_name(pschema->src_ifname);
    if (piface == NULL)
    {
        LOG(ERR, "portfw: Unable to delete port forwarding entry. Interface %s doesn't exist.",
                pschema->src_ifname);
        return false;
    }

    pfw = ds_tree_find(&portfw_list, pschema->_uuid.uuid);
    if (pfw == NULL)
    {
        LOG(ERR, "portfw: Trying to delete non-existent port forwarding entry with UUID %s.", pschema->_uuid.uuid);
        return false;
    }

    /* Push configuration to the interface */
    if (!inet_portforward_del(piface->if_inet, &pfw->pfw_data))
    {
        LOG(ERR, "portfw: Unable delete port forwarding entry "PRI(inet_portforward),
                FMT(inet_portforward, pfw->pfw_data));
        return false;
    }

    /* Remove from cache */
    ds_tree_remove(&portfw_list, pfw);
    FREE(pfw);

    /* Schedule a configuration apply */
    nm2_iface_apply(piface);

    return true;
}

/* Utility function from converting a schema structure to struct inet_portforward */
bool nm2_inet_portforward_from_schema(
        struct inet_portforward *pportfw,
        struct schema_IP_Port_Forward *pschema)
{
    *pportfw = INET_PORTFORWARD_INIT;

    if (!osn_ip_addr_from_str(&pportfw->pf_dst_ipaddr, pschema->dst_ipaddr))
    {
        LOG(DEBUG, "portfw: Unable to parse IP address from schema: %s",
                pschema->dst_ipaddr);
        return false;
    }

    if (!IS_PORT_VALID(pschema->dst_port) || !IS_PORT_VALID(pschema->src_port))
    {
        LOG(DEBUG, "portfw: Invalid src(%d)/dst(%d) port.",
                !IS_PORT_VALID(pschema->dst_port),
                !IS_PORT_VALID(pschema->src_port));
        return false;
    }

    pportfw->pf_dst_port = (uint16_t)pschema->dst_port;
    pportfw->pf_src_port = (uint16_t)pschema->src_port;
    if (pschema->nat_loopback_exists)
    {
        pportfw->pf_nat_loopback = pschema->nat_loopback;
    }

    if (strcmp("udp", pschema->protocol) == 0)
    {
        pportfw->pf_proto = INET_PROTO_UDP;
    }
    else if (strcmp("tcp", pschema->protocol) == 0)
    {
        pportfw->pf_proto = INET_PROTO_TCP;
    }
    else
    {
        LOG(DEBUG, "portfw: Invalid protocol: %s", pschema->protocol);
        return false;
    }

    return true;
}
