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

#include "log.h"
#include "util.h"
#include "ovsdb_table.h"
#include "os_util.h"
#include "reflink.h"
#include "synclist.h"
#include "ovsdb_sync.h"
#include "osp_l2switch.h"

#include "nm2.h"

#define ETH_PREFIX "eth"
#define ETH_PREFIX_LEN 3


/*
 * ===========================================================================
 *  Port table
 * ===========================================================================
 */
static ovsdb_table_t    table_Port;
static void             callback_Port(ovsdb_update_monitor_t *mon,
                                      struct schema_Port *old,
                                      struct schema_Port *new);
void                    nm2_add_vlans(struct schema_Port *rec);
void                    nm2_remove_vlans(struct schema_Port *oldrec,
                                         struct schema_Port *newrec);
void                    nm2_mod_vlans(struct schema_Port *oldrec,
                                      struct schema_Port *newrec);
/*
 * Initialize table monitors
 */
bool nm2_port_init(void)
{
    if (!osp_l2switch_init())
        return true;

    LOG(INFO, "Initializing NM Port monitoring.");
    OVSDB_TABLE_INIT_NO_KEY(Port);
    OVSDB_TABLE_MONITOR(Port,  false);

    return true;
}

void nm2_add_vlans(struct schema_Port *newrec)
{
    bool    native_mode = false;
    int     idx;

    LOG(DEBUG, "Configuring VLANs on port[%s]", newrec->name);
    if (!osp_l2switch_new(newrec->name))
        return;

    for (idx = 0; idx < newrec->trunks_len; idx++)
    {
        if (!osp_l2switch_vlan_set(newrec->name, newrec->trunks[idx], true))
        {
            LOG(ERR, "Failed to configure vlans for port[%s]", newrec->name);
            return;
        }
    }


    if (newrec->tag_exists)
    {
        if (newrec->vlan_mode_exists &&
            !strcmp(newrec->vlan_mode, "native-tagged"))
            native_mode = true;
        if (!osp_l2switch_vlan_set(newrec->name, newrec->tag, native_mode))
        {
            LOG(ERR, "Failed to add access VLANs in switch for port[%s].", newrec->name);
            return;
        }
    }

    return;
}

void nm2_remove_vlans(struct schema_Port *oldrec, struct schema_Port *newrec)
{
    int     idx;
    char   *ptr;


    if (newrec)
    {
        ptr = newrec->name;
    } else {
        ptr = oldrec->name;
    }

    LOG(DEBUG, "Removing VLANs in port[%s]", ptr);

    for (idx = 0; idx < oldrec->trunks_len; idx++)
    {
        if (!osp_l2switch_vlan_unset(ptr, oldrec->trunks[idx]))
        {
            LOG(DEBUG, "Failed to remove trunk VLANs from switch for port[%s].", ptr);
            return;
        }
    }

    if (oldrec->tag_exists)
    {
        if (!osp_l2switch_vlan_unset(ptr, oldrec->tag))
        {
            LOG(DEBUG, "Failed to remove access VLANs from switch for port[%s].", ptr);
            return;
        }
    }

    return;
}

void nm2_mod_vlans(struct schema_Port *oldrec, struct schema_Port *newrec)
{
    nm2_remove_vlans(oldrec, newrec);
    nm2_add_vlans(newrec);
    return;
}

/*
 * OVSDB monitor update callback for Port
 */
void callback_Port(
        ovsdb_update_monitor_t *mon,
        struct schema_Port *old,
        struct schema_Port *new)
{

    switch (mon->mon_type) {
        case OVSDB_UPDATE_NEW:
            nm2_add_vlans(new);
            break;
        case OVSDB_UPDATE_MODIFY:
            nm2_remove_vlans(old, new);
            nm2_add_vlans(new);
            break;
        case OVSDB_UPDATE_DEL:
            nm2_remove_vlans(old, NULL);
            break;
        default:
            LOGW("%s:mon upd error: %d", __func__, mon->mon_type);
            return;
    }

    if (mon->mon_type == OVSDB_UPDATE_NEW ||
        mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        if (!osp_l2switch_apply(new->name))
        {
            LOG(DEBUG, "Failed to apply vlans to port[%s]",new->name);
            return;
        }
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        if (!osp_l2switch_apply(old->name))
        {
            LOG(DEBUG, "Failed to apply vlans to port[%s]",old->name);
            return;
        }
        osp_l2switch_del(old->name);
    }
    return;
}
