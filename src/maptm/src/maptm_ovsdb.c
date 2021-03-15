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

// Function used to set values in persistant storage
bool maptm_ps_set(const char *key, char *value)
{
    bool ret = false;
    osp_ps_t *ps = NULL;
    ssize_t size = -1;

    ps = osp_ps_open(MAPT_PS_STORE_NAME, OSP_PS_WRITE | OSP_PS_PRESERVE);
    if (!ps)
    {
        LOGE("Open persistent storage failed");
        goto out;
    }

    size = osp_ps_set(ps, key, value, strlen(value) + 1);
    if (size < 0)
    {
        LOGE("Set OpenSync platform persistent storage failed");
        goto out;
    }

    ret = true;
out:
    if (ps)
    {
        ret = osp_ps_close(ps);
        if (!ret)
        {
            LOGE("Close persistent storage failed");
            return false;
        }
    }
    return ret;
}

// Make MAPT_Support parameter persistent
bool maptm_persistent(void)
{
    bool ret = false;
    osp_ps_t *ps = NULL;
    ssize_t size = -1;
    char mapt_support[10];

    // Open persistent storage in read-write mode
    ps = osp_ps_open(MAPT_PS_STORE_NAME, OSP_PS_RDWR | OSP_PS_PRESERVE);
    if (!ps)
    {
        LOGE("Failed when opening persistent storage");
        goto out;
    }

    // Get persistent storage value
    size = osp_ps_get(ps, MAPT_PS_KEY_NAME, mapt_support, sizeof(mapt_support));
    if (size <= 0)
    {
        LOGE("Failed when getting persistent storage value");
        snprintf(mapt_support, sizeof(mapt_support), "%s", "true");

        // Set persistent storage value
        if (!(osp_ps_set(ps, MAPT_PS_KEY_NAME, mapt_support, strlen(mapt_support)+1) <= 0))
        {
            LOGE("Failed when setting persistent storage value");
            strucWanConfig.mapt_support = true;
            goto out;
        }
    }

    LOGT("%s: mapt_support = %s", __func__, mapt_support);

    if (strcmp(mapt_support, "true"))
    {
        strucWanConfig.mapt_support = false;
    }
    ret = true;

out:
    //Check if value was safely stored and cleanly release ps handle
    ret = osp_ps_close(ps);
    if (!ret)
    {
        LOGE("Failed when closing persistent storage");
        return false;
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

        if (!strcmp(conf->module, MAPT_MODULE_NAME))
        {
            if (strucWanConfig.mapt_support != maptm_get_supportValue(conf->value))
            {
                WanConfig = 0;
                if (maptm_get_supportValue(conf->value))
                {
                    WanConfig |= MAPTM_ELIGIBILITY_ENABLE;
                }
                strucWanConfig.mapt_support = maptm_get_supportValue(conf->value);
                maptm_dhcp_option_update_15_option(strucWanConfig.mapt_support);
                maptm_dhcp_option_update_95_option(strucWanConfig.mapt_support);
                maptm_eligibilityStart(WanConfig);
                if (!(maptm_ps_set(MAPT_PS_KEY_NAME, maptm_get_supportValue(conf->value) ? "true" : "false")))
                {
                    LOGE("Error saving new MAP-T support value");
                }
            }
        }
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        if (!strcmp(conf->module, MAPT_MODULE_NAME))
        {
            LOGD("%s: node config entry deleted: module %s, key: %s, value: %s",
                    __func__, old_rec->module, old_rec->key, old_rec->value);

            if (!(maptm_ps_set(MAPT_PS_KEY_NAME, maptm_get_supportValue(conf->value) ? "true" : "false")))
            {
                LOGE("Error saving new MAP-T support value");
            }
        }
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        LOGD("%s: node config entry updated: \n"
                "old module: %s, old key: %s, old value: %s \n"
                "new module: %s, new key: %s, new value: %s",
                __func__, old_rec->module, old_rec->key, old_rec->value,
                conf->module, conf->key, conf->value);

        if (!strcmp(conf->module, MAPT_MODULE_NAME))
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
                // Restart the eligibility state machine
                maptm_eligibilityStop();
                maptm_eligibilityStart(WanConfig);
                if (!(maptm_ps_set(MAPT_PS_KEY_NAME, maptm_get_supportValue(conf->value) ? "true" : "false")))
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
            if (!strcmp(record->name, MAPT_IFC_WAN))
            {
                if (!strcmp(record->link_state, "up") && (strucWanConfig.link_up == false))
                {
                    // Restart the eligibility state machine
                    maptm_eligibilityStop();
                    strucWanConfig.link_up = true;
                    maptm_eligibilityStart(WanConfig);
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
