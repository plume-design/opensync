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

#include "cellm_mgr.h"
#include "log.h"
#include "neigh_table.h"

char *cellm_state_info[] = {
    "CELLM_STATE_UNKNOWN",
    "CELLM_STATE_INIT",
    "CELLM_STATE_UP",
    "CELLM_STATE_DOWN",
};

/**
 * @brief Get LTE state info
 */
char *cellm_get_cellm_state_info(enum cellm_state state)
{
    if ((int)state < 0 || state >= CELLM_STATE_NUM)
    {
        LOG(ERROR, "Invalid state: %d", state);
        return "INVALID";
    }
    return cellm_state_info[state];
}

char *cellm_get_state_name(enum cellm_state state)
{
    return cellm_get_cellm_state_info(state);
}

char *cellm_wan_state_info[] = {
    "WAN_UNKNOWN",
    "WAN_UP",
    "WAN_DOWN",
};

/**
 * @brief Get WAN state info
 */
char *cellm_get_wan_state_info(enum cellm_wan_state state)
{
    if ((int)state < 0 || state >= CELLM_WAN_STATE_NUM)
    {
        LOG(ERROR, "Invalid state: %d", state);
        return "INVALID";
    }
    return cellm_wan_state_info[state];
}

char *cellm_get_wan_state_name(enum cellm_wan_state state)
{
    return cellm_get_wan_state_info(state);
}

/**
 * @brief WAN is interface down
 */
void cellm_set_failover(cellm_mgr_t *mgr)
{
    int res;

    if (mgr->cellm_config_info->force_use_lte)
    {
        res = celln_force_lte(mgr);
        if (!res) /* If we fail to update the route, we don't set failover */
        {
            mgr->cellm_state_info->cellm_failover_force = true;
            mgr->cellm_state_info->cellm_failover_active = true;
            mgr->cellm_state_info->cellm_failover_start = time(NULL);
            mgr->cellm_state_info->cellm_failover_end = 0;
            mgr->cellm_state_info->cellm_failover_count++;
        }
    }
    else
    {
        mgr->cellm_state_info->cellm_failover_active = true;
        mgr->cellm_state_info->cellm_failover_start = time(NULL);
        mgr->cellm_state_info->cellm_failover_end = 0;
        mgr->cellm_state_info->cellm_failover_count++;
    }

    LOGI("%s: failover_active=%d, start time[%ld]",
         __func__,
         mgr->cellm_state_info->cellm_failover_active,
         mgr->cellm_state_info->cellm_failover_start);

    cellm_flush_flows(mgr);
    cellm_ovsdb_set_v6_failover(mgr);
}

/**
 * @brief WAN interface is back up
 */
static void cellm_revert_failover(cellm_mgr_t *mgr)
{
    int res;

    if (mgr->cellm_state_info->cellm_failover_force)
    {
        res = cellm_restore_wan(mgr);
        if (!res)
        {
            mgr->cellm_state_info->cellm_failover_force = false;
            mgr->cellm_state_info->cellm_failover_active = false;
            mgr->cellm_state_info->cellm_failover_end = time(NULL);
        }
    }
    else
    {
        mgr->cellm_state_info->cellm_failover_active = false;
        mgr->cellm_state_info->cellm_failover_end = time(NULL);
    }

    LOGI("%s: failover_active=%d, start time[%ld], end time[%ld]",
         __func__,
         mgr->cellm_state_info->cellm_failover_active,
         mgr->cellm_state_info->cellm_failover_start,
         mgr->cellm_state_info->cellm_failover_end);

    cellm_flush_flows(mgr);
    cellm_ovsdb_revert_v6_failover(mgr);
}

/**
 * @brief Handle WAN state change
 */
static void cellm_handle_wan_state_change(cellm_mgr_t *mgr)
{
    switch (mgr->wan_state)
    {
        case CELLM_STATE_UNKNOWN:
            break;
        case CELLM_WAN_STATE_DOWN:
            if (!mgr || !mgr->cellm_config_info || !mgr->cellm_config_info->manager_enable)
            {
                LOGD("%s: %s, manager_enable=false", __func__, cellm_get_wan_state_name(mgr->wan_state));
                break;
            }
            if (mgr->cellm_state == CELLM_STATE_UP && mgr->cellm_config_info->cellm_failover_enable)
            {
                cellm_set_failover(mgr);
            }
            break;
        case CELLM_WAN_STATE_UP:
            if (mgr->cellm_state == CELLM_STATE_UP && mgr->cellm_state_info->cellm_failover_active)
            {
                cellm_revert_failover(mgr);
            }
            break;
        default:
            break;
    }
    LOGI("%s: wan_state: %s, lte_state[%s] failover_enable[%d]",
         __func__,
         cellm_get_wan_state_name(mgr->wan_state),
         cellm_get_state_name(mgr->cellm_state),
         mgr->cellm_config_info->cellm_failover_enable);
}

/**
 * @brief Update WAN state
 */
void cellm_set_wan_state(enum cellm_wan_state wan_state)
{
    cellm_mgr_t *mgr;

    mgr = cellm_get_mgr();

    if (!mgr || !mgr->cellm_config_info || !mgr->cellm_config_info->manager_enable) return;

    if (mgr->wan_state == wan_state)
    {
        LOGI("Same wan_state %s", cellm_get_wan_state_name(wan_state));
        return;
    }
    else
    {
        LOGI("%s: Old wan_state %s, New wan_state %s",
             __func__,
             cellm_get_wan_state_name(mgr->wan_state),
             cellm_get_wan_state_name(wan_state));
        mgr->wan_state = wan_state;
        cellm_handle_wan_state_change(mgr);
    }

    return;
}

/**
 * @brief CELLM state change
 */
static void cellm_handle_lte_state_change(cellm_mgr_t *mgr)
{
    int res = 0;
    switch (mgr->cellm_state)
    {
        case CELLM_STATE_UNKNOWN:
        case CELLM_STATE_INIT:
            break;
        case CELLM_STATE_UP:
            mgr->cellm_route->has_L3 = true;
            res = cellm_ovsdb_cmu_insert_lte(mgr);
            if (res)
            {
                LOGI("%s: ltem_ovsdb_cmu_create_lte: failed, res[%d]", __func__, res);
                cellm_set_state(CELLM_STATE_DOWN);
            }
            else
            {
                res = cellm_set_route_metric(mgr);
                if (res)
                {
                    LOGI("%s: ltem_set_lte_route_metric: failed, res[%d]", __func__, res);
                    break;
                }

                LOGD("%s: wan_state[%s], lte_failover_enable[%d], force_use_lte[%d]",
                     __func__,
                     cellm_get_wan_state_name(mgr->wan_state),
                     mgr->cellm_config_info->cellm_failover_enable,
                     mgr->cellm_config_info->force_use_lte);

                if (mgr->wan_state == CELLM_WAN_STATE_UP
                    && (mgr->cellm_config_info->cellm_failover_enable && mgr->cellm_config_info->force_use_lte))
                {
                    LOGI("%s: force_use_lte[%d], bring down WAN", __func__, mgr->cellm_config_info->force_use_lte);
                    cellm_set_wan_state(CELLM_WAN_STATE_DOWN);
                }
            }
            break;
        case CELLM_STATE_DOWN:
            mgr->cellm_route->has_L3 = false;
            cellm_ovsdb_cmu_disable_lte(mgr);
            if (mgr->cellm_state_info->cellm_failover_active)
            {
                cellm_revert_failover(mgr);
            }
            break;
        default:
            break;
    }
    LOGI("%s: state: %s, res %d", __func__, cellm_get_state_name(mgr->cellm_state), res);
}

/**
 * @brief Init the LTE modem and start the LTE daemon
 */
bool cellm_init_modem(void)
{
    return osn_cell_modem_init();
}

/**
 * @brief Update LTE state
 */
void cellm_set_state(enum cellm_state lte_state)
{
    cellm_mgr_t *mgr;

    mgr = cellm_get_mgr();

    if (mgr->cellm_state == lte_state)
    {
        LOGD("Same lte_state %s", cellm_get_state_name(lte_state));
        return;
    }
    else
    {
        LOGI("%s: Old Lte_State %s, New lte_state %s",
             __func__,
             cellm_get_state_name(mgr->cellm_state),
             cellm_get_state_name(lte_state));
    }
    mgr->cellm_state = lte_state;

    cellm_handle_lte_state_change(mgr);

    return;
}

/**
 * @brief stop the LTE daemon
 */
void cellm_fini_modem(void)
{
    osn_cell_stop_vendor_daemon();
}
