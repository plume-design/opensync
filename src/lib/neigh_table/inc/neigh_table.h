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

#ifndef NEIGH_TABLE_H_INCLUDED
#define NEIGH_TABLE_H_INCLUDED

#include <stdint.h>
#include <time.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "ds_tree.h"
#include "os.h"
#include "os_types.h"
#include "nf_utils.h"

/******************************************************************************
* Struct Declarations
*******************************************************************************/
/*
 * neighbor entry.
 */
struct neighbour_entry
{
    struct sockaddr_storage     *ipaddr;
    os_macaddr_t                *mac;
    char                        *ifname;
    int                         ifindex;
    uint32_t                    source;
    time_t                      cache_valid_ts;
    uint8_t                     *ip_tbl;           // for fast lookups
    int                         af_family;         // for fast lookups
    ds_tree_node_t              entry_node;        // tree node structure
};

/*
 * interface entry.
 * Counts the number of neighbour entries bound to the interface
 */
struct neigh_interface
{
    int ifindex;
    int entries_count;
    ds_tree_node_t intf_node;
};

struct neigh_table_mgr
{
    bool initialized;
    ds_tree_t neigh_table;
    ds_tree_t interfaces;
    bool (*update_ovsdb_tables)(struct neighbour_entry *key, bool remove);
};


enum source
{
    NEIGH_SRC_NOT_SET       = 1 << 0,
    NEIGH_TBL_SYSTEM        = 1 << 1,
    OVSDB_DHCP_LEASE        = 1 << 2,
    OVSDB_ARP               = 1 << 3,
    FSM_ARP                 = OVSDB_ARP,
    OVSDB_NDP               = 1 << 4,
    FSM_NDP                 = OVSDB_NDP,
    OVSDB_INET_STATE        = 1 << 5,
    NEIGH_UT                = 1 << 6,
};

struct neigh_mapping_source
{
    char *source;
    int source_enum;
};

struct neigh_table_mgr
*neigh_table_get_mgr(void);

/**
 * @brief initialize neighbor_table handle manager.
 */
void
neigh_table_init_manager(void);

/**
 * @brief initialize neighbor_table handle.
 *
 * receive none
 *
 * @return 0 for success and 1 for failure .
 */
int
neigh_table_init(void);

void neigh_table_cache_cleanup(void);

/**
 * @brief cleanup allocated memory.
 *
 * receive none
 *
 * @return void.
 */
void
neigh_table_cleanup(void);

/**
 * @brief compare neighbor table entries.
 *
 * @return 0 on match and greater than 1 if no match.
 */

void
free_neigh_entry(struct neighbour_entry *entry);

int
neigh_table_cmp(void *a, void *b);

bool
update_ip_in_ovsdb_table(struct neighbour_entry *key, bool remove);

/**
 * @brief lookup for a neighbor table entry.
 *
 * @return true if found, false otherwise.
 */
bool
neigh_table_lookup(struct sockaddr_storage *ip_in,
                   os_macaddr_t *mac_out);

void
print_neigh_entry(struct neighbour_entry *entry);

void
print_neigh_table(void);

struct neighbour_entry *
neigh_table_add_to_cache(struct neighbour_entry *to_add);

bool
neigh_table_add(struct neighbour_entry *to_add);

void
neigh_table_delete_from_cache(struct neighbour_entry *to_del);

void
neigh_table_delete(struct neighbour_entry *to_del);

struct neighbour_entry *
neigh_table_cache_lookup(struct neighbour_entry *key);

bool
neigh_table_cache_update(struct neighbour_entry *entry);


/**
 * @brief remove old cache entres added by fsm
 *
 * @param ttl the cache entry time to live
 */
void neigh_table_ttl_cleanup(int64_t ttl, uint32_t source_mask);


/**
 * @brief return the source based on its enum value
 *
 * @param source_enum the source represented as an integer
 * @return a string pointer representing the source
 */
char *
neigh_table_get_source(int source_enum);

/**
 * @brief set fast lookup fields of a neighbour entry
 *
 * @param entry the entry to set
 * If not set, set the default source, the af_family
 * and void ip address pointer used in the entry comparison routine
 */
void neigh_table_set_entry(struct neighbour_entry *entry);

/**
 * @brief lookup interface context.
 *
 * @param ifindex the interface system index
 *
 * Look up an interface context
 */
struct neigh_interface * neigh_table_lookup_intf(int ifindex);

/**
 * @brief lookup/create interface context.
 *
 * @param ifindex the interface system index
 *
 * Looks up an interface context. If not found, create one.
 */
struct neigh_interface * neigh_table_get_intf(int ifindex);

/**
 * @brief initializes ovsdb callback
 */
void
neigh_src_init(void);

void neigh_table_init_monitor(struct ev_loop *loop,
                              bool system_event, bool ovsdb_event);

#endif /* NEIGH_TABLE_H_INCLUDED */
