/*
 * Copyright (c) 2021, Sagemcom.
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
#include <stdlib.h>

#include "os.h"
#include "log.h"
#include "os_nif.h"

#include "pwm_ovsdb.h"
#include "pwm_dhcp_relay.h"

#define PWM_VAP_SSID_SIZE 64
#define PWM_CMD_LEN 256

#if !defined(CONFIG_ENTERPRISE_NBR)
/* Enterprise ID: The Broadband Forum (formerly 'ADSL Forum') */
#define CONFIG_ENTERPRISE_NBR 3561
#endif /* CONFIG_ENTERPRISE_NBR */

char g_pwm_ssid[PWM_VAP_SSID_SIZE];
extern struct ovsdb_table table_Wifi_VIF_Config;

// Read ssid information from Wifi_VIF_Config table
static char *pwm_dhcp_relay_read_ssid(struct schema_Public_Wifi_Config *conf)
{
    bool ret_bool;
    struct schema_Wifi_VIF_Config vconf;

    strcpy(g_pwm_ssid, "");
    MEMZERO(vconf);
    ret_bool = ovsdb_table_select_one(&table_Wifi_VIF_Config,
                                      SCHEMA_COLUMN(Wifi_VIF_Config,
                                                    if_name),
                                      conf->vif_ifnames[0], &vconf);
    if (!ret_bool) {
        LOGE("%s: Failed to get Wifi_VIF_Config", __func__);
    } else {
        STRSCPY_WARN(g_pwm_ssid, vconf.ssid);
    }

    return g_pwm_ssid;
}

// Generate dhcp relay options configuration file
static bool pwm_dhcp_relay_gen_options_conf(struct schema_Public_Wifi_Config *conf)
{
    char hwaddr[C_MACADDR_LEN] = { 0 };
    FILE *cfg;
    os_macaddr_t mac;
    bool ret;

    cfg = fopen(CONFIG_DHCP_RELAY_OPTIONS_CONF, "w");
    if (!cfg) {
        LOGE("Failed to create dhcp relay options conf file");
        return -1;
    }

    ret = os_nif_macaddr(CONFIG_PWM_WAN_IF_NAME, &mac);
    if (ret == true)
    {
        ret = os_nif_macaddr_to_str(&mac, (char *)hwaddr, PRI_os_macaddr_lower_t);
    }

    if ((strlen(hwaddr)) && (ret == true))
    {
        /* Insert option Interface-Id (18) Format : "DHCPv4_OPTION:82='CIRCUIT_ID:{VALUE},REMOTE_ID'" */
        char securityMode = 's';
        fprintf(cfg, "DHCPv4_OPTION:82='CIRCUIT_ID:%s;%s;%c,REMOTE_ID'\n",
                (const char *) hwaddr, (const char *) pwm_dhcp_relay_read_ssid(conf),
                securityMode);

        /* Insert option Interface-Id (18) Format : "DHCPv6_OPTION:18='{VALUE}'" */
        fprintf(cfg, "DHCPv6_OPTION:18='%s;%s;%c'\n",
                (const char *) hwaddr, (const char *) pwm_dhcp_relay_read_ssid(conf),
                securityMode);
    }
    /* Insert Option Option: Remote Identifier (37) Format : "DHCPv6_OPTION:37='ENTERPRISE_NBR:{Value},REMOTE_ID'" */
    fprintf(cfg, "DHCPv6_OPTION:37='ENTERPRISE_NBR:%d,REMOTE_ID'\n",
            CONFIG_ENTERPRISE_NBR);

    fclose(cfg);
    return true;
}

// Create and configure DHCP relay options
bool pwm_dhcp_relay_create_options(struct schema_Public_Wifi_Config *conf)
{
    bool ret;

    ret = pwm_dhcp_relay_gen_options_conf(conf);
    if (!ret) {
        LOGE("Failed to generate DHCP opt82 configuration");
        return false;
    }

    return true;
}
