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

#ifndef NM2_NB_INTERFACE_H_INCLUDED
#define NM2_NB_INTERFACE_H_INCLUDED

#include "nm2.h"
#include "ovsdb_update.h"

/*
 * Interface config cached structure
 */
struct nm2_interface
{
    ovs_uuid_t          if_uuid;
    char                if_name[128 + 1];
    osn_mac_addr_t      if_mac_w;        /* mac address configured on Interface table */
    osn_mac_addr_t      if_mac_r;        /* mac address configured on the device */
    bool                if_mac_exists;
    osn_netif_t         *if_netif;
    reflink_t           if_reflink;
    ds_tree_node_t      if_tnode;
};

bool nm2_if_init(void);
ds_tree_t *nm2_if_get_list(void);
reflink_t *nm2_if_getref(const ovs_uuid_t *uuid);

void callback_Interface(ovsdb_update_monitor_t *mon, struct schema_Interface *old_rec,
                        struct schema_Interface *new_rec);

#endif /* NM2_NB_INTERFACE_H_INCLUDED */

