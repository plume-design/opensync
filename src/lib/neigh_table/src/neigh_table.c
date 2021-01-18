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
#include <net/if.h>

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
    struct neighbour_entry *e_a = (struct neighbour_entry *)a;
    struct neighbour_entry *e_b = (struct neighbour_entry *)b;
    size_t len;
    uint8_t *i_a;
    uint8_t *i_b;
    int fdiff;
    int idiff;
    size_t i;

    /* Compare sources */
    if ((e_a->source & e_b->source) == 0)
    {
        return (int)(e_a->source - e_b->source);
    }

    /* Compare af families */
    fdiff = (e_a->af_family - e_b->af_family);
    if (fdiff != 0) return fdiff;

    len = (e_a->af_family == AF_INET) ? 4 : 16;
    i_a = e_a->ip_tbl;
    i_b = e_b->ip_tbl;
    for (i = 0; i != len; i++)
    {
        idiff = (*i_a - *i_b);
        if (idiff) return idiff;
        i_a++;
        i_b++;
    }

    return 0;
}

int neigh_intf_cmp(void *a, void *b)
{
    int *idx_a = a;
    int *idx_b = b;

    return (*idx_a - *idx_b);
}

struct neigh_table_mgr
*neigh_table_get_mgr(void)
{
    return &mgr;
}


void process_neigh_event(struct nf_neigh_info *neigh_info)
{
    char ipstr[INET6_ADDRSTRLEN] = { 0 };
    struct neighbour_entry entry;
    struct neigh_interface *intf;
    os_macaddr_t mac = { { 0 } };
    char ifname[32] = { 0 };
    os_macaddr_t *pmac;
    char *ifn;
    bool rc;
    time_t now;

    pmac = (neigh_info->hwaddr != NULL) ? neigh_info->hwaddr : &mac;
    inet_ntop(neigh_info->af_family, neigh_info->ipaddr, ipstr, sizeof(ipstr));
    ifn = if_indextoname(neigh_info->ifindex, ifname);
    LOGT("%s: Found mac "  PRI_os_macaddr_lower_t
         " for ip[%s] in state %s: %s, %s, interface: %s",
         __func__, FMT_os_macaddr_pt(pmac), ipstr,
         nf_util_get_str_state(neigh_info->state),
         neigh_info->add ? "add" : "no_add",
         neigh_info->delete ? "del" : "no_del",
         (ifn != NULL) ? ifn : "none");
    memset(&entry, 0, sizeof(entry));
    now = time(NULL);
    entry.af_family = neigh_info->af_family;
    entry.ip_tbl = neigh_info->ipaddr;
    entry.source = neigh_info->source;
    entry.mac = neigh_info->hwaddr;
    entry.ifindex = neigh_info->ifindex;
    entry.cache_valid_ts = now;
    if (neigh_info->add)
    {
        rc = neigh_table_add_to_cache(&entry);
        if (!rc)
        {
            LOGD("%s: add to cache failed", __func__);
            return;
        }

        intf = neigh_table_get_intf(entry.ifindex);
        if (intf != NULL) intf->entries_count++;
    }
    else if (neigh_info->delete)
    {
        neigh_table_delete_from_cache(&entry);
        intf = neigh_table_lookup_intf(entry.ifindex);
        if (intf != NULL) intf->entries_count--;
    }
}

void process_link_event(struct nf_neigh_info *neigh_info)
{
    struct neighbour_entry *remove_node;
    struct neighbour_entry *entry_node;
    struct neigh_interface *intf;
    struct neigh_table_mgr *mgr;
    char ifname[32] = { 0 };
    ds_tree_t *tree;
    char *ifn;

    mgr = neigh_table_get_mgr();

    ifn = if_indextoname(neigh_info->ifindex, ifname);
    LOGT("%s: link ifindex %d event %d (intf %s)", __func__,
         neigh_info->ifindex, neigh_info->event,
         (ifn != NULL) ? ifn : "none");

    /* Remove neighbor cache entries bound to the interface if any */
    intf = neigh_table_lookup_intf(neigh_info->ifindex);
    if (intf == NULL) return;
    if (intf->entries_count <= 0) return;

    tree = &mgr->neigh_table;
    entry_node = ds_tree_head(tree);
    while (entry_node != NULL)
    {
        bool remove;

        remove = (entry_node->source == neigh_info->source);
        remove &= (entry_node->ifindex == neigh_info->ifindex);
        remove_node = entry_node;
        entry_node = ds_tree_next(tree, entry_node);
        if (remove)
        {
            LOGT("%s: removing entry: ", __func__);
            print_neigh_entry(remove_node);
            ds_tree_remove(tree, remove_node);
            free_neigh_entry(remove_node);
        }
    }

    /* Remove interface from tree interfaces */
    tree = &mgr->interfaces;
    ds_tree_remove(tree, intf);

    return;
}

/**
 * @brief lookup interface context.
 *
 * @param ifindex the interface system index
 *
 * Look up an interface context
 */
struct neigh_interface * neigh_table_lookup_intf(int ifindex)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neigh_interface *intf;

    mgr = neigh_table_get_mgr();

    intf = ds_tree_find(&mgr->interfaces, &ifindex);
    return intf;
}

/**
 * @brief lookup/create interface context.
 *
 * @param ifindex the interface system index
 *
 * Looks up an interface context. If not found, create one.
 */
struct neigh_interface * neigh_table_get_intf(int ifindex)
{
    struct neigh_interface *intf;
    struct neigh_table_mgr *mgr;

    mgr = neigh_table_get_mgr();

    intf = neigh_table_lookup_intf(ifindex);
    if (intf != NULL) return intf;

    intf = calloc(1, sizeof(*intf));
    if (intf == NULL) return NULL;

    intf->ifindex = ifindex;
    ds_tree_insert(&mgr->interfaces, intf, &intf->ifindex);

    return intf;
}

void neigh_table_init_monitor(struct ev_loop *loop,
                              bool system_event, bool ovsdb_event)
{
    int rc;

    if (loop == NULL) return;

    if (system_event)
    {
        struct nf_neigh_settings nf_settings;

        memset(&nf_settings, 0, sizeof(nf_settings));
        nf_settings.loop = loop;
        nf_settings.neigh_cb = process_neigh_event;
        nf_settings.link_cb = process_link_event;
        nf_settings.source = NEIGH_TBL_SYSTEM;
        rc = nf_neigh_init(&nf_settings);
        LOGI("%s: system event monitoring %s", __func__,
             rc ? "failed" : "enabled");
    }
    if (ovsdb_event) neigh_src_init();
}


void neigh_table_init_manager(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();

    if (mgr->initialized) return;

    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;

    ds_tree_init(&mgr->neigh_table, neigh_table_cmp,
                 struct neighbour_entry, entry_node);

    ds_tree_init(&mgr->interfaces, neigh_intf_cmp,
                 struct neigh_interface, intf_node);
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

    neigh_table_init_manager();

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


void neigh_table_cache_cleanup(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry *remove_node;
    struct neigh_interface *remove_intf;
    struct neighbour_entry *entry_node;
    struct neigh_interface *intf_node;

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

    tree = &mgr->interfaces;
    intf_node = ds_tree_head(tree);
    while (intf_node != NULL)
    {
        remove_intf = intf_node;
        intf_node = ds_tree_next(tree, intf_node);
        ds_tree_remove(tree, remove_intf);
        free(intf_node);
    }
    return;
}
/**
 * @brief cleanup allocated memory.
 */
void neigh_table_cleanup(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();

    neigh_table_cache_cleanup();
    mgr->initialized = false;

    return;
}

/**
 * @brief set fast lookup fields of a neighbour entry
 *
 * @param entry the entry to set
 * If not set, set the default source, the af_family
 * and void ip address pointer used in the entry comparison routine
 */
void neigh_table_set_entry(struct neighbour_entry *entry)
{
    struct sockaddr_storage *ipaddr;

    if (!entry->source) entry->source = NEIGH_SRC_NOT_SET;

    if (entry->af_family == 0) entry->af_family = entry->ipaddr->ss_family;

    if (entry->ip_tbl) return;

    ipaddr = entry->ipaddr;
    if (entry->af_family == AF_INET)
    {
        struct sockaddr_in *sa;

        sa = (struct sockaddr_in *)ipaddr;
        entry->ip_tbl = (uint8_t *)(&sa->sin_addr.s_addr);
    }

    if (entry->af_family == AF_INET6)
    {
        struct sockaddr_in6 *sa6;

        sa6 = (struct sockaddr_in6 *)ipaddr;
        entry->ip_tbl = sa6->sin6_addr.s6_addr;
    }
}

/**
 * @brief populates a sockaddr_storage structure from ip parameters
 *
 * @param af the inet family
 * @param ip a pointer to the ip address buffer
 * @param dst the sockaddr_storage structure to fill
 * @return none
 */
static void
neigh_table_populate_sockaddr(struct neighbour_entry *to_add,
                              struct sockaddr_storage *dst)
{
    if (to_add->af_family == AF_INET)
    {
        struct sockaddr_in *in4 = (struct sockaddr_in *)dst;

        memset(in4, 0, sizeof(struct sockaddr_in));
        in4->sin_family = to_add->af_family;
        memcpy(&in4->sin_addr, to_add->ip_tbl, sizeof(in4->sin_addr));
    }
    else if (to_add->af_family == AF_INET6)
    {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)dst;

        memset(in6, 0, sizeof(struct sockaddr_in6));
        in6->sin6_family = to_add->af_family;
        memcpy(&in6->sin6_addr, to_add->ip_tbl, sizeof(in6->sin6_addr));
    }
    return;
}


struct neighbour_entry *
neigh_table_add_to_cache(struct neighbour_entry *to_add)
{
    struct neighbour_entry *entry;
    struct neigh_table_mgr *mgr;
    int af_family;

    mgr = neigh_table_get_mgr();

    if (to_add->mac == NULL) return NULL;
    neigh_table_set_entry(to_add);

    if (to_add->ip_tbl == NULL) return NULL;

    af_family = to_add->af_family;
    if ((af_family != AF_INET) && (af_family != AF_INET6)) return NULL;

    entry = neigh_table_cache_lookup(to_add);
    if (entry)
    {
        /* Refresh timestamp */
        entry->cache_valid_ts = to_add->cache_valid_ts;

        return NULL;
    }

    entry = calloc(1, sizeof(struct neighbour_entry));
    if (!entry) return false;

    entry->ipaddr = calloc(1, sizeof(struct sockaddr_storage));
    if (entry->ipaddr == NULL) goto err_free_entry;

    entry->mac = calloc(1, sizeof(os_macaddr_t));
    if (entry->mac == NULL) goto err_free_ipaddr;

    if (to_add->ifname != NULL)
    {
        entry->ifname = strdup(to_add->ifname);
        if (entry->ifname == NULL) goto err_free_mac;
    }

    neigh_table_populate_sockaddr(to_add, entry->ipaddr);
    memcpy(entry->mac, to_add->mac, sizeof(os_macaddr_t));
    entry->source = to_add->source;
    entry->ifindex = to_add->ifindex;
    entry->cache_valid_ts = to_add->cache_valid_ts;

    neigh_table_set_entry(entry);
    LOGT("%s: adding to cache: ", __func__);
    print_neigh_entry(entry);

    ds_tree_insert(&mgr->neigh_table, entry, entry);

    return entry;

err_free_mac:
    free(entry->mac);

err_free_ipaddr:
    free(entry->ipaddr);

err_free_entry:
    free(entry);

    return NULL;
}

bool neigh_table_add(struct neighbour_entry *to_add)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry *entry;

    if (to_add == NULL) return false;

    neigh_table_set_entry(to_add);

    entry = neigh_table_add_to_cache(to_add);
    if (entry == NULL)
    {
        LOGD("%s: adding to cache failed", __func__);
        return false;
    }

    // Update ovsdb tables if required.
    if (mgr->update_ovsdb_tables &&
        !mgr->update_ovsdb_tables(entry, false))
    {
        LOGD("%s: Failed to add entry into ovsdb table.", __func__);
        return false;
    }

    return true;
}


void neigh_table_delete_from_cache(struct neighbour_entry *to_del)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry *lookup;

    if (!to_del) return;

    neigh_table_set_entry(to_del);
    lookup = neigh_table_cache_lookup(to_del);
    if (lookup == NULL)
    {
        LOGD("%s: entry not found", __func__);
        return;
    }

    ds_tree_remove(&mgr->neigh_table, lookup);
    free_neigh_entry(lookup);
}

void neigh_table_delete(struct neighbour_entry *to_del)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry *lookup;

    if (!to_del) return;

    neigh_table_set_entry(to_del);
    lookup = neigh_table_cache_lookup(to_del);
    if (lookup == NULL)
    {
        LOGD("%s: entry not found", __func__);
        return;
    }

    ds_tree_remove(&mgr->neigh_table, lookup);

    // Update ovsdb tables if required.
    if (mgr->update_ovsdb_tables &&
        !mgr->update_ovsdb_tables(lookup, true))
    {
        LOGD("%s: Failed to delete entry from ovsdb table.", __func__);
        return;
    }
    free_neigh_entry(lookup);

    return;
}

bool neigh_table_cache_update(struct neighbour_entry *entry)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry *lookup;

    if (!entry) return false;

    neigh_table_set_entry(entry);

    lookup = ds_tree_find(&mgr->neigh_table, entry);

    // We dont have it. Add it.
    if (!lookup)
    {
        if (!neigh_table_add_to_cache(entry))
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
    lookup->source = entry->source;

    return true;
}

struct neighbour_entry *
neigh_table_cache_lookup(struct neighbour_entry *key)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry *lookup;

    if (!key) return false;

    neigh_table_set_entry(key);
    lookup = ds_tree_find(&mgr->neigh_table, key);
    if (!lookup) return NULL;

    LOGT("%s: found entry", __func__);
    print_neigh_entry(lookup);

    if (key->mac != NULL)
    {
        memcpy(key->mac, lookup->mac, sizeof(os_macaddr_t));
    }

    return lookup;
}

int lookup_sources[] =
{
    OVSDB_DHCP_LEASE,
    NEIGH_TBL_SYSTEM,
    OVSDB_NDP,
    OVSDB_ARP,
    OVSDB_INET_STATE,
    NEIGH_SRC_NOT_SET,
    NEIGH_UT,
};

/**
 * @brief lookup for a neighbor table entry.
 *
 * @return true if found and false if not.
 */
bool neigh_table_lookup(struct sockaddr_storage *ip_in, os_macaddr_t *mac_out)
{
    struct neighbour_entry *lookup;
    struct neighbour_entry key;
    size_t len;
    size_t i;
    bool ret;

    if (!ip_in || !mac_out) return false;

    memset(&key, 0, sizeof(key));
    key.ipaddr = ip_in;
    key.mac    = mac_out;
    key.ifname = NULL;
    neigh_table_set_entry(&key);

    len = sizeof(lookup_sources) / sizeof(lookup_sources[0]);
    for (i = 0; i < len; i++)
    {
        key.source = lookup_sources[i];
        lookup = neigh_table_cache_lookup(&key);
        ret = (lookup != NULL);
        if (ret) break;
    }

    return ret;
}


/**
 * @brief remove old cache entres added by fsm
 *
 * @param ttl the cache entry time to live
 */
void neigh_table_ttl_cleanup(int64_t ttl, uint32_t source_mask)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry *remove_node;
    struct neighbour_entry *entry_node;
    ds_tree_t *tree;
    time_t now;

    if (!mgr->initialized) return;

    now = time(NULL);
    tree = &mgr->neigh_table;
    entry_node = ds_tree_head(tree);
    while (entry_node != NULL)
    {
        /* We are only interested in the fsm added entries */
        if (!(entry_node->source & source_mask))
        {
            entry_node = ds_tree_next(tree, entry_node);
            continue;
        }

        if ((now - entry_node->cache_valid_ts) < ttl)
        {
            entry_node = ds_tree_next(tree, entry_node);
            continue;
        }

        remove_node = entry_node;

        if (mgr->update_ovsdb_tables)
        {
            mgr->update_ovsdb_tables(remove_node, true);
        }

        entry_node = ds_tree_next(tree, entry_node);
        ds_tree_remove(tree, remove_node);
        free_neigh_entry(remove_node);
    }
}


void print_neigh_entry(struct neighbour_entry *entry)
{
    char                   ipstr[INET6_ADDRSTRLEN] = { 0 };
    os_macaddr_t           nullmac = { 0 };
    os_macaddr_t           *pmac;
    char                   *source;
    const char             *ip;

    ip = inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    if (ip == NULL)
    {
        LOGD("%s: inet_ntop failed: %s", __func__, strerror(errno));
        return;
    }

    pmac = (entry->mac != NULL) ? entry->mac : &nullmac;
    source = neigh_table_get_source(entry->source);
    LOGD("ip %s, mac "PRI_os_macaddr_lower_t
         " if_name %s source %s af_family %d",
         ipstr, FMT_os_macaddr_pt(pmac),
         entry->ifname ? entry->ifname : "empty",
         source ? source : "empty", entry->af_family);
}

void print_neigh_table(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry *entry_node = NULL;

    LOGT("%s: neigh_table dump", __func__);

    ds_tree_foreach(&mgr->neigh_table, entry_node)
    {
        print_neigh_entry(entry_node);
    }
    LOGT("=====END=====");
}
