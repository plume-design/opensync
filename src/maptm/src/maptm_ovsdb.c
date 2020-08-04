/*
* Copyright (c) 2020, Sagemcom.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* 3. Neither the name of the copyright holder nor the names of its contributors
*    may be used to endorse or promote products derived from this software
*    without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>

#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"

#include "maptm.h"

#ifdef MAPTM_DEBUG
#undef LOGI
#define LOGI    printf
#endif

/**
 * Globals
 */

/* Log entries from this file will contain "OVSDB" */
#define MODULE_ID LOG_MODULE_ID_OVSDB
#define MAPTM_IFC_WAN "br-wan"

ovsdb_table_t table_Netfilter;
ovsdb_table_t table_Node_Config;
struct ovsdb_table table_Node_State;
static ovsdb_table_t table_Interface;
static ovsdb_table_t  table_IP_Interface;
ovsdb_table_t  table_Wifi_Inet_Config;
ovsdb_table_t  table_DHCPv6_Client;
ovsdb_table_t  table_DHCP_Client;
ovsdb_table_t  table_IPv6_Address;

/* To change to Enum  */
int WanConfig = 0;

// Update if board is MAP-T eligible
bool maptm_update_mapt(bool enable)
{
    int rc = 0;
    json_t *where = NULL;
    struct schema_Node_Config rec_config;

    memset(&rec_config, 0, sizeof(rec_config));
    rec_config._partial_update = true;

    where = ovsdb_where_multi(
        ovsdb_where_simple_typed(SCHEMA_COLUMN(Node_Config, module), "MAPTM", OCLM_STR),
        ovsdb_where_simple_typed(SCHEMA_COLUMN(Node_Config, key), "maptParams", OCLM_STR),
        NULL);

    if (!where)
    {
        LOGE("Could not get maptParams value in Node_Config table");
        goto exit;
    }

    if (enable)
    {
        SCHEMA_SET_STR(rec_config.value, "{\"support\":\"true\",\"interface\":\"br-wan\"}");
    }
    else
    {
        SCHEMA_SET_STR(rec_config.value, "{\"support\":\"false\",\"interface\":\"br-wan\"}");
    }
    rec_config.value_exists = true;

    rc = ovsdb_table_update_where(&table_Node_Config, where, &rec_config);

    if (rc != 1 )
    {
        LOGE("%s: Could not update Node_Config table", __func__);
        goto exit;
    }

    LOGD("%s: Update Node_Config table", __func__);
    return true;

exit:
    return false;
}

// Make MAPT_Support parameter persistent
bool maptm_persistent(void)
{
    bool ret = false;
    char mapt_support[10];
    osp_ps_t *ps = NULL;

    ps = osp_ps_open("MAPT_SUPPORT", OSP_PS_RDWR | OSP_PS_PRESERVE);
    if (ps == NULL)
    {
        LOG(ERR, "maptm: Error opening \"%s\" persistent store.", mapt_support);
        return false;
    }

    if (!(osp_ps_get(ps, "MAPT_SUPPORT", mapt_support, sizeof(mapt_support))))
    {
        LOGE("%s: Cannot get MAPT_SUPPORT value", __func__);
        return false;
    }

    snprintf(mapt_support, sizeof(mapt_support), "%s", "true");
    if (!(osp_ps_set(ps, "MAPT_SUPPORT", mapt_support, sizeof("MAPT_SUPPORT"))))
    {
        LOGE("%s: Cannot save MAPT_SUPPORT through osp API", __func__);
        return false;
    }
    LOGT("%s: MAPT_SUPPORT = %s", __func__, mapt_support);

    if (!strcmp(mapt_support, "true"))
    {
        ret = maptm_update_mapt(true);
    }
    else
    {
        ret = maptm_update_mapt(false);
    }

    return ret;
}

// Get MAPT_Support value
bool maptm_get_supportValue(char *value)
{
    char str[64];
    const char delim[] = ":";

    STRSCPY(str, value);

    char *ptr = strtok(str, delim);

    while (ptr != NULL)
    {
        if (!strcmp(ptr, "\"true\""))
        {
            return true;
        }
        else if (!strcmp(ptr, "\"false\""))
        {
            return false;
        }
        ptr = strtok(NULL, ",");
    }

    return false;
}

// Node_Config callback
void callback_Node_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Node_Config *old_rec,
        struct schema_Node_Config *conf
)
{
    if ((mon->mon_type) == OVSDB_UPDATE_NEW)
    {
        LOGD("%s: new node config entry: module %s, key: %s, value: %s",
                __func__, conf->module, conf->key, conf->value);

        if (!strcmp(conf->module, "MAPTM"))
        {
            if (maptm_get_supportValue(conf->value))
            {
                WanConfig |= MAPTM_ELIGIBILITY_ENABLE;
            }
            strucWanConfig.mapt_support = maptm_get_supportValue(conf->value);
            maptm_dhcp_option_update_15_option(strucWanConfig.mapt_support);
            maptm_dhcp_option_update_95_option(strucWanConfig.mapt_support);
        }
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        LOGD("%s: node config entry deleted: module %s, key: %s, value: %s",
                __func__, old_rec->module, old_rec->key, old_rec->value);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        LOGD("%s: node config entry updated: \n"
                "old module: %s, old key: %s, old value: %s \n"
                "new module: %s, new key: %s, new value: %s",
                __func__, old_rec->module, old_rec->key, old_rec->value,
                conf->module, conf->key, conf->value);

        if (!strcmp(conf->module, "MAPTM"))
        {
            strucWanConfig.mapt_support = maptm_get_supportValue(conf->value);
            if (maptm_get_supportValue(conf->value) != maptm_get_supportValue(old_rec->value))
            {
                maptm_dhcp_option_update_15_option(strucWanConfig.mapt_support);
                maptm_dhcp_option_update_95_option(strucWanConfig.mapt_support);
            }

            if ((maptm_get_supportValue(conf->value)) && !maptm_get_supportValue(old_rec->value))
            {
                WanConfig |= MAPTM_ELIGIBILITY_ENABLE;
            }
            else if (!maptm_get_supportValue(conf->value) && (maptm_get_supportValue(old_rec->value)))
            {
                WanConfig &= MAPTM_IPV6_ENABLE;
            }

            if (maptm_get_supportValue(conf->value) != maptm_get_supportValue(old_rec->value))
            {
                maptm_eligibilityStart(WanConfig);
                osp_ps_t *ps = NULL;
                ps = osp_ps_open("MAPT_SUPPORT", OSP_PS_RDWR | OSP_PS_PRESERVE);
                if (ps == NULL)
                {
                    LOG(ERR, "Error saving new MAP-T support value");
                }
                if (!(osp_ps_set(ps, "MAPT_SUPPORT", maptm_get_supportValue(conf->value) ? "true" : "false",sizeof(maptm_get_supportValue(conf->value)))))
                {
                    LOGE("Error saving new MAP-T support value");
                }
            }
        }
    }
}

// Interface callback
static void callback_Interface(
        ovsdb_update_monitor_t *mon,
        struct schema_Interface *old,
        struct schema_Interface *record
)
{
    LOGA("Starting callback_Interface %s name ", record->name);
    if (!mon || !record)
    {
        LOGE("Interface OVSDB event: invalid parameters");
        return;
    }

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            break;

        case OVSDB_UPDATE_DEL:
            break;

        case OVSDB_UPDATE_MODIFY:
            if (!strcmp(record->name, "br-wan"))
            {
                if (!strcmp(record->link_state, "up"))
                {
                     strucWanConfig.link_up = true;
                     if ((record->link_state) != (old->link_state)) maptm_eligibilityStart(WanConfig);
                }
                else if (!strcmp(record->link_state, "down"))
                {
                    strucWanConfig.link_up = false;
                    maptm_eligibilityStop();
                }
            }
            break;

    default:
        LOGE("Netfilter OVSDB event: unknown type %d", mon->mon_type);
        break;
    }
}

// Initialize MAP-T Ovsdb
int maptm_ovsdb_init(void)
{
    OVSDB_TABLE_INIT_NO_KEY(Interface);
    OVSDB_TABLE_INIT(IP_Interface, status);
    OVSDB_TABLE_INIT_NO_KEY(Node_Config);
    OVSDB_TABLE_INIT_NO_KEY(Node_State);
    OVSDB_TABLE_INIT_NO_KEY(Wifi_Inet_Config);
    OVSDB_TABLE_INIT_NO_KEY(DHCPv6_Client);
    OVSDB_TABLE_INIT_NO_KEY(Netfilter);
    OVSDB_TABLE_INIT_NO_KEY(IPv6_Address);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(Interface, false);
    OVSDB_TABLE_MONITOR(Node_Config, false);

    return 0;
}
