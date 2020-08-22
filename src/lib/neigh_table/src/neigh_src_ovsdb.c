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

#include "os.h"
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

ovsdb_table_t table_DHCP_leased_IP;
ovsdb_table_t table_IPv4_Neighbors;
ovsdb_table_t table_IPv6_Neighbors;
ovsdb_table_t table_Wifi_Inet_State;

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

static const struct neigh_mapping_source source_map[] =
{
    {
        .source = "not set",
        .source_enum = NEIGH_SRC_NOT_SET,
    },
    {
        .source = "system neighbor table",
        .source_enum = NEIGH_TBL_SYSTEM,
    },
    {
        .source = "ovsdb dhcp lease",
        .source_enum = OVSDB_DHCP_LEASE,
    },
    {
        .source = "ovsdb ndp",
        .source_enum = OVSDB_NDP,
    },
    {
        .source = "ovsdb arp",
        .source_enum = OVSDB_ARP,
    },
    {
        .source = "ovsdb inet state",
        .source_enum = OVSDB_INET_STATE,
    },
};

/**
 * @brief return the source based on its enum value
 *
 * @param source_enum the source represented as an integer
 * @return a string pointer representing the source
 */
char *
neigh_table_get_source(int source_enum)
{
    const struct neigh_mapping_source *map;
    size_t nelems;
    size_t i;

    /* Walk the known sources */
    nelems = (sizeof(source_map) / sizeof(source_map[0]));
    map = source_map;
    for (i = 0; i < nelems; i++)
    {
        if (source_enum == map->source_enum) return map->source;
        map++;
    }

    return NULL;
}

static bool
neigh_entry_to_ipv4_neighbor_schema(struct neighbour_entry *key,
                                    struct schema_IPv4_Neighbors *ipv4entry)
{
    struct sockaddr_storage *ipaddr;
    os_macaddr_t *mac;
    char   *ifname;
    char   *source;
    int     err;

    if (!key || !ipv4entry) return false;

    ipaddr = key->ipaddr;
    ifname = key->ifname;
    mac = key->mac;

    if (!mac || !ipaddr || !ifname) return false;

    memset(ipv4entry, 0, sizeof(struct schema_IPv4_Neighbors));

    snprintf(ipv4entry->hwaddr,
             sizeof(ipv4entry->hwaddr),
             PRI_os_macaddr_t, FMT_os_macaddr_pt(key->mac));

    err = getnameinfo((struct sockaddr *)key->ipaddr,
                      sizeof(struct sockaddr_storage),
                      ipv4entry->address, sizeof(ipv4entry->address),
                      0, 0, NI_NUMERICHOST);
    if (err < 0)
    {
        LOGD("%s: Failed to get the ip: err[%s]", __func__, strerror(err));
        return false;
    }

    memcpy(ipv4entry->if_name, key->ifname, sizeof(ipv4entry->if_name));

    source = neigh_table_get_source(key->source);
    if (source == NULL) return true;

    memcpy(ipv4entry->source, source, sizeof(ipv4entry->source));

    return true;
}

static bool
update_ipv4_neigh_in_ovsdb(struct neighbour_entry *key, bool remove)
{
    struct schema_IPv4_Neighbors    ipv4entry;
    pjs_errmsg_t    perr;
    json_t      *cond;
    json_t      *where;
    json_t      *row;
    bool         ret;

    if (!key) return false;

    where = json_array();

    ret = neigh_entry_to_ipv4_neighbor_schema(key, &ipv4entry);
    if (!ret)
    {
        LOGD("%s: Couldn't convert neighbor_entry to schema.", __func__);
        return false;
    }

    cond = ovsdb_tran_cond_single("address", OFUNC_EQ, ipv4entry.address);
    json_array_append_new(where, cond);

    if (key->ifname)
    {
        cond = ovsdb_tran_cond_single("if_name", OFUNC_EQ, key->ifname);
        json_array_append_new(where, cond);
    }

    if (strlen(ipv4entry.source) != 0)
    {
        cond = ovsdb_tran_cond_single("source", OFUNC_EQ,
                                      (char *)ipv4entry.source);
        json_array_append_new(where, cond);
    }

    if (remove)
    {
        ret = ovsdb_sync_delete_where(SCHEMA_TABLE(IPv4_Neighbors), where);
        if (!ret)
        {
            LOGE("%s: Failed to remove entry from IPv4_Neighbors.", __func__);
            json_decref(where);
            return false;
        }
        LOGD("%s: Removing ip[%s]-mac[%s] mapping in IPv4_Neighbors table."
             ,__func__, ipv4entry.address, ipv4entry.hwaddr);
    }
    else
    {
        row = schema_IPv4_Neighbors_to_json(&ipv4entry, perr);
        if (row == NULL)
        {
            LOGE("%s: Error convert schema structure to JSON.", __func__);
            return false;
        }

        ret = ovsdb_sync_upsert_where(SCHEMA_TABLE(IPv4_Neighbors),
                                      where, row, NULL);
        if (!ret)
        {
            LOGE("%s: Failed to upsert entry into IPv4_Neighbors.", __func__);
            return false;
        }
        LOGD("%s: Adding ip[%s]-mac[%s] mapping in IPv4_Neighbors table."
             ,__func__, ipv4entry.address, ipv4entry.hwaddr);
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
    else if (key->ipaddr->ss_family == AF_INET)
    {
        rc = update_ipv4_neigh_in_ovsdb(key, remove);
    }
    return rc;
}


/**
 * @brief add or update a dhcp cache entry
 *
 * @param dhcp_lease the ovsdb dhcp info about the entry to add/update
 * If the entry's hw address is flagged as changed, update the cached entry.
 * Otherwise add the entry.
 */
static void
neigh_table_add_dhcp_entry(struct schema_DHCP_leased_IP *dhcp_lease)
{
    struct neighbour_entry entry;
    os_macaddr_t mac;
    uint8_t addr[4];
    bool rc;
    int ret;

    memset(&entry, 0, sizeof(entry));
    entry.af_family = AF_INET;
    ret = inet_pton(entry.af_family, dhcp_lease->inet_addr, addr);
    if (ret != 1)
    {
        LOGD("%s: conversion of %s failed", __func__, dhcp_lease->inet_addr);
        return;
    }

    entry.ip_tbl = addr;
    entry.source = OVSDB_DHCP_LEASE;
    hwaddr_aton(dhcp_lease->hwaddr, mac.addr);
    entry.mac = &mac;

    /* If the hw address field is marked as changed, update the cache entry */
    if (dhcp_lease->hwaddr_changed)
    {
        rc = neigh_table_cache_update(&entry);
        if (!rc) LOGD("%s: failed to update the dhcp entry", __func__);
    }
    else
    {
        rc = neigh_table_add_to_cache(&entry);
        if (!rc) LOGD("%s: failed to add the dhcp entry", __func__);
    }
}

/**
 * @brief delete a dhcp cache entry
 *
 * @param dhcp_lease the ovsdb dhcp info about the entry to delete
 */
static void
neigh_table_delete_dhcp_entry(struct schema_DHCP_leased_IP *dhcp_lease)
{
    struct neighbour_entry entry;
    uint8_t addr[4];
    int ret;

    memset(&entry, 0, sizeof(entry));
    entry.af_family = AF_INET;
    ret = inet_pton(entry.af_family, dhcp_lease->inet_addr, addr);
    if (ret != 1)
    {
        LOGD("%s: conversion of %s failed", __func__, dhcp_lease->inet_addr);
        return;
    }

    entry.ip_tbl = addr;
    entry.source = OVSDB_DHCP_LEASE;
    neigh_table_delete_from_cache(&entry);
}

/**
 * @brief add or update a dhcp cache entry
 *
 * @param old_rec the previous ovsdb dhcp info info about the entry
 *        to add/update
 * @param dhcp_lease the ovsdb dhcp info about the entry to add/update
 *
 * If the entry's ip address is flagged as changed, remove the old entry
 * and add a new one.
 * If the entry's hw address is flagged as changed, update the cached entry.
 * Otherwise add the entry.
 */
static void
neigh_table_update_dhcp_entry(struct schema_DHCP_leased_IP *old_rec,
                              struct schema_DHCP_leased_IP *dhcp_lease)
{
    if (dhcp_lease->inet_addr_changed)
    {
        /* Remove the old record from the cache */
        neigh_table_delete_dhcp_entry(old_rec);
    }

    /* Add/update the new record */
    neigh_table_add_dhcp_entry(dhcp_lease);
}

/**
 * @brief DHCP_leased_IP's event callbacks
 */
void
callback_DHCP_leased_IP(ovsdb_update_monitor_t *mon,
                        struct schema_DHCP_leased_IP *old_rec,
                        struct schema_DHCP_leased_IP *dhcp_lease)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        neigh_table_add_dhcp_entry(dhcp_lease);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        neigh_table_delete_dhcp_entry(dhcp_lease);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        neigh_table_update_dhcp_entry(old_rec, dhcp_lease);
        return;
    }
}


/**
 * @brief add or update a IPv4_Neighbor cache entry
 *
 * @param dhcp_lease the ovsdb IPv4_Neighbor info about the entry to add/update
 * If any of the entry's field but its IP address is marked as changed,
 * consider the entry as an update.
 * Otherwise add the entry.
 */
static void
neigh_table_add_v4_entry(struct schema_IPv4_Neighbors *neigh)
{
    struct neighbour_entry entry;
    os_macaddr_t mac;
    uint8_t addr[4];
    bool update;
    bool rc;
    int ret;

    memset(&entry, 0, sizeof(entry));
    entry.af_family = AF_INET;
    ret = inet_pton(entry.af_family, neigh->address, addr);
    if (ret != 1)
    {
        LOGD("%s: conversion of %s failed", __func__, neigh->address);
        return;
    }
    entry.ip_tbl = addr;
    entry.source = OVSDB_ARP;
    hwaddr_aton(neigh->hwaddr, mac.addr);
    entry.mac = &mac;

    update = neigh->hwaddr_changed;
    update |= neigh->source_changed;
    update |= neigh->if_name_changed;
    if (update)
    {
        rc = neigh_table_cache_update(&entry);
        if (!rc) LOGD("%s: cache update failed", __func__);
    }
    else
    {
        rc = neigh_table_add_to_cache(&entry);
        if (!rc) LOGD("%s: cache addition failed", __func__);
    }
}

/**
 * @brief delete a IPv4_Neighbor cached entry
 *
 * @brief neigh the IPv4_Neighbor info about the entry to delete
 */
static void
neigh_table_delete_v4_entry(struct schema_IPv4_Neighbors *neigh)
{
    struct neighbour_entry entry;
    os_macaddr_t mac;
    uint8_t addr[4];
    int ret;

    memset(&entry, 0, sizeof(entry));
    entry.af_family = AF_INET;
    ret = inet_pton(entry.af_family, neigh->address, addr);
    if (ret != 1)
    {
        LOGD("%s: conversion of %s failed", __func__, neigh->address);
        return;
    }
    entry.ip_tbl = addr;
    entry.source = OVSDB_ARP;
    hwaddr_aton(neigh->hwaddr, mac.addr);
    entry.mac = &mac;
    neigh_table_delete_from_cache(&entry);
}

/**
 * @brief add or update a IPv4_Neighbor cache entry
 *
 * @param old_rec the previous ovsdb IPv4_Neighbor info about the entry
 *        to add/update
 * @param v4_neigh the ovsdb IPv4_Neighbor info about the entry to add/update
 *
 * If the entry's ip address is flagged as changed, remove the old entry
 * and add a new one.
 * Else update the entry.
 */
static void
neigh_table_update_v4_entry(struct schema_IPv4_Neighbors *old_rec,
                            struct schema_IPv4_Neighbors *v4_neigh)
{
    if (v4_neigh->address_changed)
    {
        /* Remove the old record from the cache */
        neigh_table_delete_v4_entry(old_rec);
    }

    /* Add/update the new record */
    neigh_table_add_v4_entry(v4_neigh);
}

/**
 * @brief IPv4_Neighbors' event callbacks
 */
void
callback_IPv4_Neighbors(ovsdb_update_monitor_t *mon,
                        struct schema_IPv4_Neighbors *old_rec,
                        struct schema_IPv4_Neighbors *v4_neigh)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        neigh_table_add_v4_entry(v4_neigh);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        neigh_table_delete_v4_entry(v4_neigh);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        neigh_table_update_v4_entry(old_rec, v4_neigh);
        return;
    }
}

/**
 * @brief add or update a IPv4_Neighbor cache entry
 *
 * @param dhcp_lease the ovsdb IPv4_Neighbor info about the entry to add/update
 * If any of the entry's field but its IP address is marked as changed,
 * consider the entry as an update.
 * Otherwise add the entry.
 */
static void
neigh_table_add_v6_entry(struct schema_IPv6_Neighbors *neigh)
{
    struct neighbour_entry entry;
    os_macaddr_t mac;
    uint8_t addr[16];
    bool update;
    bool rc;
    int ret;

    memset(&entry, 0, sizeof(entry));
    entry.af_family = AF_INET6;
    ret = inet_pton(entry.af_family, neigh->address, addr);
    if (ret != 1)
    {
        LOGD("%s: conversion of %s failed", __func__, neigh->address);
        return;
    }
    entry.ip_tbl = addr;
    entry.source = OVSDB_NDP;
    hwaddr_aton(neigh->hwaddr, mac.addr);
    entry.mac = &mac;

    update = neigh->hwaddr_changed;
    update |= neigh->if_name_changed;
    if (update)
    {
        rc = neigh_table_cache_update(&entry);
        if (!rc) LOGD("%s: cache update failed", __func__);
    }
    else
    {
        rc = neigh_table_add_to_cache(&entry);
        if (!rc) LOGD("%s: cache addition failed", __func__);
    }
}

/**
 * @brief delete a IPv6_Neighbor cached entry
 *
 * @brief neigh the IPv6_Neighbor info about the entry to delete
 */
static void
neigh_table_delete_v6_entry(struct schema_IPv6_Neighbors *neigh)
{
    struct neighbour_entry entry;
    os_macaddr_t mac;
    uint8_t addr[16];
    int ret;

    memset(&entry, 0, sizeof(entry));
    entry.af_family = AF_INET6;
    ret = inet_pton(entry.af_family, neigh->address, addr);
    if (ret != 1)
    {
        LOGD("%s: conversion of %s failed", __func__, neigh->address);
        return;
    }
    entry.ip_tbl = addr;
    entry.source = OVSDB_NDP;
    hwaddr_aton(neigh->hwaddr, mac.addr);
    entry.mac = &mac;
    neigh_table_delete_from_cache(&entry);
}

/**
 * @brief add or update a IPv6_Neighbor cache entry
 *
 * @param old_rec the previous ovsdb IPv6_Neighbor info about the entry
 *        to add/update
 * @param v6_neigh the ovsdb IPv6_Neighbor info about the entry to add/update
 *
 * If the entry's ip address is flagged as changed, remove the old entry
 * and add a new one.
 * Else update the entry.
 */
static void
neigh_table_update_v6_entry(struct schema_IPv6_Neighbors *old_rec,
                            struct schema_IPv6_Neighbors *v6_neigh)
{
    if (v6_neigh->address_changed)
    {
        /* Remove the old record from the cache */
        neigh_table_delete_v6_entry(old_rec);
    }

    /* Add/update the new record */
    neigh_table_add_v6_entry(v6_neigh);
}

/**
 * @brief IPv6_Neighbors' event callbacks
 */
void
callback_IPv6_Neighbors(ovsdb_update_monitor_t *mon,
                        struct schema_IPv6_Neighbors *old_rec,
                        struct schema_IPv6_Neighbors *v6_neigh)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        neigh_table_add_v6_entry(v6_neigh);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        neigh_table_delete_v6_entry(v6_neigh);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        neigh_table_update_v6_entry(old_rec, v6_neigh);
        return;
    }
}

/**
 * @brief add or update a inet state entry
 *
 * @param inet_info the ovsdb inet info about the entry to add/update
 * If the entry's hw address is flagged as changed, update the cached entry.
 * Otherwise add the entry.
 */
static void
neigh_table_add_inet_entry(struct schema_Wifi_Inet_State *inet_info)
{
    struct neighbour_entry entry;
    os_macaddr_t mac;
    uint8_t addr[4];
    uint32_t *ipaddr = (uint32_t *)addr;
    bool rc;
    int ret;

    memset(&entry, 0, sizeof(entry));
    entry.af_family = AF_INET;
    ret = inet_pton(entry.af_family, inet_info->inet_addr, addr);
    if (ret != 1)
    {
        LOGD("%s: conversion of %s failed", __func__, inet_info->inet_addr);
        return;
    }

    if (*ipaddr == 0) return;

    entry.ip_tbl = addr;
    entry.source = OVSDB_INET_STATE;
    hwaddr_aton(inet_info->hwaddr, mac.addr);
    entry.mac = &mac;

    /* If the hw address field is marked as changed, update the cache entry */
    if (inet_info->hwaddr_changed)
    {
        rc = neigh_table_cache_update(&entry);
        if (!rc) LOGD("%s: failed to update the inet state entry", __func__);
    }
    else
    {
        rc = neigh_table_add_to_cache(&entry);
        if (!rc) LOGD("%s: failed to add the inet state entry", __func__);
    }
}

/**
 * @brief delete a inet state entry
 *
 * @param inet_info the ovsdb inet info about the entry to delete
 */
static void
neigh_table_delete_inet_entry(struct schema_Wifi_Inet_State *inet_info)
{
    struct neighbour_entry entry;
    uint8_t addr[4];
    uint32_t *ipaddr = (uint32_t *)addr;
    int ret;

    memset(&entry, 0, sizeof(entry));
    entry.af_family = AF_INET;
    ret = inet_pton(entry.af_family, inet_info->inet_addr, addr);
    if (ret != 1)
    {
        LOGD("%s: conversion of %s failed", __func__, inet_info->inet_addr);
        return;
    }

    if (*ipaddr == 0) return;

    entry.ip_tbl = addr;
    entry.source = OVSDB_INET_STATE;
    neigh_table_delete_from_cache(&entry);
}

/**
 * @brief add or update a inet state entry
 *
 * @param old_rec the previous ovsdb inet info info about the entry
 *        to add/update
 * @param inet_info the ovsdb inet info about the entry to add/update
 *
 * If the entry's ip address is flagged as changed, remove the old entry
 * and add a new one.
 * If the entry's hw address is flagged as changed, update the cached entry.
 * Otherwise add the entry.
 */
static void
neigh_table_update_inet_entry(struct schema_Wifi_Inet_State *old_rec,
                              struct schema_Wifi_Inet_State *inet_info)
{
    if (inet_info->inet_addr_changed)
    {
        /* Remove the old record from the cache */
        neigh_table_delete_inet_entry(old_rec);
    }

    /* Add/update the new record */
    neigh_table_add_inet_entry(inet_info);
}

/**
 * @brief DHCP_leased_IP's event callbacks
 */
void
callback_Wifi_Inet_State(ovsdb_update_monitor_t *mon,
                        struct schema_Wifi_Inet_State *old_rec,
                        struct schema_Wifi_Inet_State *inet_info)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        neigh_table_add_inet_entry(inet_info);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        neigh_table_delete_inet_entry(inet_info);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        neigh_table_update_inet_entry(old_rec, inet_info);
        return;
    }
}

/**
 * @brief registers to ovsdb tables updates
 */
void
neigh_src_init(void)
{
    OVSDB_TABLE_INIT_NO_KEY(DHCP_leased_IP);
    OVSDB_TABLE_INIT_NO_KEY(IPv4_Neighbors);
    OVSDB_TABLE_INIT_NO_KEY(IPv6_Neighbors);
    OVSDB_TABLE_INIT_NO_KEY(Wifi_Inet_State);

    OVSDB_TABLE_MONITOR(DHCP_leased_IP, false);
    OVSDB_TABLE_MONITOR(IPv4_Neighbors, false);
    OVSDB_TABLE_MONITOR(IPv6_Neighbors, false);
    OVSDB_TABLE_MONITOR(Wifi_Inet_State, false);
}
