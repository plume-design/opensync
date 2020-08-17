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
#include "ovsdb_table.h"

#include "wano.h"

bool wano_connection_manager_uplink_flush(void)
{
    int rc;

    ovsdb_table_t table_Connection_Manager_Uplink;
    OVSDB_TABLE_INIT(Connection_Manager_Uplink, if_name);

    /* Passing an empty array as the where statement effectively deletes all rows */
    rc = ovsdb_table_delete_where(&table_Connection_Manager_Uplink, json_array());
    return rc < 0;
}

bool wano_connection_manager_uplink_delete(const char *ifname)
{
    ovsdb_table_t table_Connection_Manager_Uplink;

    OVSDB_TABLE_INIT(Connection_Manager_Uplink, if_name);

    return ovsdb_table_delete_simple(
            &table_Connection_Manager_Uplink,
            "if_name",
            ifname);
}

bool wano_connection_manager_uplink_update(
        const char *ifname,
        struct wano_connection_manager_uplink_args *args)
{
    ovsdb_table_t table_Connection_Manager_Uplink;
    struct schema_Connection_Manager_Uplink conn_up;

    memset(&conn_up, 0, sizeof(conn_up));
    conn_up._partial_update = true;

    SCHEMA_SET_STR(conn_up.if_name, ifname);

    if (args->if_type != NULL)
    {
        SCHEMA_SET_STR(conn_up.if_type, args->if_type);
    }

    if (args->priority != 0)
    {
        SCHEMA_SET_INT(conn_up.priority, args->priority);
    }

    if (args->has_L2 == WANO_TRI_TRUE)
    {
        SCHEMA_SET_INT(conn_up.has_L2, true);
    }
    else if (args->has_L2 == WANO_TRI_FALSE)
    {
        SCHEMA_SET_INT(conn_up.has_L2, false);
    }

    if (args->has_L3 == WANO_TRI_TRUE)
    {
        SCHEMA_SET_INT(conn_up.has_L3, true);
    }
    else if (args->has_L3 == WANO_TRI_FALSE)
    {
        SCHEMA_SET_INT(conn_up.has_L3, false);
    }

    OVSDB_TABLE_INIT(Connection_Manager_Uplink, if_name);

    return ovsdb_table_upsert_simple(
            &table_Connection_Manager_Uplink,
            "if_name",
            (char *)ifname,
            &conn_up,
            false);
}
