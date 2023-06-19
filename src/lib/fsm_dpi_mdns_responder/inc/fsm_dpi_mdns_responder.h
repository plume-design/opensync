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

#ifndef FSM_DPI_MDNS_RESPONDER_INCLUDED
#define FSM_DPI_MDNS_RESPONDER_INCLUDED

#include <arpa/nameser.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ds_tree.h"
#include "fsm.h"

enum mdns_state
{
    UNDEFINED = 0,
    BEGIN_MDNS,
    MDNS_QNAME,
    MDNS_RESP_TYPE,
    END_MDNS
};

struct mdns_record
{
    char qname[NS_MAXCDNAME];
    bool unicast;
    enum mdns_state next_state;
};

struct mdns_resp_session
{
    bool initialized;
    ds_tree_node_t next;
    struct fsm_session *session;
};

struct dpi_mdns_resp_client
{
    bool initialized;
    int  mcast_fd;
    char *srcip;
    char *txintf;

    struct mdns_record curr_mdns_rec_processed;
    ds_tree_t fsm_sessions;
    ds_tree_t services;
};

struct fsm_dpi_mdns_service
{
    char           *name;
    char           *type;
    int             port;
    char           *target;
    char           *cname;
    ds_tree_t      *txt;

    int             n_txt_recs;
    ds_tree_node_t  service_node;
};

/**
 * @brief dpi mdns manager accessor
 */
struct dpi_mdns_resp_client *
fsm_dpi_mdns_get_mgr(void);

ds_tree_t *
fsm_dpi_mdns_get_services(void);

/**
 * @brief reset mdns record
 */
void
fsm_dpi_mdns_reset_state(struct fsm_session *session);

/**
  * @brief looks up a session
  *
  * Looks up a session, and allocates it if not found.
  * @param session the session to lookup
  * @return the found/allocated session, or NULL if the allocation failed
  */
struct mdns_resp_session *fsm_dpi_mdns_get_session(struct fsm_session *session);

/**
  * @brief Frees a mdns session
  *
  * @param n_session the mdns session to delete
  */
void fsm_dpi_mdns_free_session(struct mdns_resp_session *n_session);

/**
  * @brief deletes a session
  *
  * @param session the fsm session keying the mdns session to delete
  */
void fsm_dpi_mdns_delete_session(struct fsm_session *session);

/**
 * @brief Initialize all the required structures
 *
 * @param session used to extract information about the session.
 */
int fsm_dpi_mdns_init(struct fsm_session *session);

/**
 * @brief Releases all allocated memory and un-initialize global
 *        aggregator.
 *
 * @param session
 */
void fsm_dpi_mdns_exit(struct fsm_session *session);

/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm_dpi_mdns session to update
 */
void fsm_dpi_mdns_update(struct fsm_session *session);

/**
 * @brief periodic routine
 *
 * @param session the fsm session keying the fsm_dpi_mdns session to process
 */
void fsm_dpi_mdns_periodic(struct fsm_session *session);

/**
 * @brief process specifically a mDNS flow attribute
 *
 * @param session the fsm session
 * @param attr the attribute flow
 * @param type the value type (RTS_TYPE_BINARY, RTS_TYPE_STRING or RTS_TYPE_NUMBER)
 * @param length the length in bytes of the value
 * @param value the value itself
 * @param packet_info packet details (acc, net_parser)
 */
int fsm_dpi_mdns_process_attr(struct fsm_session *session, const char *attr,
                                     uint8_t type, uint16_t length, const void *value,
                                     struct fsm_dpi_plugin_client_pkt_info *pkt_info);

void
callback_Service_Announcement(ovsdb_update_monitor_t *mon,
                              struct schema_Service_Announcement *old_rec,
                              struct schema_Service_Announcement *conf);

void
fsm_dpi_mdns_ovsdb_init(void);

bool
fsm_dpi_mdns_send_response(struct fsm_dpi_mdns_service *record, bool unicast, struct net_header_parser *net_parser);

int
fsm_dpi_mdns_create_mcastv4_socket(void);
#endif /* FSM_DPI_MDNS_RESPONDER_INCLUDED */
