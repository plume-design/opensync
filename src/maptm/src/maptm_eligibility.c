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

#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>
#include <ev.h>
#include <syslog.h>
#include <getopt.h>

#include "schema.h"
#include "evsched.h"
#include "log.h"
#include "os.h"
#include "ovsdb.h"
#include "evext.h"
#include "os_backtrace.h"
#include "json_util.h"
#include "target.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"

#include "maptm.h"

static ovsdb_table_t table_DHCP_Option;
struct ovsdb_table table_DHCPv6_Server;

#ifdef MAPTM_DEBUG
#undef LOGI
#define LOGI    printf
#endif
/*****************************************************************************/

#define MODULE_ID LOG_MODULE_ID_MAIN

bool wait95Option = false;
bool needReconfigure = false;

/*****************************************************************************/

/******************************************************************************
 *  PROTECTED definitions
 *****************************************************************************/

static void maptm_update_wan_mode(const char *status);

static void StartStop_DHCPv4(bool refresh)
{
    struct schema_Wifi_Inet_Config iconf_update;
    struct schema_Wifi_Inet_Config iconf;
    int ret;

    MEMZERO(iconf);
    MEMZERO(iconf_update);

    ret = ovsdb_table_select_one(
            &table_Wifi_Inet_Config,
            SCHEMA_COLUMN(Wifi_Inet_Config, if_name),
            MAPT_IFC_WAN,
            &iconf);
    if (!ret)
    {
        LOGE("%s: Failed to get Interface config", __func__);
        return;
    }

    STRSCPY(iconf_update.ip_assign_scheme, refresh ? "dhcp" : "none");
    iconf_update.ip_assign_scheme_exists = true;
    char *filter[] = { "+",
                        SCHEMA_COLUMN(Wifi_Inet_Config, ip_assign_scheme),
                        NULL };

    ret = ovsdb_table_update_where_f(
            &table_Wifi_Inet_Config,
            ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Inet_Config, if_name), MAPT_IFC_WAN),
            &iconf_update, filter);

}

// Start/Stop DHCPv6
void StartStop_DHCPv6(bool refresh)
{
    struct schema_DHCPv6_Client iconf_update;
    int ret;
    MEMZERO(iconf_update);

    iconf_update.enable = refresh;

    iconf_update.enable_exists = true;
    char *filter[] = { "+",
                        SCHEMA_COLUMN(DHCPv6_Client, enable),
                        NULL };

    ret = ovsdb_table_update_where_f(
            &table_DHCPv6_Client,
            ovsdb_where_simple_typed(SCHEMA_COLUMN(DHCPv6_Client, request_address), "true", OCLM_BOOL),
            &iconf_update, filter);
    if (!ret)
    {
        LOGE("%s: Failed to get DHCPv6_Client config", __func__);
    }
}

/* Start Workaround for MAP-T Mode Add option 23 and 24 */
bool maptm_dhcpv6_server_add_option(char *uuid, bool add)
{
    if (uuid[0] == '\0') return false;

    ovsdb_sync_mutate_uuid_set(
            SCHEMA_TABLE(DHCPv6_Server),
            ovsdb_where_simple(SCHEMA_COLUMN(DHCPv6_Server, status), "enabled"),
            SCHEMA_COLUMN(DHCPv6_Server, options),
            add ? OTR_INSERT : OTR_DELETE,
            uuid);

    return true;
}
/* End Workaround for MAP-T Mode Add option 23 and 24 */

// IPv6_Address callback
void callback_IPv6_Address(
        ovsdb_update_monitor_t *mon,
        struct schema_IPv6_Address *old,
        struct schema_IPv6_Address *new)
{
    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            // Check if IPv6 is UP and we didn't receive an option 95
            if ((strncmp(new->address, "fe80", 4) != 0) &&
                (strucWanConfig.mapt_95_value[0] == '\0') &&
                (strucWanConfig.iapd[0] != '\0'))
            {
                StartStop_DHCPv4(true);
                maptm_update_wan_mode("Dual-Stack");
            }

            break;

        case OVSDB_UPDATE_MODIFY:
        case OVSDB_UPDATE_DEL:

        default:
            return;
    }
}

// DHCP_Option callback
static void callback_DHCP_Option(
        ovsdb_update_monitor_t *mon,
        struct schema_DHCP_Option *old,
        struct schema_DHCP_Option *new
)
{
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s", new->value);

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            if (((new->tag) == 26) && (new->enable) && !strcmp((new->type), "rx"))
            {
                if (strcmp(new->value, strucWanConfig.iapd) && !strcmp("MAP-T", strucWanConfig.mapt_mode))
                {
                    LOGD("%s: IAPD is changed, we need to update MAP-T configuration", __func__);
                    needReconfigure = true;
                }
                snprintf(strucWanConfig.iapd, sizeof(strucWanConfig.iapd), "%s", new->value);
            }

            if ((new->tag) == 95)
            {
                if (new->enable)
                {
                    if (strcmp(new->value, strucWanConfig.mapt_95_value))
                    {
                        LOGD("%s: Option 95 is changed, we need to update MAP-T configuration", __func__);
                        needReconfigure = true;
                    }
                    strucWanConfig.mapt_95_Option = true;
                    snprintf(strucWanConfig.mapt_95_value, sizeof(strucWanConfig.mapt_95_value), "%s", new->value);
                }
                if (strucWanConfig.mapt_support)
                {
                    if (!wait95Option && strucWanConfig.link_up)
                    {
                        // If option 95 is added, no need to wait until timer expires to configure MAP-T
                        maptm_wan_mode();
                    }
                    else if (strucWanConfig.link_up
                             && (!strcmp("Dual-Stack", strucWanConfig.mapt_mode)))
                    {
                        // Restart the state machine if option 95 is added after renew/rebind
                        StartStop_DHCPv4(false);
                        maptm_wan_mode();
                    }
                }
            }

            /* Workaround for MAP-T Mode Add option 23 and 24 */
            if (((new->tag) == 23) && (new->enable))
            {
                /* Option 23 is updated And MAP-T is configured We need to update */
                if (!strcmp("MAP-T", strucWanConfig.mapt_mode) && strucWanConfig.option_23[0]!='\0')
                {
                    maptm_dhcpv6_server_add_option(strucWanConfig.option_23, false);
                    maptm_dhcpv6_server_add_option(new->_uuid.uuid, true);
                }
                snprintf(strucWanConfig.option_23, sizeof(strucWanConfig.option_23), "%s", new->_uuid.uuid);
            }
            if (((new->tag) == 24) && (new->enable))
            {
                /* Option 24 is updated And MAP-T is configured We need to update */
                if (!strcmp("MAP-T", strucWanConfig.mapt_mode) && strucWanConfig.option_24[0]!='\0')
                {
                    maptm_dhcpv6_server_add_option(strucWanConfig.option_24, false);
                    maptm_dhcpv6_server_add_option(new->_uuid.uuid, true);
                }
                snprintf(strucWanConfig.option_24, sizeof(strucWanConfig.option_24), "%s", new->_uuid.uuid);
            }
            /* End Workaround add Option */

            if (((new->tag) == 10) && (strcmp((new->value),"1") == 0))
            {
                StartStop_DHCPv4(true);
                maptm_update_wan_mode("Dual-Stack");
            }

            break;

        case OVSDB_UPDATE_MODIFY:
            if ((new->tag) == 95)
            {
                if (new->enable)
                {
                    strucWanConfig.mapt_95_Option = true;
                    snprintf(strucWanConfig.mapt_95_value, sizeof(strucWanConfig.mapt_95_value), "%s", new->value);
                }
                else
                {
                    strucWanConfig.mapt_95_Option = false;
                }
            }

            break;

        case OVSDB_UPDATE_DEL:
            if ((old->tag) == 95
                && strucWanConfig.mapt_support
                && !strcmp("MAP-T", strucWanConfig.mapt_mode ))
            {
                if (!strucWanConfig.link_up)
                {
                    maptm_eligibilityStop();
                }
                else if (!strcmp(old->value,strucWanConfig.mapt_95_value))
                {
                    /* Option 95 has been removed we need to restart eligibility state machine */
                    maptm_eligibilityStop();
                    maptm_eligibilityStart(MAPTM_ELIGIBLE_IPV6);
                }
                else if (needReconfigure)
                {
                    LOGD("%s Reconfigure MAP-T", __func__);
                    stop_mapt();
                    maptm_wan_mode();
                }
            }

            /* Start Workaround for MAP-T Mode Add option 23 and 24 */
            if ((old->tag == 24) &&
                !strcmp("MAP-T", strucWanConfig.mapt_mode ) &&
                !strcmp(old->_uuid.uuid,strucWanConfig.option_24))
            {
                strucWanConfig.option_24[0] = '\0';
            }
            if ((old->tag == 23) &&
                !strcmp("MAP-T", strucWanConfig.mapt_mode ) &&
                !strcmp(old->_uuid.uuid,strucWanConfig.option_23))
            {
                strucWanConfig.option_23[0] = '\0';
            }
            /* End Workaround for MAP-T Mode Add option 23 and 24 */

            if ((old->tag) == 26)
            {
                char *flag = NULL;
                if (!strcmp(old->value, strucWanConfig.iapd))
                {
                    strucWanConfig.iapd[0] = '\0';
                }
                else if (needReconfigure)
                {
                    LOGE(" [%s] Reconfigure MAPT", __func__);
                    stop_mapt();
                    maptm_wan_mode();
                }

                flag = strtok(old->value, ",");
                char cmd[300] = "";
                if (flag)
                {
                    sprintf(cmd,"ip -6 route del %s", old->value);
                    system(cmd);
                }
            }

            break;

            default:
                LOGE("DHCP_Option OVSDB event: unknown type %d", mon->mon_type);
                return;
    } /* switch */
}

// Initialize eligibility state machine
int init_eligibility(void)
{
    wait95Option = false;
    StartStop_DHCPv6(false);
    StartStop_DHCPv4(false);
    strucWanConfig.mapt_EnableIpv6 = true;
    return 0;
}

// Update DHCP option 15 which states if board is MAP-T eligible
bool maptm_dhcp_option_update_15_option(bool maptSupport)
{
    struct schema_DHCP_Option iconf_update;
    struct schema_DHCP_Option iconf;
    int ret;

    MEMZERO(iconf);
    MEMZERO(iconf_update);

    ret = ovsdb_table_select_one(
            &table_DHCP_Option,
            SCHEMA_COLUMN(DHCP_Option, value),
            maptSupport ? CONFIG_MAPTM_WAN_DUALSTACK : CONFIG_MAPTM_WAN_MAP, &iconf);
    if (!ret)
    {
        LOGE("%s: Failed to get DHCP_Option config", __func__);
        return false;
    }

    STRSCPY(iconf_update.value, maptSupport ? CONFIG_MAPTM_WAN_MAP : CONFIG_MAPTM_WAN_DUALSTACK);
    iconf_update.value_exists = true;
    char *filter[] = { "+",
                        SCHEMA_COLUMN(DHCP_Option, value),
                        NULL };

    ret = ovsdb_table_update_where_f(
            &table_DHCP_Option,
            ovsdb_where_simple(SCHEMA_COLUMN(DHCP_Option, value),
            maptSupport ? CONFIG_MAPTM_WAN_DUALSTACK : CONFIG_MAPTM_WAN_MAP),
            &iconf_update, filter);

    return true;
}

// Control the request of option 95
bool maptm_dhcp_option_update_95_option(bool maptSupport)
{
    struct schema_DHCPv6_Client iconf_update;
    struct schema_DHCPv6_Client iconf;

    MEMZERO(iconf);
    MEMZERO(iconf_update);
    SCHEMA_VAL_APPEND_INT(iconf_update.request_options, 23);
    SCHEMA_VAL_APPEND_INT(iconf_update.request_options, 24);
    SCHEMA_VAL_APPEND_INT(iconf_update.request_options, 56);

    if (maptSupport)
    {
        SCHEMA_VAL_APPEND_INT(iconf_update.request_options, 95);
    }

    char *filter[] = { "+",
                        SCHEMA_COLUMN(DHCPv6_Client, request_options),
                        NULL };

    ovsdb_table_update_where_f(
            &table_DHCPv6_Client,
            ovsdb_where_simple_typed(SCHEMA_COLUMN(DHCPv6_Client, request_address), "true", OCLM_BOOL),
            &iconf_update,
            filter);

    return true;
}

// Update WAN mode
static void maptm_update_wan_mode(const char *status)
{
    struct schema_Node_State set;
    json_t *where = NULL;
    int rc = 0;

    LOGA(" [%s] ", status);
    strcpy(strucWanConfig.mapt_mode, status);
    memset(&set, 0, sizeof(set));
    set._partial_update = true;
    SCHEMA_SET_STR(set.value, status);

    where = ovsdb_where_multi(
            ovsdb_where_simple_typed(SCHEMA_COLUMN(Node_State, module), MAPT_MODULE_NAME, OCLM_STR),
            ovsdb_where_simple_typed(SCHEMA_COLUMN(Node_State, key), "maptMode", OCLM_STR),
            NULL);

    if (where == NULL)
    {
        LOGA(" [%s] ERROR: where is NULL", __func__);
        return;
    }

    rc = ovsdb_table_update_where(&table_Node_State, where, &set);
    if (rc == 1)
    {
        LOGA(" [%s] status is [%s]", __func__, status);
    }
    else
    {
        LOGA(" [%s] ERROR status: unexpected result [%d]", __func__, rc);
    }
}

void maptm_wan_mode(void)
{
    if (!wait95Option) wait95Option = true;
    if (strucWanConfig.mapt_95_Option)
    {
        if (config_mapt())
        {
             StartStop_DHCPv4(false);
             maptm_update_wan_mode("MAP-T");

             /* Start Workaround for MAP-T Mode Add option 23 and 24 */
             maptm_dhcpv6_server_add_option(strucWanConfig.option_23, true);
             maptm_dhcpv6_server_add_option(strucWanConfig.option_24, true);
             /* End Workaround for MAP-T Mode Add option 23 and 24 */
        }
        else
        {
             LOGE("Unable to configure MAP-T!");

             /* Resolve limitation: Wrong MAP-T Rule */
             StartStop_DHCPv4(true);
             maptm_update_wan_mode("Dual-Stack");
        }
    }
    needReconfigure = false;
}

// Initialize and monitor DHCP_Option Ovsdb table
int maptm_dhcp_option_init(void)
{
    OVSDB_TABLE_INIT_NO_KEY(DHCP_Option);
    OVSDB_TABLE_INIT_NO_KEY(IPv6_Address);

    OVSDB_TABLE_MONITOR(DHCP_Option, false);
    OVSDB_TABLE_MONITOR(IPv6_Address, false);
    return 0;
}

// Stop eligibility state machine
void maptm_eligibilityStop(void)
{
    StartStop_DHCPv6(false);
    StartStop_DHCPv4(false);
    wait95Option = false;
    strucWanConfig.mapt_95_Option = false;
    strucWanConfig.mapt_95_value[0] = '\0';
    maptm_dhcpv6_server_add_option(strucWanConfig.option_23, false);
    maptm_dhcpv6_server_add_option(strucWanConfig.option_24, false);
    if (!strcmp("MAP-T", strucWanConfig.mapt_mode))
    {
        LOGD("%s: Stop MAP-T", __func__);
        stop_mapt();
    }
}

// Check if IPv6 is enabled
bool maptm_ipv6IsEnabled(void)
{
    struct schema_IPv6_Address addr;
    int rc = 0;
    rc = ovsdb_table_select_one(&table_IPv6_Address, "origin", "auto_configured", &addr);
    if (rc && addr.enable)
    {
        LOGT("IPv6 is Enabled");
        return true;
    }
    LOGT("IPv6 is Disabled");
    return false;
}

// Start eligibility state machine
void maptm_eligibilityStart(int WanConfig)
{
    init_eligibility();

    // Check mapt_support value and update WanConfig and requested options list
    LOGT("%s mapt_support is %s" , __func__, strucWanConfig.mapt_support ? "enabled" : "disabled");
    if (strucWanConfig.mapt_support) {
        WanConfig |= MAPTM_ELIGIBILITY_ENABLE;
    } else {
        WanConfig &= MAPTM_IPV6_ENABLE;
    }
    maptm_dhcp_option_update_15_option(strucWanConfig.mapt_support);
    maptm_dhcp_option_update_95_option(strucWanConfig.mapt_support);

    // Check IPv6 is Enabled
    if (strucWanConfig.mapt_EnableIpv6 || maptm_ipv6IsEnabled())
    {
        WanConfig |= MAPTM_IPV6_ENABLE;
    }
    else
    {
        WanConfig &= MAPTM_ELIGIBLE_NO_IPV6;
    }

    // Check DHCPv6 services
    switch (WanConfig)
    {
        case MAPTM_NO_ELIGIBLE_NO_IPV6:  // check IPv4 only
        {
             LOGT("*********** IPv4 only");
             maptm_update_wan_mode("IPv4 Only");
             StartStop_DHCPv4(true);
             break;
        }
        case MAPTM_NO_ELIGIBLE_IPV6:
        {
             LOGT("*********** DUAL STACK");
             maptm_update_wan_mode("Dual-Stack");
             StartStop_DHCPv6(true);
             StartStop_DHCPv4(true);
             break;
        }
        case MAPTM_ELIGIBLE_NO_IPV6:
        {
             StartStop_DHCPv4(true);
             LOGT("*********** IPv4 only");
             maptm_update_wan_mode("IPv4 Only");
             break;
        }
        case MAPTM_ELIGIBLE_IPV6:
        {
            LOGI("*********** MAP-T");
            StartStop_DHCPv6(true);
            break;
        }
        default:
             LOGE("Unable to determine WAN Mode");
    }

}
