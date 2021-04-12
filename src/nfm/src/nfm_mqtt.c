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

#include "ovsdb_table.h"
#include "schema.h"

#include "nfm_mqtt.h"
#include "nfm_nflog.h"

char nfm_mqtt_topic[C_MAXPATH_LEN] = "";
char nfm_mqtt_node_id[C_MAXPATH_LEN] = "";
char nfm_mqtt_location_id[C_MAXPATH_LEN] = "";

static ovsdb_table_t table_AWLAN_Node;

static void callback_AWLAN_Node(
        ovsdb_update_monitor_t *mon,
        struct schema_AWLAN_Node *old,
        struct schema_AWLAN_Node *new);

/*
 * Initialize the MQTT subsystem -- subscribe to the AWLAN_Node table, which is
 * used to acquire the MQTT topic, location and node IDs
 */
bool nfm_mqtt_init(void)
{
    static char *filter[] =
    {
        SCHEMA_COLUMN(AWLAN_Node, mqtt_topics),
        SCHEMA_COLUMN(AWLAN_Node, mqtt_headers),
        NULL,
    };

    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
    OVSDB_TABLE_MONITOR_F(AWLAN_Node, filter);
    return true;
}

void nfm_mqtt_fini(void)
{
	/* Noop */
}

/*
 * Callback for the AWLAN_Node table updates
 */
void callback_AWLAN_Node(
        ovsdb_update_monitor_t *mon,
        struct schema_AWLAN_Node *old,
        struct schema_AWLAN_Node *new)
{
    (void)mon;
    (void)old;

    int mi;

    /*
     * In the delete case, new is empty; the logic below uses this to clear the
     * MQTT topic
     */
    nfm_mqtt_topic[0] = '\0';
    for (mi = 0; mi < new->mqtt_topics_len; mi++)
    {
        if (strcmp(new->mqtt_topics_keys[mi], "Netfilter.Nflog") == 0)
        {
            if (STRSCPY(nfm_mqtt_topic, new->mqtt_topics[mi]) < 0)
            {
                LOG(WARN, "nfm: MQTT topic name too long, please increase the buffer size.");
            }
            break;
        }
    }

    nfm_mqtt_node_id[0] = '\0';
    nfm_mqtt_location_id[0] = '\0';
    for (mi = 0; mi < new->mqtt_headers_len; mi++)
    {
        if (strcmp(new->mqtt_headers_keys[mi], "location_id") == 0)
        {
            if (STRSCPY(nfm_mqtt_topic, new->mqtt_topics[mi]) < 0)
            {
                LOG(WARN, "nfm: MQTT topic name too long, please increase the buffer size.");
            }
        }

        if (strcmp(new->mqtt_headers_keys[mi], "node_id") == 0)
        {
            if (STRSCPY(nfm_mqtt_topic, new->mqtt_topics[mi]) < 0)
            {
                LOG(WARN, "nfm: MQTT topic name too long, please increase the buffer size.");
            }
        }
    }

    LOG(INFO, "nfm: MQTT topic set: %s", nfm_mqtt_topic[0] == '\0' ? "(empty)" : nfm_mqtt_topic);
    LOG(INFO, "nfm: MQTT location_id set: %s", nfm_mqtt_location_id[0] == '\0' ? "(empty)" : nfm_mqtt_location_id);
    LOG(INFO, "nfm: MQTT node_id set: %s", nfm_mqtt_node_id[0] == '\0' ? "(empty)" : nfm_mqtt_node_id);

    /* Start/stop NFLOG monitoring */
    if (nfm_mqtt_topic[0] != '\0')
    {
        nfm_nflog_start();
    }
    else
    {
        nfm_nflog_stop();
    }
}
