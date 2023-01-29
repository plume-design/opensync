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

#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <linux/kernel.h>
#include <sys/sysinfo.h>
#include <net/if.h>

#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "schema.h"
#include "log.h"
#include "ds.h"
#include "json_util.h"

#include "qosm_ip_iface.h"
#include "qosm_interface_classifier.h"
#include "qosm_ic_template.h"

ovsdb_table_t table_IP_Interface;
ovsdb_table_t table_Interface_Classifier;
ovsdb_table_t table_Openflow_Tag;
ovsdb_table_t table_Openflow_Local_Tag;
ovsdb_table_t table_Openflow_Tag_Group;


/**
 * @brief register ovsdb callback events for IP_Interface table
 *        updates
 */
void
callback_IP_Interface(ovsdb_update_monitor_t *mon,
                      struct schema_IP_Interface *old_rec,
                      struct schema_IP_Interface *new_rec)
{
    struct qosm_ip_iface *ipi;

    TRACE();

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            ipi = qosm_ip_iface_get(new_rec);
            break;

        case OVSDB_UPDATE_DEL:
            qosm_ip_iface_del(old_rec);
            return;

        default:
            LOGE("%s(): Invalid action received", __func__ );
            return;
    }

    if (ipi == NULL)
    {
        LOGE("%s(): failed to get IP_Interface object for %s", __func__, new_rec->if_name);
        return;
    }

    qosm_ip_iface_update_ic(ipi, new_rec);

    /* Schedule an update */
    qosm_ip_iface_start(ipi);
}


void
callback_Interface_Classifier(ovsdb_update_monitor_t *mon,
                              struct schema_Interface_Classifier *old_rec,
                              struct schema_Interface_Classifier *new_rec)
{
    struct qosm_intf_classifier *ic;

    TRACE();

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            ic = qosm_interface_classifier_get(new_rec);
            if (ic == NULL) return;
            reflink_ref(&ic->ic_reflink, 1);
            break;

        case OVSDB_UPDATE_MODIFY:
            ic = qosm_interface_classifier_get(new_rec);
            if (ic == NULL) return;
            break;

        case OVSDB_UPDATE_DEL:
            ic = qosm_interface_classifier_get(old_rec);
            if (ic == NULL) return;
            reflink_ref(&ic->ic_reflink, -1);
            return;

        default:
            LOGE("%s(): Error, invalid action", __func__);
            return;
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        qosm_interface_classifier_update(ic, new_rec);
    }

    /* Signal to listeners that the configuration may have changed */
    reflink_signal(&ic->ic_reflink);
    return;

}

/**
 * @brief registered callback for Openflow_Tag events
 */
void
callback_Openflow_Tag(ovsdb_update_monitor_t *mon,
                      struct schema_Openflow_Tag *old_rec,
                      struct schema_Openflow_Tag *tag)
{
    TRACE();

    if (mon->mon_type == OVSDB_UPDATE_NEW) {
        om_tag_add_from_schema(tag);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL) {
        om_tag_remove_from_schema(old_rec);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY) {
        om_tag_update_from_schema(tag);
    }
}

/**
 * @brief register ovsdb callback events
 */
int qosm_ovsdb_init(void)
{
    LOGI("Initializing QOSM tables");

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(Interface_Classifier);
    OVSDB_TABLE_INIT(IP_Interface, if_name);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Local_Tag);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag_Group);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(Interface_Classifier, false);
    OVSDB_TABLE_MONITOR_F(IP_Interface, C_VPACK("if_name", "ingress_classifier", "egress_classifier"));
    // tests call callback_Openflow_Tag directly, so we keep using it
    OVSDB_TABLE_MONITOR(Openflow_Tag, false);
    om_standard_callback_openflow_local_tag(&table_Openflow_Local_Tag);
    om_standard_callback_openflow_tag_group(&table_Openflow_Tag_Group);

    return 0;
}
