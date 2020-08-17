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

#ifndef OVS_MAC_LEARN_H_INCLUDED
#define OVS_MAC_LEARN_H_INCLUDED
#include <stdio.h>

#include "ds_tree.h"
#include "const.h"
#include "schema.h"
#include "ovsdb_update.h"
#include "target.h"

/*
 * Per-interface MAC addresses cache. The key for this structure is the combination of
 * bridge name, interface name and MAC address
 */
#define OVSMAC_FLAG_ACTIVE      (1 << 1)
#define FIELD_ARRAY_LEN(TYPE,FIELD) ARRAY_LEN(((TYPE*)0)->FIELD)

struct ovsmac_node
{
    struct schema_OVS_MAC_Learning  mac;
    int                             mac_flags;
    ds_tree_node_t                  mac_node;
};

/*
 * Reduced structs. To save memory use reduced custom struct instead of complete schema struct.
 * Custom reduced struct only has information that is needed for ovs mac learning to work.
 */
struct reduced_bridge
{
    char name[C_IFNAME_LEN];
    ovs_uuid_t _uuid;
    ovs_uuid_t ports[FIELD_ARRAY_LEN(struct schema_Bridge, ports)];
    int ports_len;
};

struct reduced_port
{
    char name[C_IFNAME_LEN];
    ovs_uuid_t _uuid;
    ovs_uuid_t interfaces[FIELD_ARRAY_LEN(struct schema_Port, interfaces)];
    int interfaces_len;
};

struct reduced_interface
{
    char name[C_IFNAME_LEN];
    ovs_uuid_t _uuid;
    int ofport;
    _Bool ofport_exists;
};

/*
 * Bridge cache list
 */
struct bridge_node
{
    struct reduced_bridge       br_bridge;
    ds_tree_node_t              br_node;
};

/*
 * Port cache list
 */
struct port_node
{
    struct reduced_port         pr_port;
    ds_tree_node_t              pr_node;
};

/*
 * Interface cache
 */
struct iface_node
{
    struct reduced_interface    if_iface;
    ds_tree_node_t              if_node;
};

struct bridge_flt_node
{
    char                        bridge[128];
    ds_tree_node_t              bridge_node;
};


struct iface_flt_node
{
    char                        if_iface[128];
    ds_tree_node_t              iface_node;
};

bool ovs_mac_learning_register(target_mac_learning_cb_t *omac_cb);

#endif /* OVS_MAC_LEARN_H_INCLUDED */
