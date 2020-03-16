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

/*
 * Band Steering Manager - Neighbors
 */

#ifndef BM_NEIGHBOR_H_INCLUDED
#define BM_NEIGHBOR_H_INCLUDED

#ifndef OVSDB_UUID_LEN
#define OVSDB_UUID_LEN      37
#endif /* OVSDB_UUID_LEN */

/***************************************************************************************/

typedef struct {

    char                ifname[BSAL_IFNAME_LEN];
    char                bssid[MAC_STR_LEN];

    uint8_t             channel;
    radio_chanwidth_t   ht_mode;
    uint8_t             priority;

    bsal_neigh_info_t   neigh_report;

    char                uuid[OVSDB_UUID_LEN];
    ds_tree_node_t      dst_node;
} bm_neighbor_t;

/*****************************************************************************/

extern bool     bm_neighbor_init( void );
extern bool     bm_neighbor_cleanup( void );

ds_tree_t       *bm_neighbor_get_tree( void );
bm_neighbor_t   *bm_neighbor_find_by_uuid( const char *uuid );
bm_neighbor_t   *bm_neighbor_find_by_macstr( char *mac_str );
unsigned int    bm_neighbor_number(bm_client_t *client);

bool bm_neighbor_get_self_btm_values(bsal_btm_params_t *btm_params, bm_client_t *client, bool bs_allowed);
bool bm_neighbor_build_btm_neighbor_list(bm_client_t *client, bsal_btm_params_t *btm_params);

uint8_t         bm_neighbor_get_op_class(uint8_t channel);
uint8_t         bm_neighbor_get_phy_type(uint8_t channel);

void bm_neighbor_set_all_to_group(const bm_group_t *group);
void bm_neighbor_remove_all_from_group(const bm_group_t *group);

bool bm_neighbor_get_self_neighbor(const char *ifname, bsal_neigh_info_t *neigh);
bool bm_neighbor_only_dfs_channels(bm_client_t *client);

bool bm_neighbor_is_our_bssid(const bm_client_t *client, const unsigned char *bssid);
int bm_neighbor_get_channels(bm_client_t *client, bm_client_rrm_req_type_t rrm_req_type, uint8_t *channels, int channels_size, int self_first);

#endif /* BM_NEIGHBOR_H_INCLUDED */
