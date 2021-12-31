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

#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "const.h"
#include "os.h"

#include "pwm_ovsdb.h"
#include "pwm_bridge.h"
#include "pwm_firewall.h"
#include "pwm_utils.h"
#include "pwm_wifi.h"

#define MODULE_ID LOG_MODULE_ID_MISC

struct pwm_wifi_used {
    char                    ifname[IFNAMSIZ];
    struct ds_dlist_node    list;
};

static ds_dlist_t g_pwm_wifi_used_list = DS_DLIST_INIT(struct pwm_wifi_used, list);

extern struct ovsdb_table table_Wifi_VIF_Config;

int pwm_wifi_update_vif_config(char *wifi_if_name, bool status)
{
    int     rc;
    json_t *where;
    struct schema_Wifi_VIF_Config wifi_update;

    LOGD("PWM: Wifi_VIF_Config UPDATE: wifi=[%s]", wifi_if_name);

    MEM_SET(&wifi_update, 0, sizeof(struct schema_Wifi_VIF_Config));
    wifi_update._partial_update = true;

    if (status) {
        SCHEMA_SET_INT(wifi_update.enabled, 1);
    } else {
        SCHEMA_SET_INT(wifi_update.enabled, 0);
    }
    wifi_update.enabled_changed = true;

    where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_VIF_Config, if_name), wifi_if_name);
    if (where == NULL) {
        LOGE("PWM: Wifi_VIF_Config UPDATE: Could not get if_name=[%s]", wifi_if_name);
        return 1;
    }

    rc = ovsdb_table_update_where(&table_Wifi_VIF_Config, where, &wifi_update);
    if (rc != 1) {
        LOGE("PWM: Wifi_VIF_Config UPDATE: wifi=[%s] rc=[%d]",
                wifi_if_name, rc);
        return 2;
    }

    return 0;
}

void pwm_wifi_disable(void)
{
    struct pwm_wifi_used *p_vif_name;

    p_vif_name = ds_dlist_head(&g_pwm_wifi_used_list);
    while( p_vif_name != NULL )
    {
        LOGD("Wifi_VIF_Config DISABLE: AP at [%s]",
                p_vif_name->ifname);

        if (pwm_wifi_update_vif_config(p_vif_name->ifname, false) != 0) {
            LOGE("Wifi_VIF_Config DISABLE: update parameters failed");
        }

        ds_dlist_remove(&g_pwm_wifi_used_list, p_vif_name);
        free(p_vif_name);

        p_vif_name = ds_dlist_head(&g_pwm_wifi_used_list);
    }

    pwm_ovsdb_update_state_table(PWM_STATE_TABLE_VIF_IFNAMES, false, NULL);
}

void pwm_wifi_set(struct schema_Public_Wifi_Config *m_conf)
{
    int i;
    struct pwm_wifi_used *p_vif_name;
    char*   state_vif_ifnames[STATE_VIF_IFNAMES_LENGTH];

    if (m_conf->vif_ifnames_len < 1) {
        LOGI("WIFI SET: unexpected vif_ifname members count [%d]", m_conf->vif_ifnames_len);
        return;
    }
    LOGD("WIFI SET: vif_ifname n=[%d]", m_conf->vif_ifnames_len);

    MEM_SET(&state_vif_ifnames, 0, sizeof(state_vif_ifnames));

    for (i = 0; i < m_conf->vif_ifnames_len; i++)
    {
        if (pwm_wifi_update_vif_config(m_conf->vif_ifnames[i], true) == 0)
        {
            p_vif_name = calloc(1, sizeof(struct pwm_wifi_used));
            if (p_vif_name != NULL)
            {
                STRSCPY_WARN(p_vif_name->ifname, m_conf->vif_ifnames[i]);
                ds_dlist_insert_tail(&g_pwm_wifi_used_list, p_vif_name);
                state_vif_ifnames[i] = m_conf->vif_ifnames[i];
            }
            else
            {
                LOGE("Wifi_VIF_Config SET:: add %s port to PWM wifi list failed",
                        m_conf->vif_ifnames[i]);
            }
        }
    }

    if (state_vif_ifnames[0] != NULL)
        pwm_ovsdb_update_state_table(PWM_STATE_TABLE_VIF_IFNAMES, true, state_vif_ifnames);
}
