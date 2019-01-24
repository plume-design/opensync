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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "ds_tree.h"
#include "log.h"
#include "schema.h"
#include "json_util.h"
#include "ovsdb_update.h"
#include "target.h"


#define MODULE_ID LOG_MODULE_ID_MAIN


static ovsdb_update_monitor_t   portfw_monitor;
static ovsdb_update_cbk_t       portfw_monitor_fn;


bool nm2_fw_init(void)
{
    bool rc;

    /* Register for OVS IP_Port_Forward table monitor updates */
    rc = ovsdb_update_monitor(
            &portfw_monitor,
            portfw_monitor_fn,
            SCHEMA_TABLE(IP_Port_Forward),
            OMT_ALL);
    if (!rc)
    {
        LOG(ERR, "FW: Error initializing %s monitor", SCHEMA_TABLE(IP_Port_Forward));
        return false;
    }

    return true;
}


/*
 * ===========================================================================
 *  Port Forwarding
 * ===========================================================================
 */
void portfw_monitor_fn(ovsdb_update_monitor_t *self)
{
    struct schema_IP_Port_Forward schema_port_fw;
    pjs_errmsg_t perr;

    memset(&schema_port_fw, 0, sizeof(schema_port_fw));

    switch (self->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            LOG(DEBUG, "NEW: %s: %s", SCHEMA_TABLE(IP_Port_Forward),
                       json_dumps_static(self->mon_json_new, 0));

            if (!schema_IP_Port_Forward_from_json(&schema_port_fw,
                                               self->mon_json_new, false, perr))
            {
                LOG(ERR, "NEW: %s row: Error parsing: %s",
                         SCHEMA_TABLE(IP_Port_Forward), perr);
                return;
            }

            target_portforward_set(schema_port_fw.src_ifname, &schema_port_fw);
            break;

        case OVSDB_UPDATE_MODIFY:
            LOG(DEBUG, "MODIFY: %s: %s", SCHEMA_TABLE(IP_Port_Forward),
                        json_dumps_static(self->mon_json_new, 0));

            if (!schema_IP_Port_Forward_from_json(&schema_port_fw,
                                                self->mon_json_new, true, perr))
            {
                LOG(ERR, "MODIFY: %s: Error parsing: %s",
                         SCHEMA_TABLE(IP_Port_Forward), perr);
                return;
            }

            target_portforward_set(schema_port_fw.src_ifname, &schema_port_fw);
            break;

        case OVSDB_UPDATE_DEL:
            LOG(DEBUG, "DELETE: %s: %s", SCHEMA_TABLE(IP_Port_Forward),
                       json_dumps_static(self->mon_json_old, 0));

            if (!schema_IP_Port_Forward_from_json(&schema_port_fw,
                                               self->mon_json_old, false, perr))
            {
                LOG(ERR, "DELETE: %s: Error parsing: %s",
                         SCHEMA_TABLE(IP_Port_Forward), perr);
                return;
            }

            target_portforward_del(schema_port_fw.src_ifname, &schema_port_fw);
            break;

        default:
            LOG(ERR, "ERROR: %s: Unhandled update notification type %d for UUID %s",
                     SCHEMA_TABLE(IP_Port_Forward), self->mon_type, self->mon_uuid);
            return;
    }

}

