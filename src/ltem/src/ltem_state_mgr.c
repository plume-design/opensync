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
 * @brief Handle WAN state change
 */
static void
ltem_handle_wan_state_change(ltem_mgr_t *mgr)
{
    int res = 0;
    switch (mgr->wan_state) {
    case LTEM_WAN_STATE_UNKNOWN:
        break;
    case LTEM_WAN_STATE_DOWN:
        if (mgr->lte_state == LTEM_LTE_STATE_UP && (mgr->lte_config_info->lte_failover || mgr->lte_config_info->force_use_lte))
        {
            if (!ltem_add_lte_route(mgr))
            {
                mgr->lte_state_info->lte_failover = true;
            }
            else
            {
                mgr->lte_state_info->lte_failover = false;
                res = ltem_ovsdb_cmu_disable_lte(mgr);
            }
        }
        break;
    case LTEM_WAN_STATE_UP:
        if (mgr->lte_state == LTEM_LTE_STATE_UP && mgr->lte_state_info->lte_failover && !mgr->lte_config_info->force_use_lte)
        {
            ltem_restore_default_route(mgr);
            ltem_ovsdb_cmu_disable_lte(mgr);
            mgr->lte_state_info->lte_failover = false;
        }
        break;
    default:
        break;
    }
    LOGI("%s: state: %s, res %d", __func__, ltem_get_wan_state_name(mgr->wan_state), res);
}

/**
 * @brief Update WAN state
 */
void
ltem_set_wan_state(enum ltem_wan_state wan_state) {
    ltem_mgr_t *mgr;

    mgr = ltem_get_mgr();

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
        res = ltem_create_lte_route_table(mgr);
        if (res)
        {
            LOGI("%s: ltem_create_lte_route_table() failed", __func__);
            break;
        }

        res = ltem_ovsdb_cmu_create_lte(mgr);
        if (res)
        {
            LOGI("%s: ltem_ovsdb_cmu_create_lte: failed", __func__);
            ltem_set_lte_state(LTEM_LTE_STATE_DOWN);
        }
        break;
    case LTEM_LTE_STATE_DOWN:
        if (mgr->lte_state_info->lte_failover)
        {
            ltem_restore_default_route(mgr);
            ltem_ovsdb_cmu_disable_lte(mgr);
            mgr->lte_state_info->lte_failover = false;
        }
        break;
    default:
        break;
    }
    LOGI("%s: state: %s, res %d", __func__, ltem_get_lte_state_name(mgr->lte_state), res);
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
        LOGI("Same lte_state %s", ltem_get_lte_state_name(lte_state));
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
