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
#include "log.h"
#include "ds_tree.h"
#include "neigh_table.h"
#include "nf_utils.h"

static struct neigh_table_mgr
mgr =
{
    .initialized = false,
};

/**
 * @brief compare neighbor table entries.
 *
 * @return 0 on match and greater than 1 if no match.
 */

int neigh_table_cmp(void *a, void *b)
{
    return memcmp(a, b, sizeof(struct sockaddr_storage));
}

struct neigh_table_mgr
*neigh_table_get_mgr(void)
{
    return &mgr;
}


/**
 * @brief initialize neighbor_table handle.
 *
 * receive none
 *
 * @return 0 for success and 1 for failure .
 */
int neigh_table_init(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();

    if (mgr->initialized) return 0;

    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;
    mgr->lookup_ovsdb_tables = lookup_ip_in_ovsdb_table;
    mgr->lookup_kernel_entry = lookup_entry_in_kernel;

    ds_tree_init(&mgr->neigh_table, neigh_table_cmp, struct neighbour_entry, entry_node);
    mgr->initialized = true;
    return 0;
}

void free_neigh_entry(struct neighbour_entry *entry)
{
    if (!entry) return;

    free(entry->ipaddr);
    free(entry->mac);
    free(entry->ifname);
    free(entry);
}

/**
 * @brief cleanup allocatef memody.
 *
 * receive none
 *
 * @return void.
 */
void neigh_table_cleanup(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry *entry_node;
    struct neighbour_entry *remove_node;
    ds_tree_t           *tree;

    if (!mgr->initialized) return;

    tree = &mgr->neigh_table;
    entry_node = ds_tree_head(tree);
    while (entry_node != NULL)
    {
        remove_node = entry_node;

        if (mgr->update_ovsdb_tables)
        {
            mgr->update_ovsdb_tables(remove_node, true);
        }

        entry_node = ds_tree_next(tree, entry_node);
        ds_tree_remove(tree, remove_node);
        free_neigh_entry(remove_node);
    }
    mgr->initialized = false;
    return;
}

bool neigh_table_add(struct neighbour_entry *to_add)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry *entry;

    if (!to_add) return false;

    entry = ds_tree_find(&mgr->neigh_table, to_add->ipaddr);
    if (entry)
    {
        LOGD("%s: entry already exists", __func__);
        return true;
    }

    entry = calloc(sizeof(struct neighbour_entry), 1);
    if (!entry) return false;

    entry->ipaddr = calloc(sizeof(struct sockaddr_storage), 1);
    if (entry->ipaddr == NULL) goto err_free_entry;

    entry->mac = calloc(sizeof(os_macaddr_t), 1);
    if (entry->mac == NULL) goto err_free_ipaddr;

    if (to_add->ifname != NULL)
    {
        entry->ifname = strdup(to_add->ifname);
        if (entry->ifname == NULL) goto err_free_mac;
    }

    entry->ipaddr->ss_family = to_add->ipaddr->ss_family;
    memcpy(entry->ipaddr, to_add->ipaddr, sizeof(struct sockaddr_storage));
    memcpy(entry->mac, to_add->mac, sizeof(os_macaddr_t));
    entry->cache_valid_ts = time(NULL);

    ds_tree_insert(&mgr->neigh_table, entry, entry->ipaddr);

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE)) print_neigh_table();

    // Update ovsdb tables if required.
    if (mgr->update_ovsdb_tables &&
        !mgr->update_ovsdb_tables(entry, false))
    {
        LOGD("%s: Failed to add entry into ovsdb table.", __func__);
        return false;
    }

    return true;

err_free_mac:
    free(entry->mac);

err_free_ipaddr:
    free(entry->ipaddr);

err_free_entry:
    free(entry);

    return false;
}

void neigh_table_delete(struct neighbour_entry *to_del)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry *lookup;

    if (!to_del) return;

    lookup = ds_tree_find(&mgr->neigh_table, to_del->ipaddr);

    // Update ovsdb tables if required.
    if (mgr->update_ovsdb_tables &&
        !mgr->update_ovsdb_tables(lookup, true))
    {
        LOGD("%s: Failed to delete entry from ovsdb table.", __func__);
        return;
    }

    ds_tree_remove(&mgr->neigh_table, lookup);

    free_neigh_entry(lookup);

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE)) print_neigh_table();

    return;
}

bool neigh_table_cache_update(struct neighbour_entry *entry)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry *lookup;

    if (!entry) return false;

    lookup = ds_tree_find(&mgr->neigh_table, entry->ipaddr);

    // We dont have it. Add it.
    if (!lookup)
    {
        if (!neigh_table_add(entry))
        {
            LOGD("%s: Couldn't cache the entry.", __func__);
            return false;
        }
        return true;
    }

    memcpy(lookup->mac, entry->mac, sizeof(os_macaddr_t));
    free(lookup->ifname);
    if (entry->ifname != NULL)
    {
        lookup->ifname = strdup(entry->ifname);
        if (lookup->ifname == NULL) return false;
    }

    lookup->cache_valid_ts = time(NULL);

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE)) print_neigh_table();

    // Update ovsdb tables if required.
    if (mgr->update_ovsdb_tables &&
        !mgr->update_ovsdb_tables(lookup, false))
    {
        LOGD("%s: Failed to update entry in ovsdb table.", __func__);
        return false;
    }

    return true;
}

bool neigh_table_cache_lookup(struct neighbour_entry *key)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry *entry;
    time_t now;

    if (!key) return false;

    entry = ds_tree_find(&mgr->neigh_table, key->ipaddr);
    if (!entry) return false;

    now = time(NULL);

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE)) print_neigh_table();

    if (entry && ((now - entry->cache_valid_ts) < NEIGH_CACHE_INTERVAL))
    {
        memcpy(key->mac, entry->mac, sizeof(os_macaddr_t));
        return true;
    }
    return false;
}

/**
 * @brief lookup for a neighbor table entry.
 *
 * @return true if found and false if not.
 */
bool neigh_table_lookup(struct sockaddr_storage *ip_in, os_macaddr_t *mac_out)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry key;

    if (!ip_in || !mac_out) return false;

    key.ipaddr = ip_in;
    key.mac    = mac_out;
    key.ifname = NULL;

    // Lookup in cache.
    if (neigh_table_cache_lookup(&key)) return true;

    // Lookup in ovsdb tables.
    if (mgr->lookup_ovsdb_tables &&
        mgr->lookup_ovsdb_tables(&key))
    {
        if (!neigh_table_cache_update(&key))
        {
            LOGD("%s: Failed to update the cache with ovsdb data.", __func__);
        }
        return true;
    }

    // Lookup in kernel neigh table.
    if (mgr->lookup_kernel_entry &&
        mgr->lookup_kernel_entry(&key))
    {
        if (!neigh_table_cache_update(&key))
        {
            LOGD("%s: Failed to update the cache with kernel data.", __func__);
        }
        return true;
    }

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
        print_neigh_table();
    return false;
}

void print_neigh_table(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry *entry_node = NULL;
    char                    ipstr[INET6_ADDRSTRLEN] = {0};
    time_t                  now;

    LOGT("%s: neigh_table dump", __func__);
    now = time(NULL);
    ds_tree_foreach(&mgr->neigh_table, entry_node)
    {
        getnameinfo((struct sockaddr *)entry_node->ipaddr,
                    sizeof(struct sockaddr_storage), ipstr, sizeof(ipstr),
                    0, 0, NI_NUMERICHOST);
        LOGT("entry_age: %lu ip %s, mac "PRI_os_macaddr_lower_t " if_name %s",
             (now - entry_node->cache_valid_ts),
             ipstr,FMT_os_macaddr_pt(entry_node->mac),
             entry_node->ifname ? entry_node->ifname : "empty");
    }
    LOGT("=====END=====");
}
