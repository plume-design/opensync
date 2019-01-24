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


#include "schema.h"
#include "log.h"
#include "nm2.h"
#include "ovsdb.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"

#include "target.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

static ovsdb_table_t table_Wifi_Route_State;

static target_route_state_cb_t nm2_route_state_update;
static void nm2_route_state_clear(ds_dlist_t *rts_list);

void nm2_route_state_update(struct schema_Wifi_Route_State *rts)
{
    json_t *where;
    json_t *cond;

    LOG(INFO, "nm2: Updating Wifi_Route_State: if_name=%s dest_addr=%s dest_mask=%s gateway=%s gateway_hwaddr=%s",
            rts->if_name,
            rts->dest_addr,
            rts->dest_mask,
            rts->gateway,
            rts->gateway_hwaddr);

    /* OVSDB transaction where multi condition */
    where = json_array();

    cond = ovsdb_tran_cond_single("if_name", OFUNC_EQ, rts->if_name);
    json_array_append_new(where, cond);

    cond = ovsdb_tran_cond_single("dest_addr", OFUNC_EQ, rts->dest_addr);
    json_array_append_new(where, cond);

    cond = ovsdb_tran_cond_single("dest_mask", OFUNC_EQ, rts->dest_mask);
    json_array_append_new(where, cond);

    cond = ovsdb_tran_cond_single("gateway", OFUNC_EQ, rts->gateway);
    json_array_append_new(where, cond);

    if (rts->_update_type != OVSDB_UPDATE_DEL)
    {
        if (!ovsdb_table_upsert_where(&table_Wifi_Route_State, where, rts, false))
        {
            LOG(ERR, "nm2: Error updating Wifi_Route_State (upsert).");
        }
    }
    else
    {
        if (!ovsdb_table_delete_where(&table_Wifi_Route_State, where))
        {
            LOG(ERR, "nm2: Error updating Wifi_Route_State (delete).");
        }

    }
}

void nm2_route_state_clear(ds_dlist_t *rts_list)
{
    ds_dlist_iter_t iter;
    target_route_state_init_t *rts;

    for (rts = ds_dlist_ifirst(&iter, rts_list);
         rts != NULL;
         rts = ds_dlist_inext(&iter))
    {
        ds_dlist_iremove(&iter);
        free(rts);
    }
}

bool nm2_route_state_init(void)
{
    bool ret;
    ds_dlist_t rts_list;
    target_route_state_init_t *rts;

    static bool rtstate_init = false;

    if (rtstate_init) return true;

    /* Initialize OVSDB tables */
    OVSDB_TABLE_INIT_NO_KEY(Wifi_Route_State);

    if (!target_route_state_init(&rts_list))
    {
        LOGW("nm2: Initializing route state (not found)");
        return false;
    }

    ds_dlist_foreach(&rts_list, rts)
    {
        ovsdb_table_upsert(&table_Wifi_Route_State, &rts->rstate, false);
    }

    nm2_route_state_clear(&rts_list);

    /* Register to DHCP lease changed ... */
    ret = target_route_state_register(nm2_route_state_update);
    if (!ret)
    {
        LOG(ERR, "nm2: Error initializing route state.");
        return false;
    }

    rtstate_init = true;

    return true;
}
