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

#ifndef FSM_H_INCLUDED
#define FSM_H_INCLUDED

#include <ev.h>
#include <pcap.h>
#include <time.h>

#include "os_types.h"
#include "schema.h"
#include "ds_tree.h"

typedef enum {
    FSM_NO_HANDLER = -1,
    FSM_HTTP_ID = 0,
    FSM_DNS_ID = 1,
    FSM_DHCP_ID = 2,
    FSM_UPNP_ID = 3,
    FSM_FLOWS_ID = 4,
    FSM_NUM_HANDLER_IDS = 5,
} fsm_topic_ids;

typedef enum {
    FSM_NO_HEADER = -1,
    FSM_HEADER_LOCATION_ID = 0,
    FSM_HEADER_NODE_ID = 1,
    FSM_NUM_HEADER_IDS = 2,
} fsm_header_ids;

struct fsm_pcaps {
    pcap_t *pcap;
    struct bpf_program *bpf;
    int pcap_fd;
    ev_io fsm_evio;
};

struct fsm_session {
    struct schema_Flow_Service_Manager_Config *conf;
    struct fsm_pcaps *pcaps;
    char *topic;
    void (*send_report)(struct fsm_session *, char *);
    void (*handler)(uint8_t *, const struct pcap_pkthdr *, const uint8_t *);
    void *handler_ctxt;
    void (*update)(struct fsm_session *);
    void (*periodic)(struct fsm_session *);
    void (*exit)(struct fsm_session *);
    void *handle;
    char *session_mqtt_headers[FSM_NUM_HEADER_IDS];
    char mqtt_val[256];
    char dso[256];
    ds_tree_node_t fsm_node;
    int64_t report_count;
    struct ev_loop *loop;
    bool has_topic;
    bool has_awlan_headers;
    char name[256];
    char dso_init[256];
};

struct fsm_mgr {
    struct ev_loop *loop;
    ds_tree_t mqtt_topics;
    ds_tree_t fsm_sessions;
    char *mqtt_headers[FSM_NUM_HEADER_IDS];
    ev_timer timer;
    char pid[16];
};

struct mem_usage {
    int curr_real_mem;
    int peak_real_mem;
    int curr_virt_mem;
    int peak_virt_mem;
};

bool fsm_pcap_open(struct fsm_session *session);
void fsm_pcap_close(struct fsm_session *session);
struct fsm_mgr *fsm_get_mgr(void);
ds_tree_t *fsm_get_sessions(void);
int fsm_ovsdb_init(void);
void fsm_event_init(void);
void fsm_event_close(void);
void fsm_get_memory(struct mem_usage *mem);
#endif /* FSM_H_INCLUDED */
