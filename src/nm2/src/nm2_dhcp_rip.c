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

/* DHCP reserved IP handling.  */

#include "ds_tree.h"
#include "log.h"
#include "schema.h"
#include "json_util.h"
#include "ovsdb_update.h"
#include "target.h"


#define MODULE_ID LOG_MODULE_ID_MAIN


static ovsdb_update_monitor_t dhcp_rip_monitor;
static ovsdb_update_cbk_t nm2_dhcp_rip_update_fn;


bool nm2_dhcp_rip_init(void)
{
    bool rc;

    /* Register for OVS monitor updates. */
    rc = ovsdb_update_monitor(
            &dhcp_rip_monitor,
            nm2_dhcp_rip_update_fn,
            SCHEMA_TABLE(DHCP_reserved_IP),
            OMT_ALL);
    if (!rc)
    {
        LOG(ERR, "Error initialzing DHCP_reserved_IP monitor.");
        return false;
    }
    return true;
}


/* Update Monitor callback for the DHCP_reserved_IP table.  */
void nm2_dhcp_rip_update_fn(ovsdb_update_monitor_t *self)
{
    struct schema_DHCP_reserved_IP schema_rip;
    pjs_errmsg_t perr;

    memset(&schema_rip, 0, sizeof(schema_rip));

    switch (self->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            LOG(DEBUG, "DHCP_reserved_IP NEW: %s", json_dumps_static(self->mon_json_new, 0));

            if (!schema_DHCP_reserved_IP_from_json(&schema_rip, self->mon_json_new, false, perr))
            {
                LOG(ERR, "NEW: DHCP_reserved_IP row: Error parsing: %s", perr);
                return;
            }

            target_dhcp_rip_set(NULL, &schema_rip);
            break;

        case OVSDB_UPDATE_MODIFY:
            LOG(DEBUG, "DHCP_reserved_IP MODIFY: %s", json_dumps_static(self->mon_json_new, 0));

            if (!schema_DHCP_reserved_IP_from_json(&schema_rip, self->mon_json_new, true, perr))
            {
                LOG(ERR, "MODIFY: DHCP_reserved_IP: Error parsing: %s", perr);
                return;
            }

            target_dhcp_rip_set(NULL, &schema_rip);
            break;

        case OVSDB_UPDATE_DEL:
            LOG(DEBUG, "DHCP_reserved_IP DELETE: %s", json_dumps_static(self->mon_json_new, 0));

            if (!schema_DHCP_reserved_IP_from_json(&schema_rip, self->mon_json_old, false, perr))
            {
                LOG(ERR, "DELETE: DHCP_reserved_IP: Error parsing: %s", perr);
                return;
            }

            target_dhcp_rip_del(NULL, &schema_rip);
            break;

        default:
            LOG(ERR, "ERROOR: DHCP_reserved_IP: Unhandled update notification "
                     "type %d for UUID %s.", self->mon_type, self->mon_uuid);
            return;
    }

}

