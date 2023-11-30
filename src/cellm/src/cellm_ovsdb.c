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

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "log.h"
#include "os.h"
#include "qm_conn.h"
#include "dppline.h"
#include "network_metadata.h"
#include "cell_info.h"
#include "cellm_mgr.h"

/* Log entries from this file will contain "OVSDB" */
#define MODULE_ID LOG_MODULE_ID_OVSDB


#define CELL_NODE_MODULE "cell"

ovsdb_table_t table_AWLAN_Node;

void
cellm_ovsdb_update_awlan_node(struct schema_AWLAN_Node *new)
{
    int i;
    cellm_mgr_t *mgr = cellm_get_mgr();

    for (i = 0; i < new->mqtt_headers_len; i++)
    {
        if (strcmp(new->mqtt_headers_keys[i], "nodeId") == 0)
        {
            STRSCPY(mgr->node_id, new->mqtt_headers[i]);
            LOGI("%s: new node_id[%s]", __func__, mgr->node_id);
        }
        else if (strcmp(new->mqtt_headers_keys[i], "locationId") == 0)
        {
            STRSCPY(mgr->location_id, new->mqtt_headers[i]);
            LOGI("%s: new location_id[%s]", __func__, mgr->location_id);
        }
    }

    for (i = 0; i < new->mqtt_topics_len; i++)
    {
        if (strcmp(new->mqtt_topics_keys[i], "CellStats") == 0)
        {
            STRSCPY(mgr->topic, new->mqtt_topics[i]);
            LOGI("%s: new topic[%s]", __func__, mgr->topic);
        }
    }
}

void callback_AWLAN_Node(
        ovsdb_update_monitor_t *mon,
        struct schema_AWLAN_Node *old,
        struct schema_AWLAN_Node *new)
{
    switch (mon->mon_type) {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            break;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            cellm_ovsdb_update_awlan_node(new);
            break;
    }

}

int
cellm_ovsdb_init(void)
{
    LOGI("Initializing CELLM OVSDB tables");

    static char *filter[] =
    {
        SCHEMA_COLUMN(AWLAN_Node, mqtt_headers),
        SCHEMA_COLUMN(AWLAN_Node, mqtt_topics),
        NULL,
    };

    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
    OVSDB_TABLE_MONITOR_F(AWLAN_Node, filter);

    return 0;
}
