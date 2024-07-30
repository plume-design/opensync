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

#ifndef NM2_NB_BRIDGE_H_INCLUDED
#define NM2_NB_BRIDGE_H_INCLUDED

#include "nm2.h"
#include "ovsdb_update.h"

/*
 * Bridge config cached structure
 */
struct nm2_bridge
{
    ovs_uuid_t          br_uuid;
    inet_t*             br_inet;            /* Inet structure */
    char                br_name[128 + 1];
    reflink_t           br_reflink;
    uuidset_t           br_ports;
    ds_tree_node_t      br_tnode;
};

void callback_Bridge(ovsdb_update_monitor_t *mon, struct schema_Bridge *old_rec,
                     struct schema_Bridge *conf);
ds_tree_t *nm2_bridge_get_list(void);
bool nm2_default_br_create_tables(char *br_name);
void nm2_default_br_init(char *br_name);
void nm2_bridge_init(void);
void nm2_open_vswitch_init(void);

#endif /* NM2_NB_BRIDGE_H_INCLUDED */
