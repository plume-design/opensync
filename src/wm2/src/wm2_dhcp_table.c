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

#include <stdlib.h>
#include <sys/wait.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <inttypes.h>
#include <jansson.h>

#include "json_util.h"
#include "evsched.h"
#include "schema.h"
#include "log.h"
#include "wm2.h"
#include "ovsdb.h"
#include "ovsdb_sync.h"

#include "target.h"

// Defines
#define MODULE_ID LOG_MODULE_ID_MAIN

// OVSDB constants
#define OVSDB_DHCP_TABLE            "DHCP_leased_IP"

static bool                         g_dhcp_table_init = false;

/******************************************************************************
 *  PROTECTED definitions
 *****************************************************************************/
static bool
wm2_dhcp_table_update(struct schema_DHCP_leased_IP       *dlip)
{
    pjs_errmsg_t        perr;
    json_t             *where, *row;
    bool                ret;

    LOGT("Updating DHCP lease '%s'",
         dlip->hwaddr);

    /* Skip deleting the empty entries at startup */
    if(!g_dhcp_table_init && (dlip->lease_time == 0))
    {
        return true;
    }

    if (dlip->lease_time == 0)  {
        // Released or expired lease... remove from OVSDB
        where = ovsdb_tran_cond(OCLM_STR, "hwaddr", OFUNC_EQ, str_tolower(dlip->hwaddr));
        ret = ovsdb_sync_delete_where(OVSDB_DHCP_TABLE, where);
        if (!ret) {
            LOGE("Updating DHCP lease %s (Failed to remove entry)",
                dlip->hwaddr);
            return false;
        }
        LOGN("Removed DHCP lease '%s' with '%s' '%s' '%d'",
             dlip->hwaddr, dlip->inet_addr, dlip->hostname, dlip->lease_time);

    }
    else {
        // New/active lease, upsert it into OVSDB
        where = ovsdb_tran_cond(OCLM_STR, "hwaddr", OFUNC_EQ, str_tolower(dlip->hwaddr));
        row   = schema_DHCP_leased_IP_to_json(dlip, perr);
        ret = ovsdb_sync_upsert_where(OVSDB_DHCP_TABLE, where, row, NULL);
        if (!ret) {
            LOGE("Updating DHCP lease %s (Failed to insert entry)",
                dlip->hwaddr);
            return false;
        }
        LOGN("Updated DHCP lease '%s' with '%s' '%s' '%d'",
             dlip->hwaddr, dlip->inet_addr, dlip->hostname, dlip->lease_time);

    }

    return true;
}

/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/
bool
wm2_dhcp_table_init(void)
{
    bool         ret;

    /* Register to DHCP lease changed ... */
    ret = target_dhcp_leased_ip_register(wm2_dhcp_table_update);
    if (false == ret) {
        return false;
    }

    g_dhcp_table_init = true;

    return true;
}
