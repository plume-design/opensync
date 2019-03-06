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
static ds_tree_t                bm_neighbors = DS_TREE_INIT( (ds_key_cmp_t *)strcmp,
                                                             bm_neighbor_t,
                                                             dst_node );

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


/*****************************************************************************/
static bool
bm_neighbor_from_ovsdb( struct schema_Wifi_VIF_Neighbors *nconf, bm_neighbor_t *neigh )
{
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

    return true;
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

            break;
        }

        case OVSDB_UPDATE_DEL:
        {
            if( !( neigh = bm_neighbor_find_by_uuid( self->mon_uuid ))) {
                LOGE( "Unable to find neighbor for delete with uuid=%s", self->mon_uuid );
                return;
            }

            LOGN( "Removing neighbor %s", neigh->bssid );
            ds_tree_remove( &bm_neighbors, neigh );
            free( neigh );

            break;
        }

        default:
            break;
    }

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
