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

#include <const.h>
#include <log.h>
#include <memutil.h>
#include <ovsdb_table.h>
#include <ovsdb_update.h>

#include <osw_module.h>
#include <ow_steer_bm.h>
#include "ow_steer_bm_mlo.h"
#include "ow_ovsdb_steer_bm_mlo.h"
#include "schema.h"

#define LOG_PREFIX(fmt, ...) "ow: ovsdb: bm: mlo: " fmt, ##__VA_ARGS__

#define OW_OVSDB_STEER_BM_MLO_AWLAN_MQTT_TOPIC_KEY "BSReportV2"

struct ow_ovsdb_steer_bm_mlo
{
    ow_steer_bm_mlo_t *m_mlo;
    ovsdb_table_t table_AWLAN_Node;
};

static const char *ow_ovsdb_steer_bm_mlo_awlan_get_mqtt_topic(const struct schema_AWLAN_Node *row)
{
    return SCHEMA_KEY_VAL_NULL(row->mqtt_topics, OW_OVSDB_STEER_BM_MLO_AWLAN_MQTT_TOPIC_KEY);
}

static void ow_ovsdb_steer_bm_mlo_awlan_node_cb(
        ovsdb_update_monitor_t *mon,
        const struct schema_AWLAN_Node *old,
        const struct schema_AWLAN_Node *anode)
{
    ovsdb_table_t *table = mon->mon_data;
    ow_ovsdb_steer_bm_mlo_t *m = container_of(table, typeof(*m), table_AWLAN_Node);

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            ow_steer_bm_mlo_set_topic(m->m_mlo, ow_ovsdb_steer_bm_mlo_awlan_get_mqtt_topic(anode));
            break;
        case OVSDB_UPDATE_MODIFY:
            if (ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, mqtt_topics)))
                ow_steer_bm_mlo_set_topic(m->m_mlo, ow_ovsdb_steer_bm_mlo_awlan_get_mqtt_topic(anode));
            break;
        case OVSDB_UPDATE_DEL:
            ow_steer_bm_mlo_set_topic(m->m_mlo, NULL);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

static void ow_ovsdb_steer_bm_mlo_init(ow_ovsdb_steer_bm_mlo_t *m)
{
    m->m_mlo = OSW_MODULE_LOAD(ow_steer_bm_mlo);
    OVSDB_TABLE_VAR_INIT(&m->table_AWLAN_Node, AWLAN_Node, _uuid);
}

void ow_ovsdb_steer_bm_mlo_start(ow_ovsdb_steer_bm_mlo_t *m)
{
    ovsdb_table_monitor(&m->table_AWLAN_Node, (void *)ow_ovsdb_steer_bm_mlo_awlan_node_cb, true);
}

OSW_MODULE(ow_ovsdb_steer_bm_mlo)
{
    static struct ow_ovsdb_steer_bm_mlo m;
    ow_ovsdb_steer_bm_mlo_init(&m);
    return &m;
}
