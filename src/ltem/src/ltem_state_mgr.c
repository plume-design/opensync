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

#include <ev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>

#include "ltem_mgr.h"
#include "log.h"
#include "neigh_table.h"

char *ltem_lte_state_info[] =
{
    "LTE_UNKNOWN",
    "LTE_INIT",
    "LTE_UP",
    "LTE_DOWN",
};

/**
 * @brief Get lte state info
 */
char *
ltem_get_lte_state_info(enum ltem_lte_state state)
{
    if ((int)state < 0 || state >= LTEM_LTE_STATE_NUM)
    {
        LOG(ERROR, "Invalid state: %d", state);
        return "INVALID";
    }
    return ltem_lte_state_info[state];
}

char *
ltem_get_lte_state_name(enum ltem_lte_state state)
{
    return ltem_get_lte_state_info(state);
}

char *ltem_wan_state_info[] =
{
    "WAN_UNKNOWN",
    "WAN_UP",
    "WAN_DOWN",
};

/**
 * @brief Get wan state info
 */
char *
ltem_get_wan_state_info(enum ltem_wan_state state)
{
    if ((int)state < 0 || state >= LTEM_WAN_STATE_NUM)
    {
        LOG(ERROR, "Invalid state: %d", state);
        return "INVALID";
    }
    return ltem_wan_state_info[state];
}

char *
ltem_get_wan_state_name(enum ltem_wan_state state)
{
    return ltem_get_wan_state_info(state);
}

/**
 * @brief WAN is interface down
 */
static void
ltem_set_failover(ltem_mgr_t *mgr)
{
    int res;

    if (mgr->lte_config_info->force_use_lte)
    {
        res = ltem_force_lte_route(mgr);
        if (!res) /* If we fail to update the route, we don't set failover */
        {
            mgr->lte_state_info->lte_failover_force = true;
            mgr->lte_state_info->lte_failover_active = true;
            mgr->lte_state_info->lte_failover_start = time(NULL);
            mgr->lte_state_info->lte_failover_end = 0;
            mgr->lte_state_info->lte_failover_count++;
        }
    }
    else
    {
            mgr->lte_state_info->lte_failover_active = true;
            mgr->lte_state_info->lte_failover_start = time(NULL);
            mgr->lte_state_info->lte_failover_end = 0;
            mgr->lte_state_info->lte_failover_count++;
    }

    LOGI("%s: failover_active=%d, start time[%ld]", __func__,
         mgr->lte_state_info->lte_failover_active,
         mgr->lte_state_info->lte_failover_start);
}

/**
 * @brief WAN interface is back up
 */
static void
ltem_revert_failover(ltem_mgr_t *mgr)
{
    int res;

    if (mgr->lte_state_info->lte_failover_force)
    {

        res = ltem_restore_default_wan_route(mgr);
        if (!res)
        {
            mgr->lte_state_info->lte_failover_force = false;
            mgr->lte_state_info->lte_failover_active = false;
            mgr->lte_state_info->lte_failover_end = time(NULL);
        }
    }
    else
    {
        mgr->lte_state_info->lte_failover_active = false;
        mgr->lte_state_info->lte_failover_end = time(NULL);
    }

    LOGI("%s: failover_active=%d, start time[%ld], end time[%ld]", __func__,
         mgr->lte_state_info->lte_failover_active,
         mgr->lte_state_info->lte_failover_start,
         mgr->lte_state_info->lte_failover_end);
}

/**
 * @brief Handle WAN state change
 */
static void
ltem_handle_wan_state_change(ltem_mgr_t *mgr)
{
    switch (mgr->wan_state) {
    case LTEM_WAN_STATE_UNKNOWN:
        break;
    case LTEM_WAN_STATE_DOWN:
        if (!mgr || !mgr->lte_config_info || !mgr->lte_config_info->manager_enable)
        {
            LOGD("%s: %s, manager_enable=false", __func__, ltem_get_wan_state_name(mgr->wan_state));
            break;
        }
        if (mgr->lte_state == LTEM_LTE_STATE_UP && mgr->lte_config_info->lte_failover_enable)
        {
            ltem_set_failover(mgr);
        }
        break;
    case LTEM_WAN_STATE_UP:
        if (mgr->lte_state == LTEM_LTE_STATE_UP && mgr->lte_state_info->lte_failover_active)
        {
            ltem_revert_failover(mgr);
        }
        break;
    default:
        break;
    }
    LOGI("%s: wan_state: %s, lte_state[%s] failover_enable[%d]", __func__,
         ltem_get_wan_state_name(mgr->wan_state), ltem_get_lte_state_name(mgr->lte_state), mgr->lte_config_info->lte_failover_enable);
}

/**
 * @brief Update WAN state
 */
void
ltem_set_wan_state(enum ltem_wan_state wan_state) {
    ltem_mgr_t *mgr;

    mgr = ltem_get_mgr();

    if (!mgr || !mgr->lte_config_info || !mgr->lte_config_info->manager_enable) return;

    if (mgr->wan_state == wan_state)
    {
        LOGI("Same wan_state %s", ltem_get_wan_state_name(wan_state));
        return;
    }
    else
    {
        LOGI("%s: Old wan_state %s, New wan_state %s", __func__, ltem_get_wan_state_name(mgr->wan_state), ltem_get_wan_state_name(wan_state));
        mgr->wan_state = wan_state;
        ltem_handle_wan_state_change(mgr);
    }

    return;
}

/**
 * @brief Handle LTE state change
 */
static void
ltem_handle_lte_state_change(ltem_mgr_t *mgr)
{
    int res = 0;
    switch (mgr->lte_state) {
    case LTEM_LTE_STATE_UNKNOWN:
    case LTEM_LTE_STATE_INIT:
        break;
    case LTEM_LTE_STATE_UP:
        mgr->lte_route->has_L3 = true;
        res = ltem_ovsdb_cmu_insert_lte(mgr);
        if (res)
        {
            LOGI("%s: ltem_ovsdb_cmu_create_lte: failed, res[%d]", __func__, res);
            ltem_set_lte_state(LTEM_LTE_STATE_DOWN);
        }
        else
        {
            res = ltem_set_lte_route_metric(mgr);
            if (res)
            {
                LOGI("%s: ltem_set_lte_route_metric: failed, res[%d]", __func__, res);
                break;
            }

            LOGD("%s: wan_state[%s], lte_failover_enable[%d], force_use_lte[%d]",
                 __func__, ltem_get_wan_state_name(mgr->wan_state), mgr->lte_config_info->lte_failover_enable, mgr->lte_config_info->force_use_lte);

            if (mgr->wan_state == LTEM_WAN_STATE_UP && (mgr->lte_config_info->lte_failover_enable && mgr->lte_config_info->force_use_lte))
            {
                LOGI("%s: force_use_lte[%d], bring down WAN", __func__, mgr->lte_config_info->force_use_lte);
                ltem_set_wan_state(LTEM_WAN_STATE_DOWN);
            }
        }
        break;
    case LTEM_LTE_STATE_DOWN:
        mgr->lte_route->has_L3 = false;
        ltem_ovsdb_cmu_disable_lte(mgr);
        if (mgr->lte_state_info->lte_failover_active)
        {
            ltem_revert_failover(mgr);
        }
        break;
    default:
        break;
    }
    LOGI("%s: state: %s, res %d", __func__, ltem_get_lte_state_name(mgr->lte_state), res);
}

/**
 * @brief Init the LTE modem and start the LTE daemon
 */
bool
ltem_init_lte_modem(void)
{
    int ret;

    osn_lte_set_qmi_mode();
    osn_lte_enable_sim_detect();
    ret = osn_lte_read_pdp_context();
    if (ret) return false;
    ret = osn_lte_set_pdp_context_params(PDP_CTXT_PDP_TYPE, PDP_TYPE_IPV4);
    ret |= osn_lte_set_ue_data_centric();
    if (ret)
    {
        osn_lte_reset_modem();
    }
    osn_lte_start_vendor_daemon(SOURCE_AT_CMD);
    return true;
}

/**
 * @brief Update LTE state
 */
void
ltem_set_lte_state(enum ltem_lte_state lte_state) {
    ltem_mgr_t *mgr;

    mgr = ltem_get_mgr();

    if (mgr->lte_state == lte_state)
    {
        LOGD("Same lte_state %s", ltem_get_lte_state_name(lte_state));
        return;
    }
    else
    {
        LOGI("%s: Old Lte_State %s, New lte_state %s", __func__, ltem_get_lte_state_name(mgr->lte_state), ltem_get_lte_state_name(lte_state));
    }
    mgr->lte_state = lte_state;

    ltem_handle_lte_state_change(mgr);

    return;
}

/**
 * @brief stop the LTE daemon
 */
void
ltem_fini_lte_modem(void)
{
    osn_lte_stop_vendor_daemon();
}
