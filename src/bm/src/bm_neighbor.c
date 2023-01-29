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
#include "memutil.h"
#include "bm_util_opclass.h"

/*****************************************************************************/

#define MODULE_ID LOG_MODULE_ID_NEIGHBORS

/*****************************************************************************/
static ovsdb_update_monitor_t   bm_neighbor_ovsdb_update;
static ovsdb_update_monitor_t   bm_vif_state_ovsdb_update;
static ds_tree_t                bm_neighbors = DS_TREE_INIT( ds_int_cmp,
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
bm_neighbor_get_op_class(uint8_t channel, radio_type_t rtype)
{
    /* note: these Operating Classes are 20 MHz */
    switch (rtype) {
    case RADIO_TYPE_NONE:
        break;
    case RADIO_TYPE_2G:
        if (channel >= 1 && channel <= 13) {
            return BTM_24_OP_CLASS;
        }
        break;
    case RADIO_TYPE_5G: /* passthrough */
    case RADIO_TYPE_5GL: /* passthrough */
    case RADIO_TYPE_5GU:
        if (channel >= 36 && channel <= 48) {
            return BTM_5GL_OP_CLASS;
        }

        if (channel >= 52 && channel <= 64) {
            return BTM_L_DFS_OP_CLASS;
        }

        if (channel >= 100 && channel <= 140) {
            return BTM_U_DFS_OP_CLASS;
        }

        if (channel >= 149 && channel <= 169) {
            return BTM_5GU_OP_CLASS;
        }
        break;
    case RADIO_TYPE_6G:
        if (channel >= 1 && channel <= 233) {
            return BTM_6G_OP_CLASS;
        }
        break;
    }

    return 0;
}

static radio_type_t
bm_neighbor_get_radio_type(const struct schema_Wifi_VIF_Neighbors *neigh)
{
    /*
     * If VIF with "neigh->ifname" exists just use its radio type.
     */
    if (bm_group_find_by_ifname(neigh->if_name))
        return bm_group_find_radio_type_by_ifname(neigh->if_name);

    /*
     * If there's no VIF called "neigh->ifname" it's most probably the case
     * when pod has 5 GHz VAP on 5GU, but controller filled WIF_VIF_Neighbors
     * with 5GL VAP name. In such case infer radio type from channel.
     *
     * Check at least whether channel can be a valid 5 GHz band channel.
     */
    if (neigh->channel >= 36 && neigh->channel <= 181)
        return RADIO_TYPE_5G;

    LOGW("%s: Cannot infer radio type for vifname", neigh->if_name);
    return RADIO_TYPE_NONE;
}

bool
bm_neighbor_get_self_neighbor(const char *_ifname, bsal_neigh_info_t *neigh)
{
    struct  schema_Wifi_VIF_State   vif;
    json_t                          *jrow;
    pjs_errmsg_t                    perr;
    char                            *ifname;
    os_macaddr_t                    macaddr;
    radio_type_t                    rtype;

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
    rtype = bm_group_find_radio_type_by_ifname(ifname);

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

    LOGT( "Got self channel: %d, bssid: %s ifname: %s", vif.channel, vif.mac, _ifname);
    memcpy( neigh->bssid, (uint8_t *)&macaddr, sizeof( neigh->bssid ) );

    // Assume the default BSSID_INFO
    neigh->bssid_info = BTM_DEFAULT_NEIGH_BSS_INFO;
    neigh->op_class = bm_neighbor_get_op_class(neigh->channel, rtype);
    neigh->phy_type = bm_neighbor_get_phy_type(neigh->op_class);

    json_decref(jrow);
    return true;
}

uint8_t
bm_neighbor_get_phy_type(uint8_t op_class)
{
    if (ieee80211_global_op_class_is_2ghz(op_class))
        return BTM_24_PHY_TYPE;

    if (ieee80211_global_op_class_is_5ghz(op_class))
        return BTM_5_PHY_TYPE;

    if (ieee80211_global_op_class_is_6ghz(op_class))
        return BTM_HE_PHY_TYPE;

    LOGW("Geeting phy type failed, op_class %hhu", op_class);
    return 0;
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
    radio_type_t rtype;
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

    rtype = bm_neighbor_get_radio_type(nconf);

    memcpy(&neigh->neigh_report.bssid, &bssid, sizeof(bssid));
    neigh->neigh_report.bssid_info = BTM_DEFAULT_NEIGH_BSS_INFO;
    neigh->neigh_report.channel = nconf->channel;

    neigh->neigh_report.op_class = bm_neighbor_get_op_class(nconf->channel, rtype);
    neigh->neigh_report.phy_type = bm_neighbor_get_phy_type(neigh->neigh_report.op_class);

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
                    radio_type_t rtype;
                    if (strcmp(group->ifcfg[i].ifname, vstate.if_name))
                        continue;
                    if (group->ifcfg[i].self_neigh.channel == vstate.channel)
                        continue;
                    LOGI("%s self %d new %d", vstate.if_name, group->ifcfg[i].self_neigh.channel, vstate.channel);
                    rtype = bm_group_find_radio_type_by_ifname(vstate.if_name);
                    group->ifcfg[i].self_neigh.channel = vstate.channel;
                    group->ifcfg[i].self_neigh.op_class = bm_neighbor_get_op_class(vstate.channel, rtype);
                    bm_neighbor_add_to_group_by_ifname(group, group->ifcfg[i].ifname, group->ifcfg[i].bs_allowed);
                    bm_client_update_all_from_group(group);
                }
            }

            bm_client_update_all_channel(&vstate);

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
    bool                                overwrite = false;

    switch( self->mon_type )
    {
        case OVSDB_UPDATE_NEW:
        {
            if( !schema_Wifi_VIF_Neighbors_from_json( &nconf,
                                                      self->mon_json_new, false, perr )) {
                LOGE( "Failed to parse new Wifi_VIF_Neighbors row: %s", perr );
                return;
            }

            /* Workaround for duplicated BSSID.
             * Whenever insert comes before delete for the same BSSID
             * - e.g. due to both operations being contained
             * in a single transaction - an already existing neighbor
             * will be overwritten including its uuid.
             * The following delete will trigger a warning since
             * old bssid is not present anymore.
             */

            if ((neigh = bm_neighbor_find_by_macstr(nconf.bssid))) {
                ds_tree_remove(&bm_neighbors, neigh);
                overwrite = true;
            } else {
                neigh = CALLOC( 1, sizeof( *neigh ) );
            }
            STRSCPY(neigh->uuid, nconf._uuid.uuid);

            if (!bm_neighbor_from_ovsdb(&nconf, neigh)) {
                LOGE( "Failed to convert row to neighbor info (uuid=%s) (insert-overwrite=%s)", neigh->uuid, overwrite ? "true" : "false");
                if (overwrite)
                    bm_neighbor_remove_neighbor(&neigh->neigh_report);
                FREE(neigh);
                return;
            }

            ds_tree_insert( &bm_neighbors, neigh, &neigh->priority );
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

            ds_tree_remove(&bm_neighbors, neigh);
            if (!bm_neighbor_from_ovsdb(&nconf, neigh)) {
                LOGE( "Failed to convert row to neighbor for modify (uuid=%s)", neigh->uuid );
                bm_neighbor_remove_neighbor(&neigh->neigh_report);
                FREE(neigh);
                return;
            }

            ds_tree_insert(&bm_neighbors, neigh, &neigh->priority);

            LOGN( "Updated Neighbor %s", neigh->bssid );
            bm_neighbor_set_neighbor(&neigh->neigh_report);

            break;
        }

        case OVSDB_UPDATE_DEL:
        {
            if( !( neigh = bm_neighbor_find_by_uuid( self->mon_uuid ))) {
                LOGW( "Unable to find neighbor for delete with uuid=%s", self->mon_uuid );
                return;
            }

            LOGN( "Removing neighbor %s", neigh->bssid );
            bm_neighbor_remove_neighbor(&neigh->neigh_report);
            ds_tree_remove( &bm_neighbors, neigh );
            FREE( neigh );

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
    bm_neighbor_t   *neigh;

    ds_tree_foreach( &bm_neighbors, neigh ) {
        if( !strcmp( neigh->bssid, mac_str )) {
            return neigh;
        }
    }

    return NULL;
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

        FREE( neigh );

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
bm_neighbor_channel_allowed(const bm_client_t *client, uint8_t channel, uint8_t op_class)
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
               if (ieee80211_global_op_class_is_2ghz(op_class) &&
                   channel >=1 && channel <= 13)
                   allowed = true;
               break;
           case RADIO_TYPE_5G:
           case RADIO_TYPE_5GL:
           case RADIO_TYPE_5GU:
               if (ieee80211_global_op_class_is_5ghz(op_class) &&
                   channel >=13 && channel <= 177)
                   allowed = true;
               break;
           case RADIO_TYPE_6G:
               if (ieee80211_global_op_class_is_6ghz(op_class) &&
                   channel >= 1 && channel <= 233)
                   allowed = true;
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

static bool
bm_neighbor_in_group(const bm_neighbor_t *neighbor, const bm_group_t *group)
{
    unsigned int i;

    if (!group) return false;

    for (i = 0; i < group->ifcfg_num; i++) {
        if(!strcmp(neighbor->ifname, group->ifcfg[i].ifname)) {
            return true;
        }
    }
    return false;
}

unsigned int
bm_neighbor_number(bm_client_t *client)
{
    int neighbors = 0;
    bm_neighbor_t *bm_neigh;

    ds_tree_foreach(&bm_neighbors, bm_neigh) {
        if (!bm_neighbor_channel_allowed(client, bm_neigh->channel, bm_neigh->neigh_report.op_class))
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
        if (!bm_neighbor_channel_allowed(client, bm_neigh->channel, bm_neigh->neigh_report.op_class))
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
    unsigned int i;

    btm_params->num_neigh = 0;

    for (i = 0; i < client->ifcfg_num; i++) {
        if (client->ifcfg[i].bs_allowed != bs_allowed) {
            continue;
        }

        if (client->group != client->ifcfg[i].group) {
            continue;
        }

        if (client->ifcfg[i].radio_type == RADIO_TYPE_6G &&
            !(client->band_cap_mask & BM_CLIENT_OPCLASS_60_CAP_BIT)) {
            continue;
        }

        _bm_neighbor_get_self_btm_values(btm_params, client->ifcfg[i].ifname);
   }

   if (btm_params->num_neigh) {
       btm_params->pref = 1;
   } else {
       LOGI("Client '%s': empty self btm values", client->mac_addr);
       btm_params->pref = 0;
   }

   return btm_params->num_neigh > 0;
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

/*
 * This function is expected to return TRUE solely when "bm_neigh" is better
 * than self, i.e. from STA perspective neighbor has higher RCPI than self.
 *
 * This condition can be satisfied only when:
 * - BM has warm Beacon Measurement Report from STA
 * - Beacon Measurement Report contains entries for neighbor and self
 * - neigh's RCPI is better than self's RCPI
 */
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
        return false;
    }

    if ((unsigned int) (now - rrm_self_neigh->time) > client->rrm_age_time) {
        LOGD("%s rrm results too old, don't use them %u", client->mac_addr,
             (unsigned int) (now - rrm_self_neigh->time));
        return false;
    }

    rrm_neigh = bm_neighbor_get_rrm_neigh(client, (os_macaddr_t *) bm_neigh->neigh_report.bssid);
    if (!rrm_neigh) {
        LOGD("%s no rrm_neigh, assume client don't see %s", client->mac_addr, bm_neigh->bssid);
        return false;
    }

    if ((unsigned int) (now - rrm_neigh->time) > client->rrm_age_time) {
        LOGD("%s rrm results too old, don't use them %u", client->mac_addr,
             (unsigned int) (now - rrm_neigh->time));
        return false;
    }

    /* Finally compare rcpi */
    if (rrm_neigh->rcpi < rrm_self_neigh->rcpi + 2 * client->rrm_better_factor) {
        LOGD("[%s]: neigh %s self %u neigh %u worst rcpi", client->mac_addr, bm_neigh->bssid, rrm_self_neigh->rcpi, rrm_neigh->rcpi);
        return false;
    }

    return true;
}

static bool
bm_neigbour_op_class_allowed(bm_client_t *client, bm_neighbor_t *bm_neigh)
{
    int i;
    const uint8_t op_class = bm_neigh->neigh_report.op_class;

    if (!op_class) {
        LOGW("%s op class not defined", bm_neigh->bssid);
        return false;
    }

    LOGD("Client '%s' checking supported op classes", client->mac_addr);

    if (client->op_classes.size == 0) {
        LOGD("%s: client %s has nos no op_classes, accepting unconditionally",
              bm_neigh->bssid, client->mac_addr);

        if (ieee80211_global_op_class_is_6ghz(op_class))
            return (client->band_cap_mask & BM_CLIENT_OPCLASS_60_CAP_BIT);

        return true;
    }

    for (i = 0; i < client->op_classes.size; i++) {
        const uint8_t op_c = client->op_classes.op_class[i];

        if (ieee80211_global_op_class_is_contained_in(op_class, op_c))
            return true;

        if (ieee80211_global_op_class_is_contained_in(op_c, op_class))
            return true;

        if (op_c == op_class)
            return true;
    }

    return false;
}

static int bm_cli_neigh_better_rrm_rcpi(const void *a, const void *b) {
    bm_rrm_neighbor_t *rrm_neigh_a;
    bm_rrm_neighbor_t *rrm_neigh_b;
    const bm_client_neighbor_t *cli_neigh_a = (bm_client_neighbor_t *) a;
    const bm_client_neighbor_t *cli_neigh_b = (bm_client_neighbor_t *) b;
    bm_neighbor_t *neigh_a = (bm_neighbor_t *) cli_neigh_a->neighbor;
    bm_neighbor_t *neigh_b = (bm_neighbor_t *) cli_neigh_b->neighbor;

    // descending order,  push neighbors without rrm report towards the end
    rrm_neigh_a = bm_neighbor_get_rrm_neigh(cli_neigh_a->client, (os_macaddr_t *) neigh_a->neigh_report.bssid);
    if (!rrm_neigh_a) return 1;
    rrm_neigh_b = bm_neighbor_get_rrm_neigh(cli_neigh_b->client, (os_macaddr_t *) neigh_b->neigh_report.bssid);
    if (!rrm_neigh_b) return -1;
    if (rrm_neigh_a->rcpi > rrm_neigh_b->rcpi) return -1;
    if (rrm_neigh_a->rcpi < rrm_neigh_b->rcpi) return 1;
    return 0;
}

bool
bm_neighbor_build_btm_neighbor_list( bm_client_t *client, bsal_btm_params_t *btm_params )
{
    ds_tree_t                   *bm_neighbors   = NULL;
    bm_neighbor_t               *bm_neigh       = NULL;

    os_macaddr_t                macaddr;
    int                         max_regular_neighbors;
    int                         max_self_neighbors;
    unsigned int                i = 0;
    unsigned int                cand_bm_cli_neigh_len = 256;
    unsigned int                cand_bm_cli_neigh_size = 0;
    bm_client_neighbor_t        cand_bm_cli_neigh_list[cand_bm_cli_neigh_len];

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

    if (client->band_cap_mask & BM_CLIENT_OPCLASS_60_CAP_BIT) {
        max_regular_neighbors = BSAL_MAX_TM_NEIGHBORS;
        max_self_neighbors = 2;
    }
    else {
        max_regular_neighbors = BSAL_MAX_TM_NEIGHBORS_LEGACY;
        max_self_neighbors = 1;
    }

    if (btm_params->inc_self)
        /* Leave place for self neighbors */
        max_regular_neighbors -= max_self_neighbors;

    /* First choose neighbors using Beacon Measurement Reports (if enabled or available) */
    if (client->neighbor_list_filter_by_beacon_report ) {
        ds_tree_foreach(bm_neighbors, bm_neigh) {

            // Include only allowed neighbors
            if (!bm_neighbor_channel_allowed(client, bm_neigh->channel, bm_neigh->neigh_report.op_class)) {
                LOGT("Skipping neighbor = %s, channel = %hhu", bm_neigh->bssid, bm_neigh->channel);
                continue;
            }

            if (!bm_neighbor_better(client, bm_neigh)) {
                LOGI("[%s] skipping neighbor %s - not better than current", client->mac_addr, bm_neigh->bssid);
                continue;
            }

            // Filter out neighbors not in a group
            if (!bm_neighbor_in_group(bm_neigh, client->group)) {
                LOGT("Skipping neighbor = %s, ifname = %s", bm_neigh->bssid, bm_neigh->ifname);
                continue;
            }

            cand_bm_cli_neigh_list[cand_bm_cli_neigh_size].client = client;
            cand_bm_cli_neigh_list[cand_bm_cli_neigh_size].neighbor = bm_neigh;
            cand_bm_cli_neigh_size++;

            if (cand_bm_cli_neigh_size == cand_bm_cli_neigh_len) {
                LOGE("Candidate neighbor list length limit reached");
                break;
            }
        }
    }

    /* Sort chosen neighbors by RCPI reported by client using rrm */
    qsort(cand_bm_cli_neigh_list, cand_bm_cli_neigh_size, sizeof(bm_client_neighbor_t), bm_cli_neigh_better_rrm_rcpi);

    /* Add neighbors from list sorted by RCPI */
    for (i=0; i<cand_bm_cli_neigh_size; i++) {
        bm_neigh = cand_bm_cli_neigh_list[i].neighbor;
        bsal_neigh_info_t *bsal_neigh = NULL;

        if (btm_params->num_neigh >= max_regular_neighbors) {
            LOGT("Built maximum allowed neighbors");
            break;
        }

        bsal_neigh = &btm_params->neigh[btm_params->num_neigh];

        if (!os_nif_macaddr_from_str(&macaddr, bm_neigh->bssid)) {
            LOGE("Unable to parse mac address '%s'", bm_neigh->bssid);
            return false;
        }

        memcpy(bsal_neigh->bssid, (uint8_t *)&macaddr, sizeof(bsal_neigh->bssid));
        bsal_neigh->channel = bm_neigh->channel;
        bsal_neigh->bssid_info = bm_neigh->neigh_report.bssid_info;
        bsal_neigh->op_class = bm_neigh->neigh_report.op_class;
        bsal_neigh->phy_type = bm_neigh->neigh_report.phy_type;

        btm_params->num_neigh++;

        LOGT("Built neighbor [%d] (beacon report): %s channel: %hhu bssid: %hhu op_class: %hhu phy_type: %hhu",
              btm_params->num_neigh, bm_neigh->bssid, bm_neigh->channel, bm_neigh->neigh_report.bssid_info,
              bm_neigh->neigh_report.op_class, bm_neigh->neigh_report.phy_type);
    }

    /* Fill the remaining space with other neighbors */
    ds_tree_foreach(bm_neighbors, bm_neigh) {
        bsal_neigh_info_t *bsal_neigh = NULL;
        bool skip_neigh = false;
        int neigh_i = 0;

        if (btm_params->num_neigh >= max_regular_neighbors) {
            LOGT("Built maximum allowed neighbors");
            break;
        }

        if (!os_nif_macaddr_from_str(&macaddr, bm_neigh->bssid)) {
            LOGE("Unable to parse mac address '%s'", bm_neigh->bssid);
            return false;
        }

        for (neigh_i = 0; neigh_i < btm_params->num_neigh; neigh_i++) {
            const bsal_neigh_info_t *neigh = &btm_params->neigh[neigh_i];
            if (memcmp(neigh->bssid, (uint8_t *)&macaddr, sizeof(neigh->bssid)) == 0) {
                skip_neigh = true;
                break;
            }
        }

        if (skip_neigh) {
            LOGT("Skipping neighbor = %s, channel = %hhu (it's already on the list)",
                 bm_neigh->bssid, bm_neigh->channel);
            continue;
        }

        // Include only allowed neighbors
        if (!bm_neighbor_channel_allowed(client, bm_neigh->channel, bm_neigh->neigh_report.op_class)) {
            LOGT("Skipping neighbor = %s, channel = %hhu", bm_neigh->bssid, bm_neigh->channel);
            continue;
        }

        if (!bm_neigbour_op_class_allowed(client, bm_neigh)) {
            LOGT("Skipping neighbor = %s due too op_class mismatch", bm_neigh->bssid);
            continue;
        }

        // Filter out neighbors not in a group
        if (!bm_neighbor_in_group(bm_neigh, client->group)) {
            LOGT("Skipping neighbor = %s, ifname = %s", bm_neigh->bssid, bm_neigh->ifname);
            continue;
        }

        bsal_neigh = &btm_params->neigh[btm_params->num_neigh];

        memcpy( bsal_neigh->bssid, (uint8_t *)&macaddr, sizeof( bsal_neigh->bssid ) );
        bsal_neigh->channel = bm_neigh->channel;
        bsal_neigh->bssid_info = bm_neigh->neigh_report.bssid_info;
        bsal_neigh->op_class = bm_neigh->neigh_report.op_class;
        bsal_neigh->phy_type = bm_neigh->neigh_report.phy_type;

        btm_params->num_neigh++;

        LOGT("Built neighbor [%d]: %s channel: %hhu bssid: %hhu op_class: %hhu phy_type: %hhu",
              btm_params->num_neigh, bm_neigh->bssid, bm_neigh->channel, bm_neigh->neigh_report.bssid_info,
              bm_neigh->neigh_report.op_class, bm_neigh->neigh_report.phy_type);
    }

    if (btm_params->inc_self && btm_params->num_neigh) {
        int neigh_i = 0;
        bool skip_self = false;

        for (i = 0; i < client->group->ifcfg_num; i++) {
            if (!bm_neighbor_channel_allowed(client, client->group->ifcfg[i].self_neigh.channel, client->group->ifcfg[i].self_neigh.op_class))
                continue;

            if (btm_params->num_neigh > max_regular_neighbors + max_self_neighbors) {
                LOGI("%s client %s unable to add all self BSSIDs", __func__, client->mac_addr);
                break;
            }

            /* Skip 6GHz self neighbor for clients without 6GHz capability */
            if (ieee80211_global_op_class_is_6ghz(client->group->ifcfg[i].self_neigh.op_class) &&
                !(client->band_cap_mask & BM_CLIENT_OPCLASS_60_CAP_BIT))
                continue;

            if (!bm_neighbor_get_self_neighbor(client->group->ifcfg[i].bsal.ifname,
                &btm_params->neigh[btm_params->num_neigh])) {
                LOGW("get self for sticky 11v neighbor list failed for %s",
                     client->group->ifcfg[i].bsal.ifname);
                return false;
            }

            for (neigh_i = 0; neigh_i < btm_params->num_neigh; neigh_i++) {
                const bsal_neigh_info_t *neigh = &btm_params->neigh[neigh_i];
                if (memcmp(neigh->bssid, btm_params->neigh[btm_params->num_neigh].bssid, sizeof(neigh->bssid)) == 0) {
                    skip_self = true;
                    break;
                }
            }

            if (skip_self)
                LOGT("Client %s skip self bssid, included on the list", client->mac_addr);
            else
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
        if (!bm_neighbor_in_group(neigh, group))
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
        if (!bm_neighbor_in_group(neigh, group))
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
                         uint8_t *channels, int channels_size, int self_first,
                         uint8_t *op_classes, int op_classes_size)
{
    uint8_t channel;
    bm_neighbor_t *neigh;
    radio_type_t self_rtype;
    uint8_t self_op_class;
    uint8_t op_class;
    int count;
    int i;

    memset(channels, 0, channels_size);
    memset(op_classes, 0, op_classes_size);
    count = 0;

    if (WARN_ON(channels_size != op_classes_size))
        return 0;

    self_rtype = bm_group_find_radio_type_by_ifname(client->ifname);
    self_op_class = bm_neighbor_get_op_class(client->self_neigh.channel, self_rtype);

    if (rrm_req_type == BM_CLIENT_RRM_OWN_BAND_ONLY) {
        switch (self_rtype) {
            case RADIO_TYPE_NONE:
                LOGW("%s: unknown radio type on %s", __func__, client->ifname);
                return 0;
            case RADIO_TYPE_2G:
                rrm_req_type = BM_CLIENT_RRM_2G_ONLY;
                break;
            case RADIO_TYPE_5G: /* fallthrough */
            case RADIO_TYPE_5GL: /* fallthrough */
            case RADIO_TYPE_5GU:
                rrm_req_type = BM_CLIENT_RRM_5G_ONLY;
                break;
            case RADIO_TYPE_6G:
                rrm_req_type = BM_CLIENT_RRM_6G_ONLY;
                break;
        }
    }

    ds_tree_foreach(&bm_neighbors, neigh) {
        if (count >= channels_size)
            break;

        channel = neigh->neigh_report.channel;
        op_class = neigh->neigh_report.op_class;

        if (rrm_req_type == BM_CLIENT_RRM_2G_ONLY &&
            !ieee80211_global_op_class_is_2ghz(op_class))
                continue;

        if (rrm_req_type == BM_CLIENT_RRM_5G_ONLY &&
            !ieee80211_global_op_class_is_5ghz(op_class))
                continue;

        if (rrm_req_type == BM_CLIENT_RRM_6G_ONLY &&
            !ieee80211_global_op_class_is_6ghz(op_class))
                continue;

        if (rrm_req_type == BM_CLIENT_RRM_5_6G_ONLY &&
            !ieee80211_global_op_class_is_5ghz(op_class) &&
            !ieee80211_global_op_class_is_6ghz(op_class))
                continue;

        if (rrm_req_type == BM_CLIENT_RRM_OWN_CHANNEL_ONLY) {
            if (!ieee80211_global_op_class_is_contained_in(op_class, self_op_class) || client->self_neigh.channel != channel)
                continue;
        }

        if (!ieee80211_global_op_class_is_channel_supported(op_class, channel)) {
            LOGW("Channel %d is not supported for the opclass: %d", channel, op_class);
                continue;
        }

        /* Skip duplicates */
        for (i = 0; i < count; i++) {
            if (channel == channels[i] && op_class == op_classes[i])
                break;
        }

        if (i == count) {
            channels[count] = channel;
            op_classes[count] = op_class;
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
            op_classes[0] = self_op_class;
        } else {
            channels[count] = client->self_neigh.channel;
            op_classes[count] = self_op_class;
        }
        count++;
    }

    return count;
}
