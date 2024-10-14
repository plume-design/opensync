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

#ifndef NM2_NB_PORT_H_INCLUDED
#define NM2_NB_PORT_H_INCLUDED

#include "nm2.h"
#include "ovsdb_update.h"

/*
 * Port config cached structure
 */
struct nm2_port
{
    ovs_uuid_t          port_uuid;
    reflink_t           port_reflink;
    bool                port_valid;
    char                port_name[128 + 1];
    bool                port_hairpin;
    inet_t*             port_inet;
    struct nm2_bridge   *port_bridge;   /* ptr to br, this port is connected */
    ds_tree_node_t      port_tnode;
    uuidset_t           port_interfaces;
};

void nm2_nb_port_process_update(ovsdb_update_monitor_t *mon, struct schema_Port *old,
                                struct schema_Port *new);
void nm2_add_port_to_br(struct nm2_bridge *bridge, struct nm2_port *port, bool add);
void nm2_nb_port_cfg_reapply(const char *port_name);
ds_tree_t *nm2_port_get_list(void);
reflink_t *nm2_ports_getref(const ovs_uuid_t *uuid);
void callback_Port(ovsdb_update_monitor_t *mon, struct schema_Port *old,
                   struct schema_Port *new);
void nm2_inet_bridge_config_reapply(struct nm2_iface *pif);

#endif /* NM2_NB_PORT_H_INCLUDED */
