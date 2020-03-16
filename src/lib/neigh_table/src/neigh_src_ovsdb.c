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
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <error.h>

#include "os_types.h"
#include "neigh_table.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_cache.h"
#include "schema.h"
#include "log.h"
#include "json_util.h"

static bool
lookup_ipv6_neigh_in_ovsdb(struct neighbour_entry *key)
{
    struct schema_IPv6_Neighbors    v6ip;
    json_t                         *jrows;
    json_t                         *where;
    int                             cnt;
    pjs_errmsg_t                    perr;
    char                            ipstr[INET6_ADDRSTRLEN] = {0};
    int                             err;

    err = getnameinfo((struct sockaddr *)key->ipaddr,
                      sizeof(struct sockaddr_storage),
                      ipstr, sizeof(ipstr),
                      0, 0, NI_NUMERICHOST);
    if (err < 0)
    {
        LOGD("%s: Failed to get the ip: err[%s]",__func__,strerror(err));
        return false;
    }

    where = ovsdb_tran_cond(OCLM_STR, "address", OFUNC_EQ, &ipstr);
    if (where == NULL)
    {
        LOGD("%s: Failed to get address column in IPv6_Neighbors table.", __func__);
        return false;
    }

    jrows = ovsdb_sync_select_where(SCHEMA_TABLE(IPv6_Neighbors), where);
    if (jrows == NULL)
    {
        LOGD("%s: Couldn't find the column in IPv6_Neighbors table.",__func__);
        json_decref(where);
        return false;
    }

    cnt = json_array_size(jrows);
    if (cnt == 0)
    {
        LOGD("%s: Couldn't find the ip[%s] in IPv6_Neighbors table.", __func__, ipstr);
        goto err_ipv6;
    }

    if (cnt > 1) LOGD("%s: Found duplicate entries, taking the first one.",__func__);

    if (!schema_IPv6_Neighbors_from_json(&v6ip, json_array_get(jrows, 0),
                                         false, perr))
    {
        LOGE("%s: Unable to parse IPv6_Neighbors column: %s", __func__, perr);
        goto err_ipv6;
    }

    err = hwaddr_aton(v6ip.hwaddr, key->mac->addr);
    if (err)
    {
        LOGE("%s: Invalid mac address[%s] retrieved",__func__, v6ip.hwaddr);
        goto err_ipv6;
    }

    json_decref(jrows);
    return true;

err_ipv6:
    json_decref(jrows);
    return false;
}

static bool
lookup_ipv4_neigh_in_ovsdb(struct neighbour_entry *key)
{
    struct schema_DHCP_leased_IP    dlip;
    json_t                         *jrows;
    json_t                         *where;
    int                             cnt;
    pjs_errmsg_t                    perr;
    char                            ipstr[INET6_ADDRSTRLEN] = {0};
    int                             err;

    err = getnameinfo((struct sockaddr *)key->ipaddr,
                      sizeof(struct sockaddr_storage),
                      ipstr, sizeof(ipstr),
                      0, 0, NI_NUMERICHOST);
    if (err < 0)
    {
        LOGD("%s: Failed to get the ip: err[%s]",__func__,strerror(err));
        return false;
    }

    where = ovsdb_tran_cond(OCLM_STR, "inet_addr", OFUNC_EQ, &ipstr);
    if (!where)
    {
        LOGD("%s: Failed to get inet_addr column in DHCP_leased_IP table.", __func__);
        return false;
    }

    jrows = ovsdb_sync_select_where(SCHEMA_TABLE(DHCP_leased_IP), where);
    if (jrows == NULL)
    {
        LOGD("%s: Couldn't find the column in DHCP_leased_IP table.",__func__);
        json_decref(where);
        return false;
    }

    cnt = json_array_size(jrows);
    if (cnt == 0)
    {
        LOGD("%s: Couldn't find the ip[%s] in DHCP_leased_IP table.", __func__, ipstr);
        goto err_ipv4;
    }

    if (cnt > 1) LOGD("%s: Found duplicate entries, taking the first one.",__func__);

    if (!schema_DHCP_leased_IP_from_json(&dlip, json_array_get(jrows, 0),
                                         false, perr))
    {
        LOGE("%s: Unable to parse DHCP_leased_IP column: %s", __func__, perr);
        goto err_ipv4;
    }

    err = hwaddr_aton(dlip.hwaddr, key->mac->addr);
    if (err)
    {
        LOGE("%s: Invalid mac address[%s] retrieved",__func__, dlip.hwaddr);
        goto err_ipv4;
    }

    json_decref(jrows);
    return true;

err_ipv4:
    json_decref(jrows);
    return false;

}

static bool
neigh_entry_to_ipv6_neighbor_schema(struct neighbour_entry *key,
                                    struct schema_IPv6_Neighbors *ipv6entry)
{
    int     err = -1;

    if (!key || !ipv6entry) return false;

    os_macaddr_t *mac = key->mac;
    struct sockaddr_storage *ipaddr = key->ipaddr;
    char  *ifname = key->ifname;

    if (!mac || !ipaddr || !ifname) return false;

    memset(ipv6entry, 0, sizeof(struct schema_IPv6_Neighbors));

    snprintf(ipv6entry->hwaddr,
             sizeof(ipv6entry->hwaddr),
             PRI_os_macaddr_t, FMT_os_macaddr_pt(key->mac));

    err = getnameinfo((struct sockaddr *)key->ipaddr,
                      sizeof(struct sockaddr_storage),
                      ipv6entry->address, sizeof(ipv6entry->address),
                      0, 0, NI_NUMERICHOST);
    if (err < 0)
    {
        LOGD("%s: Failed to get the ip: err[%s]",__func__,strerror(err));
        return false;
    }

    memcpy(ipv6entry->if_name, key->ifname, sizeof(ipv6entry->if_name));

    return true;
}


static bool
update_ipv6_neigh_in_ovsdb(struct neighbour_entry *key, bool remove)
{
    struct schema_IPv6_Neighbors    ipv6entry;
    pjs_errmsg_t    perr;
    json_t      *cond;
    json_t      *where;
    json_t      *row;
    bool         ret;

    if (!key) return false;

    where = json_array();

    if (!neigh_entry_to_ipv6_neighbor_schema(key, &ipv6entry))
    {
        LOGD("%s: Couldn't convert neighbor_entry to schema.", __func__);
        return false;
    }

    cond = ovsdb_tran_cond_single("address", OFUNC_EQ, ipv6entry.address);
    json_array_append_new(where, cond);

    if (key->ifname)
    {
        cond = ovsdb_tran_cond_single("if_name", OFUNC_EQ, key->ifname);
        json_array_append_new(where, cond);
    }

    if (remove)
    {
        ret = ovsdb_sync_delete_where(SCHEMA_TABLE(IPv6_Neighbors), where);
        if (!ret)
        {
            LOGE("%s: Failed to remove entry from IPv6_Neighbors.", __func__);
            json_decref(where);
            return false;
        }
        LOGD("%s: Removing ip[%s]-mac[%s] mapping in IPv6_Neighbors table."
             ,__func__, ipv6entry.address, ipv6entry.hwaddr);
    }
    else
    {
        row = schema_IPv6_Neighbors_to_json(&ipv6entry, perr);
        if (row == NULL)
        {
            LOGE("%s: Error convert schema structure to JSON.", __func__);
            return false;
        }

        if (!ovsdb_sync_upsert_where(SCHEMA_TABLE(IPv6_Neighbors), where, row, NULL))
        {
            LOGE("%s: Failed to upsert entry into IPv6_Neighbors.", __func__);
            return false;
        }
        LOGD("%s: Adding ip[%s]-mac[%s] mapping in IPv6_Neighbors table."
             ,__func__, ipv6entry.address, ipv6entry.hwaddr);
    }

    return true;
}

bool
update_ip_in_ovsdb_table(struct neighbour_entry *key, bool remove)
{
    bool rc = true;

    if (!key) return rc;

    if (key->ipaddr->ss_family == AF_INET6)
    {
        rc = update_ipv6_neigh_in_ovsdb(key, remove);
    }
    return rc;
}

bool
lookup_ip_in_ovsdb_table(struct neighbour_entry *key)
{
    bool rc = false;

    if (!key) return rc;

    if (key->ipaddr->ss_family == AF_INET)
    {
        rc = lookup_ipv4_neigh_in_ovsdb(key);
    }
    else if (key->ipaddr->ss_family == AF_INET6)
    {
        rc = lookup_ipv6_neigh_in_ovsdb(key);
    }
    return rc;
}
