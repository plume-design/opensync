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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <assert.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <stdarg.h>
#include <linux/types.h>

#include "bm.h"

/*****************************************************************************/

#define MODULE_ID LOG_MODULE_ID_NEIGHBORS

/*****************************************************************************/
static ovsdb_update_monitor_t   bm_neighbor_ovsdb_update;
static ovsdb_update_monitor_t   bm_vif_state_ovsdb_update;
static ds_tree_t                bm_neighbors = DS_TREE_INIT( (ds_key_cmp_t *)strcmp,
                                                             bm_neighbor_t,
                                                             dst_node );

static void
bm_neighbor_add_to_group_by_ifname(const bm_group_t *group, const char *ifname, bool bs_allowed_only);

static c_item_t map_ovsdb_chanwidth[] = {
    C_ITEM_STR( RADIO_CHAN_WIDTH_20MHZ,         "HT20" ),
    C_ITEM_STR( RADIO_CHAN_WIDTH_40MHZ,         "HT40" ),
    C_ITEM_STR( RADIO_CHAN_WIDTH_40MHZ_ABOVE,   "HT40+" ),
    C_ITEM_STR( RADIO_CHAN_WIDTH_40MHZ_BELOW,   "HT40-" ),
    C_ITEM_STR( RADIO_CHAN_WIDTH_80MHZ,         "HT80" ),
    C_ITEM_STR( RADIO_CHAN_WIDTH_160MHZ,        "HT160" ),
    C_ITEM_STR( RADIO_CHAN_WIDTH_80_PLUS_80MHZ, "HT80+80" ),
    C_ITEM_STR( RADIO_CHAN_WIDTH_NONE,          "HT2040" )
};

uint8_t
bm_neighbor_get_op_class( uint8_t channel )
{
    if( channel >= 1 && channel <= 13 ) {
        return BTM_24_OP_CLASS;
    }

    if( channel >= 36 && channel <= 48 ) {
        return BTM_5GL_OP_CLASS;
    }

    if( channel >= 52 && channel <= 64 ) {
        return BTM_L_DFS_OP_CLASS;
    }

    if( channel >= 100 && channel <= 140 ) {
        return BTM_U_DFS_OP_CLASS;
    }

    if( channel >= 149 && channel <= 169 ) {
        return BTM_5GU_OP_CLASS;
    }

    return 0;
}

uint8_t
bm_neighbor_get_phy_type( uint8_t channel )
{
    if( channel >= 1 && channel <= 13 ) {
        return BTM_24_PHY_TYPE;
    }

    if( channel >= 36 && channel <= 169 ) {
        return BTM_5_PHY_TYPE;
    }

    return 0;
}

bool
bm_neighbor_get_self_neighbor(const char *_ifname, bsal_neigh_info_t *neigh)
{
    struct  schema_Wifi_VIF_State   vif;
    json_t                          *jrow;
    pjs_errmsg_t                    perr;
    char                            *ifname;
    os_macaddr_t                    macaddr;

    memset(neigh, 0, sizeof(*neigh));

    // On platforms other than pods, the home-ap-* interfaces are mapped to
    // other platform-dependent names such as ath0, etc. On the pod, the
    // mapping is the same.
    ifname = target_unmap_ifname((char *) _ifname);
    if( strlen( ifname ) == 0 ) {
        LOGE("Unable to target_unmap_ifname '%s'", _ifname);
        return false;
    }

    json_t  *where = ovsdb_where_simple( SCHEMA_COLUMN( Wifi_VIF_State, if_name),
                                         ifname );

    /* TODO use ovsdb_sync_select() here */
    jrow = ovsdb_sync_select_where( SCHEMA_TABLE( Wifi_VIF_State ), where );

    if( !schema_Wifi_VIF_State_from_json(
                &vif,
                json_array_get( jrow, 0 ),
                false,
                perr ) )
    {
        LOGE( "Unable to parse Wifi_VIF_State column: %s", perr );
        json_decref(jrow);
        return false;
    }

    neigh->channel = ( uint8_t )vif.channel;

    if (!vif.mac_exists) {
        LOGE("%s: mac doesn't exists", ifname);
        json_decref(jrow);
        return false;
    }

    if( !os_nif_macaddr_from_str( &macaddr, vif.mac ) ) {
        LOGE( "Unable to parse mac address '%s'", vif.mac );
        json_decref(jrow);
        return false;
    }

    LOGT( "Got self channel: %d and self bssid: %s", vif.channel, vif.mac );
    memcpy( neigh->bssid, (uint8_t *)&macaddr, sizeof( neigh->bssid ) );

    // Assume the default BSSID_INFO
    neigh->bssid_info = BTM_DEFAULT_NEIGH_BSS_INFO;
    neigh->op_class = bm_neighbor_get_op_class(neigh->channel);
    neigh->phy_type = bm_neighbor_get_phy_type(neigh->channel);

    json_decref(jrow);
    return true;
}

static void
bm_neighbor_set_neighbor(const bsal_neigh_info_t *neigh_report)
{
    ds_tree_t       *groups;
    bm_group_t       *group;

    if (!(groups = bm_group_get_tree())) {
        LOGE("%s failed to get group tree", __func__);
        return;
    }

    ds_tree_foreach(groups, group) {
        bm_neighbor_remove_all_from_group(group);
        bm_neighbor_set_all_to_group(group);
    }
}

static void
bm_neighbor_remove_neighbor(const bsal_neigh_info_t *neigh_report)
{
    ds_tree_t       *groups;
    bm_group_t      *group;
    unsigned int    i;

    if (!(groups = bm_group_get_tree())) {
        LOGE("%s failed to get group tree", __func__);
        return;
    }

    ds_tree_foreach(groups, group) {
        for (i = 0; i < group->ifcfg_num; i++) {
            if (target_bsal_rrm_remove_neighbor(group->ifcfg[i].bsal.ifname, neigh_report))
                LOGW("%s: remove_neigh: "PRI(os_macaddr_t)" failed", group->ifcfg[i].bsal.ifname,
                     FMT(os_macaddr_pt, (os_macaddr_t *) neigh_report->bssid));
        }
    }
}

/*****************************************************************************/
static bool
bm_neighbor_from_ovsdb( struct schema_Wifi_VIF_Neighbors *nconf, bm_neighbor_t *neigh )
{
    os_macaddr_t bssid;
    c_item_t    *item;

    STRSCPY(neigh->ifname, nconf->if_name);
    STRSCPY(neigh->bssid,  nconf->bssid);

    neigh->channel  = nconf->channel;
    neigh->priority = nconf->priority;

    if (!nconf->ht_mode_exists) {
        neigh->ht_mode = RADIO_CHAN_WIDTH_20MHZ;
    } else {
        item = c_get_item_by_str( map_ovsdb_chanwidth, nconf->ht_mode );
        if( !item ) {
            LOGE( "Neighbor %s - unknown ht_mode '%s'", neigh->bssid, nconf->ht_mode );
            return false;
        }
        neigh->ht_mode  = (radio_chanwidth_t)item->key;
    }

    if(!os_nif_macaddr_from_str(&bssid, neigh->bssid)) {
        return false;
    }

    memcpy(&neigh->neigh_report.bssid, &bssid, sizeof(bssid));
    neigh->neigh_report.bssid_info = BTM_DEFAULT_NEIGH_BSS_INFO;
    neigh->neigh_report.channel = nconf->channel;
    neigh->neigh_report.op_class = bm_neighbor_get_op_class(nconf->channel);
    neigh->neigh_report.phy_type = bm_neighbor_get_phy_type(nconf->channel);

    return true;
}

static void
bm_vif_state_ovsdb_update_cb(ovsdb_update_monitor_t *self)
{
    struct schema_Wifi_VIF_State vstate;
    unsigned int i;
    ds_tree_t *groups;
    bm_group_t *group;
    pjs_errmsg_t perr;

    switch(self->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            if (!schema_Wifi_VIF_State_from_json(&vstate, self->mon_json_new, false, perr)) {
                LOGE("Failed to prase new Wifi_VIF_State row: %s", perr);
                break;
            }

            if (!vstate.channel_exists)
                break;

            if (!(groups = bm_group_get_tree())) {
                LOGE("%s failed to get group tree", __func__);
                break;;
            }

            ds_tree_foreach(groups, group) {
                for (i = 0; i < group->ifcfg_num; i++) {
                    if (strcmp(group->ifcfg[i].ifname, vstate.if_name))
                        continue;
                    if (group->ifcfg[i].self_neigh.channel == vstate.channel)
                        continue;
                    LOGI("%s self %d new %d", vstate.if_name, group->ifcfg[i].self_neigh.channel, vstate.channel);
                    group->ifcfg[i].self_neigh.channel = vstate.channel;
                    group->ifcfg[i].self_neigh.op_class = bm_neighbor_get_op_class(vstate.channel);
                    bm_neighbor_add_to_group_by_ifname(group, group->ifcfg[i].ifname, group->ifcfg[i].bs_allowed);
                    bm_client_update_all_from_group(group);
                }
            }

            break;
        case OVSDB_UPDATE_DEL:
        default:
            break;
    }
}

static void
bm_neighbor_ovsdb_update_cb( ovsdb_update_monitor_t *self )
{
    struct schema_Wifi_VIF_Neighbors    nconf;
    pjs_errmsg_t                        perr;
    bm_neighbor_t                       *neigh;

    switch( self->mon_type )
    {
        case OVSDB_UPDATE_NEW:
        {
            if( !schema_Wifi_VIF_Neighbors_from_json( &nconf,
                                                      self->mon_json_new, false, perr )) {
                LOGE( "Failed to parse new Wifi_VIF_Neighbors row: %s", perr );
                return;
            }

            if ((neigh = ds_tree_find(&bm_neighbors, nconf.bssid))) {
                LOGE("Ignoring duplicate neighbor '%s' (orig uuid=%s, new uuid=%s)",
                      neigh->bssid, neigh->uuid, nconf._uuid.uuid);
                return;
            }

            neigh = calloc( 1, sizeof( *neigh ) );
            STRSCPY(neigh->uuid, nconf._uuid.uuid);

            if( !bm_neighbor_from_ovsdb( &nconf, neigh ) ) {
                LOGE( "Failed to convert row to neighbor info (uuid=%s)", neigh->uuid );
                free( neigh );
                return;
            }

            ds_tree_insert( &bm_neighbors, neigh, neigh->bssid );
            LOGN( "Initialized Neighbor VIF bssid:%s if-name:%s Priority: %hhu"
                  " Channel: %hhu HT-Mode: %hhu", neigh->bssid, neigh->ifname,
                                                  neigh->priority, neigh->channel,
                                                  neigh->ht_mode );
            bm_neighbor_set_neighbor(&neigh->neigh_report);
            break;
        }

        case OVSDB_UPDATE_MODIFY:
        {
            if( !( neigh = bm_neighbor_find_by_uuid( self->mon_uuid ))) {
                LOGE( "Unable to find Neighbor for modify with uuid=%s", self->mon_uuid );
                return;
            }

            if( !schema_Wifi_VIF_Neighbors_from_json( &nconf,
                                                      self->mon_json_new, true, perr )) {
                LOGE( "Failed to parse modeified Wifi_VIF_Neighbors row uuid=%s: %s", 
                                                                        self->mon_uuid, perr );
                return;
            }

            if( !bm_neighbor_from_ovsdb( &nconf, neigh ) ) {
                LOGE( "Failed to convert row to neighbor for modify (uuid=%s)", neigh->uuid );
                return;
            }

            LOGN( "Updated Neighbor %s", neigh->bssid );
            bm_neighbor_set_neighbor(&neigh->neigh_report);

            break;
        }

        case OVSDB_UPDATE_DEL:
        {
            if( !( neigh = bm_neighbor_find_by_uuid( self->mon_uuid ))) {
                LOGE( "Unable to find neighbor for delete with uuid=%s", self->mon_uuid );
                return;
            }

            LOGN( "Removing neighbor %s", neigh->bssid );
            bm_neighbor_remove_neighbor(&neigh->neigh_report);
            ds_tree_remove( &bm_neighbors, neigh );
            free( neigh );

            break;
        }

        default:
            break;
    }

    bm_client_update_rrm_neighbors();
    return;
}


/*****************************************************************************/

ds_tree_t *
bm_neighbor_get_tree( void )
{
    return &bm_neighbors;
}

bm_neighbor_t *
bm_neighbor_find_by_uuid( const char *uuid )
{
    bm_neighbor_t   *neigh;

    ds_tree_foreach( &bm_neighbors, neigh ) {
        if( !strcmp( neigh->uuid, uuid )) {
            return neigh;
        }
    }

    return NULL;
}

bm_neighbor_t *
bm_neighbor_find_by_macstr( char *mac_str )
{
    return ( bm_neighbor_t * )ds_tree_find( &bm_neighbors, (char *)mac_str );
}


/*****************************************************************************/
bool
bm_neighbor_init( void )
{
    LOGI( "BM Neighbors Initializing" );

    // Start OVSDB monitoring
    if( !ovsdb_update_monitor( &bm_neighbor_ovsdb_update,
                               bm_neighbor_ovsdb_update_cb,
                               SCHEMA_TABLE( Wifi_VIF_Neighbors ),
                               OMT_ALL ) ) {
        LOGE( "Failed to monitor OVSDB table '%s'", SCHEMA_TABLE( Wifi_VIF_Neighbors ) );
        return false;
    }

    if (!ovsdb_update_monitor(&bm_vif_state_ovsdb_update,
                              bm_vif_state_ovsdb_update_cb,
                              SCHEMA_TABLE(Wifi_VIF_State),
                              OMT_ALL)) {
        LOGE("Failed to monitor OVSDB table %s", SCHEMA_TABLE(Wifi_VIF_State));
        return false;
    }

    return true;
}

bool
bm_neighbor_cleanup( void )
{
    ds_tree_iter_t  iter;
    bm_neighbor_t   *neigh;

    LOGI( "BM Neighbors cleaning up" );

    neigh = ds_tree_ifirst( &iter, &bm_neighbors );
    while( neigh ) {
        ds_tree_iremove( &iter );

        free( neigh );

        neigh = ds_tree_inext( &iter );
    }

    return true;
}

/*
 * In the future client should have list of available/supported
 * channels. Next we could chose compatible neighbors
 */
static bool
bm_neighbor_channel_supported_by_client(const bm_client_t *client, uint8_t channel)
{
    /* TODO: Handle supported channels in the future here */
    return true;
}

static bool
bm_neighbor_channel_allowed(const bm_client_t *client, uint8_t channel)
{
    bool allowed = false;
    radio_type_t radio_type;
    unsigned int i;

    /* First check if client support such channel */
    if (!bm_neighbor_channel_supported_by_client(client, channel)) {
        return false;
    }

    /* Next check base on radio_type and bs_allowed */
    for (i = 0; i < client->ifcfg_num; i++) {
       if (!client->ifcfg[i].bs_allowed)
           continue;

       radio_type = client->ifcfg[i].radio_type;
       switch (radio_type) {
           case RADIO_TYPE_2G:
               if (channel <= 13)
                   allowed = true;
               break;
           case RADIO_TYPE_5G:
           case RADIO_TYPE_5GL:
           case RADIO_TYPE_5GU:
               if (channel > 13)
                   allowed = true;
               break;
           case RADIO_TYPE_NONE:
           default:
               break;
       }

       if (allowed)
           break;
    }

    LOGD("%s %d channel allowed %d", client->mac_addr, channel, allowed);
    return allowed;
}

unsigned int
bm_neighbor_number(bm_client_t *client)
{
    int neighbors = 0;
    bm_neighbor_t *bm_neigh;

    ds_tree_foreach(&bm_neighbors, bm_neigh) {
        if (!bm_neighbor_channel_allowed(client, bm_neigh->channel))
            continue;
        neighbors++;
    }

    return neighbors;
}

bool
bm_neighbor_only_dfs_channels(bm_client_t *client)
{
    bm_neighbor_t *bm_neigh;
    bool only_dfs = true;

    ds_tree_foreach(&bm_neighbors, bm_neigh) {
        if (!bm_neighbor_channel_allowed(client, bm_neigh->channel))
            continue;
        if (bm_client_is_dfs_channel(bm_neigh->channel))
            continue;

        only_dfs = false;
        break;
    }

    return only_dfs;
}

/* group update */
static bool
bm_neighbor_channel_bs_allowed(const bm_group_t *group, uint8_t channel)
{
    bool allowed = false;
    radio_type_t radio_type;
    unsigned int i;

    /* Check base on radio_type and bs_allowed */
    for (i = 0; i < group->ifcfg_num; i++) {
       if (!group->ifcfg[i].bs_allowed)
           continue;

       radio_type = group->ifcfg[i].radio_type;
       switch (radio_type) {
           case RADIO_TYPE_2G:
               if (channel <= 13)
                   allowed = true;
               break;
           case RADIO_TYPE_5G:
           case RADIO_TYPE_5GL:
           case RADIO_TYPE_5GU:
               if (channel > 13)
                   allowed = true;
               break;
           case RADIO_TYPE_NONE:
           default:
               break;
       }

       if (allowed)
           break;
    }

    LOGD("group %d channel allowed %d", channel, allowed);
    return allowed;
}

/* BSS TM */
static bool
_bm_neighbor_get_self_btm_values(bsal_btm_params_t *btm_params, const char *ifname)
{
    if ((unsigned int) btm_params->num_neigh >= ARRAY_SIZE(btm_params->neigh)) {
        LOGW("%s we exceend neigh array size %d", ifname, btm_params->num_neigh);
        return false;
    }

    if (!bm_neighbor_get_self_neighbor(ifname, &btm_params->neigh[btm_params->num_neigh])) {
        LOGW("%s failed for %s", __func__, ifname);
        return false;
    }

    btm_params->num_neigh++;
    return true;
}

bool
bm_neighbor_get_self_btm_values(bsal_btm_params_t *btm_params,
                                bm_client_t *client, bool bs_allowed)
{
    bool res = false;
    unsigned int i;

    btm_params->num_neigh = 0;

    for (i = 0; i < client->ifcfg_num; i++) {
        if (client->ifcfg[i].bs_allowed != bs_allowed) {
            continue;
        }

        res = res || _bm_neighbor_get_self_btm_values(btm_params, client->ifcfg[i].ifname);
   }

   if (btm_params->num_neigh) {
       btm_params->pref = 1;
   } else {
       LOGI("Client '%s': empty self btm values", client->mac_addr);
       btm_params->pref = 0;
   }

   return res;
}

bm_rrm_neighbor_t *
bm_neighbor_get_rrm_neigh(bm_client_t *client, os_macaddr_t *bssid)
{
    bm_rrm_neighbor_t *rrm_neigh;
    unsigned int i;

    for (i = 0; i < client->rrm_neighbor_num; i++) {
        rrm_neigh = &client->rrm_neighbor[i];
        if (memcmp(&rrm_neigh->bssid, bssid, sizeof(*bssid)) == 0)
            return rrm_neigh;
    }

    return NULL;
}

bm_rrm_neighbor_t *
bm_neighbor_get_self_rrm_neigh(bm_client_t *client)
{
    bsal_neigh_info_t neigh;

    if (!bm_neighbor_get_self_neighbor(client->ifname, &neigh))
        return NULL;

    return bm_neighbor_get_rrm_neigh(client, (os_macaddr_t *) neigh.bssid);
}

bool
bm_neighbor_better(bm_client_t *client, bm_neighbor_t *bm_neigh)
{
    bm_rrm_neighbor_t *rrm_self_neigh;
    bm_rrm_neighbor_t *rrm_neigh;
    time_t now;

    now = time(NULL);

    LOGD("%s better check", client->mac_addr);
    rrm_self_neigh = bm_neighbor_get_self_rrm_neigh(client);
    if (!rrm_self_neigh) {
        /* No rcpi/rssi for self bssid */
        LOGD("%s no self neigh", client->mac_addr);
        return true;
    }

    if ((unsigned int) (now - rrm_self_neigh->time) > client->rrm_age_time) {
        LOGD("%s rrm results too old, don't use them %u", client->mac_addr,
             (unsigned int) (now - rrm_self_neigh->time));
        return true;
    }

    rrm_neigh = bm_neighbor_get_rrm_neigh(client, (os_macaddr_t *) bm_neigh->neigh_report.bssid);
    if (!rrm_neigh) {
        LOGD("%s no rrm_neigh, assume client don't see %s", client->mac_addr, bm_neigh->bssid);
        return false;
    }

    /* Finally compare rcpi */
    if (rrm_neigh->rcpi < rrm_self_neigh->rcpi + 2 * client->rrm_better_factor) {
        LOGD("[%s]: neigh %s self %u neigh %u worst rcpi", client->mac_addr, bm_neigh->bssid, rrm_self_neigh->rcpi, rrm_neigh->rcpi);
        return false;
    }

    return true;
}

bool
bm_neighbor_build_btm_neighbor_list( bm_client_t *client, bsal_btm_params_t *btm_params )
{
    ds_tree_t                   *bm_neighbors   = NULL;
    bm_neighbor_t               *bm_neigh       = NULL;

    bsal_neigh_info_t           *bsal_neigh     = NULL;
    os_macaddr_t                macaddr;
    int                         max_regular_neighbors;
    unsigned int                i;

    if (WARN_ON(!client->group))
        return false;

    if (!btm_params->inc_neigh) {
        LOGT(" Client '%s': NOT building sticky neighbor list", client->mac_addr );
        btm_params->pref      = 0;
        btm_params->num_neigh = 0;
        return true;
    }

    bm_neighbors = bm_neighbor_get_tree();
    if( !bm_neighbors ) {
        LOGE( "Unable to get bm_neighbors tree" );
        return false;
    }

    btm_params->num_neigh = 0;
    if (btm_params->inc_self)
        /* Leave place for self neighbor */
        max_regular_neighbors = BSAL_MAX_TM_NEIGHBORS - 1;
    else
        max_regular_neighbors = BSAL_MAX_TM_NEIGHBORS;

    ds_tree_foreach( bm_neighbors, bm_neigh ) {
        if( btm_params->num_neigh >= max_regular_neighbors ) {
            LOGT( "Built maximum allowed neighbors" );
            break;
        }

        // Include only allowed neighbors
        if(!bm_neighbor_channel_allowed(client, bm_neigh->channel)) {
            LOGT( "Skipping neighbor = %s, channel = %hhu",
                                bm_neigh->bssid, bm_neigh->channel );
        } else if (!bm_neighbor_better(client, bm_neigh)) {
            LOGI("[%s] skipping neighbor %s - not better than current", client->mac_addr, bm_neigh->bssid);
        } else {
            bsal_neigh = &btm_params->neigh[btm_params->num_neigh];

            if( !os_nif_macaddr_from_str( &macaddr, bm_neigh->bssid ) ) {
                LOGE( "Unable to parse mac address '%s'", bm_neigh->bssid );
                return false;
            }

            memcpy( bsal_neigh->bssid, (uint8_t *)&macaddr, sizeof( bsal_neigh->bssid ) );
            bsal_neigh->channel = bm_neigh->channel;
            bsal_neigh->bssid_info = BTM_DEFAULT_NEIGH_BSS_INFO;
            bsal_neigh->op_class = bm_neighbor_get_op_class(bsal_neigh->channel);
            bsal_neigh->phy_type = bm_neighbor_get_phy_type(bsal_neigh->channel);

            btm_params->num_neigh++;

            LOGT( "Built neighbor: %s, channel: %hhu", bm_neigh->bssid, bm_neigh->channel );
        }
    }

    if (btm_params->inc_self && btm_params->num_neigh) {
        for (i = 0; i < client->group->ifcfg_num; i++) {
            if (!bm_neighbor_channel_allowed(client, client->group->ifcfg[i].self_neigh.channel))
                continue;

            if ((unsigned int)btm_params->num_neigh >= ARRAY_SIZE(btm_params->neigh)) {
                LOGI("%s client %s no space left for self bssid", __func__, client->mac_addr);
                break;
            }

            if (!bm_neighbor_get_self_neighbor(client->group->ifcfg[i].bsal.ifname,
                &btm_params->neigh[btm_params->num_neigh])) {
                LOGW("get self for sticky 11v neighbor list failed for %s",
                     client->group->ifcfg[i].bsal.ifname);
                return false;
            }

            btm_params->num_neigh++;
        }
    }


    if (btm_params->num_neigh) {
        btm_params->pref = 1;
    } else {
        LOGI("Client '%s': empty sticky 11v neighbor list", client->mac_addr);
        client->cancel_btm = true;
        btm_params->pref = 0;
        return false;
    }

    LOGT( "Client '%s': Total neighbors = %hhu", client->mac_addr, btm_params->num_neigh );

    return true;
}

static void
bm_neighbor_add_to_group_by_ifname(const bm_group_t *group, const char *ifname, bool bs_allowed_only)
{
    bm_neighbor_t *neigh;
    unsigned int i;

    /* First add bs_allowed neighbors */
    ds_tree_foreach(&bm_neighbors, neigh) {
        if (!bm_neighbor_channel_bs_allowed(group, neigh->channel))
            continue;
        if (target_bsal_rrm_set_neighbor(ifname, &neigh->neigh_report))
            LOGW("%s: set_neigh: %s failed", ifname, neigh->bssid);
    }

    for (i = 0; i < group->ifcfg_num; i++) {
        if (!bm_neighbor_channel_bs_allowed(group, group->ifcfg[i].self_neigh.channel))
            continue;
        if (target_bsal_rrm_set_neighbor(ifname, &group->ifcfg[i].self_neigh))
            LOGW("%s: set_neigh: "PRI(os_macaddr_t)" failed", ifname,
                 FMT(os_macaddr_pt, (os_macaddr_t *) group->ifcfg[i].self_neigh.bssid));
    }

    if (bs_allowed_only)
        return;

    /* Now add !bs_allowed neighbors */
    ds_tree_foreach(&bm_neighbors, neigh) {
        if (bm_neighbor_channel_bs_allowed(group, neigh->channel))
            continue;
        if (target_bsal_rrm_set_neighbor(ifname, &neigh->neigh_report))
            LOGW("%s: set_neigh: %s failed", ifname, neigh->bssid);
    }

    for (i = 0; i < group->ifcfg_num; i++) {
        if (bm_neighbor_channel_bs_allowed(group, group->ifcfg[i].self_neigh.channel))
            continue;
        if (target_bsal_rrm_set_neighbor(ifname, &group->ifcfg[i].self_neigh))
            LOGW("%s: set_neigh: "PRI(os_macaddr_t)" failed", ifname,
                 FMT(os_macaddr_pt, (os_macaddr_t *) group->ifcfg[i].self_neigh.bssid));
    }
}

void
bm_neighbor_set_all_to_group(const bm_group_t *group)
{
    unsigned int i;
    bool bs_allowed_only;
    const char *ifname;

    for (i = 0; i < group->ifcfg_num; i++) {
        ifname = group->ifcfg[i].ifname;

        if (group->ifcfg[i].bs_allowed)
            bs_allowed_only = true;
         else
            bs_allowed_only = false;

        bm_neighbor_add_to_group_by_ifname(group, ifname, bs_allowed_only);
    }
}

void
bm_neighbor_remove_all_from_group(const bm_group_t *group)
{
    bm_neighbor_t *neigh;
    unsigned int i, j;

    ds_tree_foreach(&bm_neighbors, neigh) {
        for (i = 0; i < group->ifcfg_num; i++) {
            if (target_bsal_rrm_remove_neighbor(group->ifcfg[i].bsal.ifname, &neigh->neigh_report))
                LOGW("%s: remove_neigh: %s failed", group->ifcfg[i].bsal.ifname, neigh->bssid);
	}
    }

    for (i = 0; i < group->ifcfg_num; i++) {
        for (j = 0; j < group->ifcfg_num; j++) {
            if (target_bsal_rrm_remove_neighbor(group->ifcfg[i].bsal.ifname, &group->ifcfg[j].self_neigh))
                LOGW("%s: remove_neigh: "PRI(os_macaddr_t)" failed", group->ifcfg[i].bsal.ifname,
                     FMT(os_macaddr_pt, (os_macaddr_t *) group->ifcfg[j].self_neigh.bssid));
        }
    }
}

bool
bm_neighbor_is_our_bssid(const bm_client_t *client, const unsigned char *bssid)
{
    bm_neighbor_t *neigh;
    unsigned int i;

    ds_tree_foreach(&bm_neighbors, neigh) {
        if (memcmp(neigh->neigh_report.bssid, bssid, BSAL_MAC_ADDR_LEN) == 0)
            return true;
    }

    if (!client->group || !client->connected)
        return false;

    for (i = 0; i < client->group->ifcfg_num; i++) {
        if (memcmp(client->group->ifcfg[i].self_neigh.bssid , bssid, BSAL_MAC_ADDR_LEN) == 0)
            return true;
    }

    return false;
}

int
bm_neighbor_get_channels(bm_client_t *client, bm_client_rrm_req_type_t rrm_req_type,
                         uint8_t *channels, int channels_size, int self_first)
{
    uint8_t channel;
    bm_neighbor_t *neigh;
    int count;
    int i;

    memset(channels, 0, channels_size);
    count = 0;

    /* Check own band */
    if (rrm_req_type == BM_CLIENT_RRM_OWN_BAND_ONLY) {
        if (client->self_neigh.channel > 13)
            rrm_req_type = BM_CLIENT_RRM_5G_ONLY;
        else
            rrm_req_type = BM_CLIENT_RRM_2G_ONLY;
    }

    ds_tree_foreach(&bm_neighbors, neigh) {
        channel = neigh->neigh_report.channel;
        if (count >= channels_size)
            break;

        if (rrm_req_type == BM_CLIENT_RRM_2G_ONLY) {
            if (channel > 13)
                continue;
        }

        if (rrm_req_type == BM_CLIENT_RRM_5G_ONLY) {
            if (channel <= 13)
                continue;
        }

        if (rrm_req_type == BM_CLIENT_RRM_OWN_CHANNEL_ONLY) {
            if (client->self_neigh.channel != channel)
                continue;
        }

        /* Skip duplicates */
        for (i = 0; i < count; i++) {
            if (channel == channels[i])
                break;
        }

        if (i == count) {
            channels[count] = channel;
            count++;
        }
    }

    if (!count)
        return count;


    /* Add self channel */
    if (rrm_req_type == BM_CLIENT_RRM_5G_ONLY && client->self_neigh.channel <= 13)
        return count;

    if (rrm_req_type == BM_CLIENT_RRM_2G_ONLY && client->self_neigh.channel > 13)
        return count;

    for (i = 0; i < count; i++) {
        if (channels[i] == client->self_neigh.channel)
            break;
    }

    if (i == count && count < channels_size) {
        if (self_first) {
            memmove(&channels[1], &channels[0], count * sizeof(channels[0]));
            channels[0] = client->self_neigh.channel;
        } else {
            channels[count] = client->self_neigh.channel;
        }
        count++;
    }

    return count;
}
