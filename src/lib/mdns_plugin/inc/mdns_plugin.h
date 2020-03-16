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

#ifndef MDNS_PLUGIN_H_INCLUDED
#define MDNS_PLUGIN_H_INCLUDED
#include <pcap.h>
#include <stdint.h>
#include <time.h>
#include <ev.h>

#include "fsm.h"
#include "net_header_parse.h"
#include "os_types.h"
#include "ovsdb.h"
#include "schema.h"

#include "mdnsd.h"


/**
 * @brief container for mdns parser data
 */
struct mdns_parser
{
    struct  net_header_parser *net_parser;
    size_t  mdns_len;
    size_t  parsed;
    size_t  caplen;
    uint8_t *data;
};

struct mdns_session
{
    struct fsm_session     *session;
    bool                    initialized;
    struct mdns_parser      parser;

    ds_tree_node_t          session_node;
};

/**
 *@brief container for mdns daemon
 */
struct mdnsd_context
{
    bool                  enabled;
    mdns_daemon_t        *dmn;
    int                   mcast_fd;
    char                 *srcip;
    char                 *txintf;
    struct timeval        sleep_tv;
    ev_io                 read;
    ev_timer              timer;
    ds_tree_t             services;

    int  (*dmn_get_mcast_sock)();
    void (*dmn_ev_io_init)();
    void (*dmn_ev_timer_init)();
    void (*dmn_rcvcb)(EV_P_ ev_io *r, int revents);
    void (*dmn_timercb)(EV_P_ struct ev_timer *w, int revents);
};

struct mdnsd_service
{
    char           *name;
    char           *type;
    int             port;
    char           *target;
    char           *cname;
    ds_tree_t      *txt;
    ds_tree_node_t  service_node;
};

/**
 * @brief the plugin cache, a singleton tracking instances
 *
 * The cache tracks the global initialization of the plugin
 * and the running sessions.
 */
struct mdns_plugin_mgr
{
    bool            initialized;
    ds_tree_t       fsm_sessions;


    /* mdns daemon config */
    struct mdnsd_context   *ctxt;
    struct ev_loop  *loop;

    void (*ovsdb_init)(void);
};

/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the pcap handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int
mdns_plugin_init(struct fsm_session *session);

struct mdns_plugin_mgr
*mdns_get_mgr(void);

void
mdns_mgr_init(void);
/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
mdns_plugin_exit(struct fsm_session *session);

/**
 * @brief session packet processing entry point
 *
 * packet processing handler.
 * @param session the fsm session
 * @param net_parser the container of parsed header and original packet
 */
void
mdns_plugin_handler(struct fsm_session *session,
                    struct net_header_parser *net_parser);

void
mdns_plugin_periodic(struct fsm_session  *session);

void
mdnsd_recv_cb(EV_P_ ev_io *r, int revents);

void
mdnsd_timer_cb(EV_P_ struct ev_timer *w, int revents);

void
mdnsd_ev_io_init(void);

void
mdnsd_ev_timer_init(void);

void
mdnsd_ctxt_update(struct mdns_session *md_session);

bool
mdnsd_ctxt_init(struct mdns_session *md_session);

void
mdnsd_ctxt_exit(void);

bool
mdnsd_ctxt_start(struct mdnsd_context *pctxt);

void
mdnsd_ctxt_stop(struct mdnsd_context *pctxt);

int
mdnsd_ctxt_get_mcast_fd(void);

bool
mdnsd_ctxt_set_srcip(struct mdns_session *md_session);

bool
mdnsd_ctxt_set_txintf(struct mdns_session *md_session);

void
callback_Service_Announcement(ovsdb_update_monitor_t *mon,
                              struct schema_Service_Announcement *old_rec,
                              struct schema_Service_Announcement *conf);

void
mdns_ovsdb_init(void);

int
mdnsd_service_cmp(void *a, void *b);

ds_tree_t *
mdnsd_get_services(void);

void
mdnsd_free_service(struct mdnsd_service *service);

void
mdnsd_free_services(void);

void
mdnsd_walk_services_txt_records(struct mdnsd_service *service);

void
mdnsd_walk_services_tree(void);

struct mdnsd_service
*mdnsd_alloc_service(struct schema_Service_Announcement *conf);

bool
mdnsd_add_service(struct schema_Service_Announcement *conf);

void
mdnsd_delete_service(struct schema_Service_Announcement *conf);

bool
mdnsd_modify_service(struct schema_Service_Announcement *conf);

void
mdnsd_record_conflict(char *name, int type, void *arg);

bool
mdnsd_update_record(struct mdnsd_service *service);

void
mdnsd_remove_record(struct mdnsd_service *service);
#endif // MDNS_PLUGIN_H_INCLUDED
