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
    time_t                      cache_valid_ts;
    ds_tree_node_t              entry_node;        // tree node structure
};

struct neigh_table_mgr
{
    bool initialized;
    ds_tree_t neigh_table;
    bool (*update_ovsdb_tables)(struct neighbour_entry *key, bool remove);
    bool (*lookup_ovsdb_tables)(struct neighbour_entry *key);
    bool (*lookup_kernel_entry)(struct neighbour_entry *key);
    void (*ovsdb_init)(void);
};

#define NEIGH_CACHE_INTERVAL       600

struct neigh_table_mgr
*neigh_table_get_mgr(void);

/**
 * @brief initialize neighbor_table handle.
 *
 * receive none
 *
 * @return 0 for success and 1 for failure .
 */
int
neigh_table_init(void);

/**
 * @brief cleanup allocatef memody.
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

bool
lookup_entry_in_kernel(struct neighbour_entry *key);

bool
lookup_ip_in_ovsdb_table(struct neighbour_entry *key);

/**
 * @brief lookup for a neighbor table entry.
 *
 * @return true if found, false otherwise.
 */
bool
neigh_table_lookup(struct sockaddr_storage *ip_in,
                   os_macaddr_t *mac_out);

void
print_neigh_table(void);

bool
neigh_table_add(struct neighbour_entry *to_add);

void
neigh_table_delete(struct neighbour_entry *to_del);

bool
neigh_table_cache_lookup(struct neighbour_entry *key);

bool
neigh_table_cache_update(struct neighbour_entry *entry);
#endif /* NEIGH_TABLE_H_INCLUDED */
