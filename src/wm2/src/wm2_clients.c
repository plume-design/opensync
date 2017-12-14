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
#include "ds_list.h"
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
#define OVSDB_CLIENTS_TABLE                 "Wifi_Associated_Clients"
#define OVSDB_CLIENTS_PARENT                "Wifi_VIF_State"

#define OVSDB_CLIENTS_PARENT_COL            "associated_clients"

/******************************************************************************
 *  PRIVATE definitions
 *****************************************************************************/
static bool
wm2_clients_update(struct schema_Wifi_Associated_Clients  *schema, char* ifname, bool   status)
{
    json_t                                 *where;
    json_t                                 *pwhere;
    json_t                                 *row;
    bool                                    ret;

    LOGN("Client '%s' %s %s",
          schema->mac,
          status ? "connected on" : "disconnected",
          ifname);

    pwhere = ovsdb_tran_cond(OCLM_STR, "if_name", OFUNC_EQ,
            ifname);
    if (status) {
        // Insert client
        row = json_object();
        json_object_set_new(row, "mac", json_string(schema->mac));
        json_object_set_new(row, "state", json_string(schema->state));

        where = ovsdb_where_simple("mac", schema->mac);

        ret = ovsdb_sync_upsert_with_parent(OVSDB_CLIENTS_TABLE,
                where,
                row,
                NULL,
                OVSDB_CLIENTS_PARENT,
                pwhere,
                OVSDB_CLIENTS_PARENT_COL);
        if (!ret) {
            LOGE("Updating client %s (Failed to insert entry)",
                schema->mac);
        }
    }
    else  {
        // Remove client
        ret = ovsdb_delete_with_parent_s(OVSDB_CLIENTS_TABLE,
                ovsdb_tran_cond(OCLM_STR,
                    "mac",
                    OFUNC_EQ,
                    (char *)schema->mac),
                OVSDB_CLIENTS_PARENT,
                pwhere,
                OVSDB_CLIENTS_PARENT_COL);
        if (!ret) {
            LOGE("Updating client %s (Failed to remove entry)",
                schema->mac);
        }
    }

    return true;
}

/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/
bool
wm2_clients_init(char *if_name)
{
    if (!if_name) {
        LOGE("Initializing clients (input validation failed)" );
        return false;
    }

    if (false == target_clients_register(if_name, wm2_clients_update)) {
        return false;
    }

    return true;
}
