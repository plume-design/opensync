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

#ifndef CM2_H_INCLUDED
#define CM2_H_INCLUDED

#include "schema.h"
#include "ds_list.h"
#include "ev.h"

typedef enum
{
    CM2_STATE_INIT,
    CM2_STATE_TRY_RESOLVE,
    CM2_STATE_RE_CONNECT,
    CM2_STATE_TRY_CONNECT,
    CM2_STATE_CONNECTED,
    CM2_STATE_INTERNET,
    CM2_STATE_QUIESCE_OVS,
    CM2_STATE_NUM,
} cm2_state_e;

extern char *cm2_state_name[];

// update reason
typedef enum
{
    CM2_REASON_TIMER,
    CM2_REASON_AWLAN,
    CM2_REASON_MANAGER,
    CM2_REASON_CHANGE,
    CM2_REASON_NUM,
} cm2_reason_e;

extern char *cm2_reason_name[];

typedef enum
{
    CM2_DEST_REDIR,
    CM2_DEST_MANAGER,
} cm2_dest_e;

#define CM2_RESOURCE_MAX 512
#define CM2_HOSTNAME_MAX 256
#define CM2_PROTO_MAX 6

typedef struct
{
    bool updated;
    bool valid;
    bool resolved;
    char resource[CM2_RESOURCE_MAX];
    char proto[CM2_PROTO_MAX];
    char hostname[CM2_HOSTNAME_MAX];
    int  port;
    struct addrinfo *ai_list;
    struct addrinfo *ai_curr;
} cm2_addr_t;

typedef struct
{
    cm2_state_e     state;
    cm2_reason_e    reason;
    cm2_dest_e      dest;
    bool            state_changed;
    bool            connected;
    time_t          timestamp;
    int             disconnects;
    cm2_addr_t      addr_redirector;
    cm2_addr_t      addr_manager;
    ev_timer        timer;
    struct ev_loop *loop;
    bool have_manager;
    bool have_awlan;
    int min_backoff;
    int max_backoff;
} cm2_state_t;

extern cm2_state_t g_state;


int cm2_ovsdb_init(void);
void cm2_event_init(struct ev_loop *loop);
void cm2_event_close(struct ev_loop *loop);

bool cm2_ovsdb_set_Manager_target(char *target);
bool cm2_ovsdb_set_AWLAN_Node_manager_addr(char *addr);
void cm2_update_state(cm2_reason_e reason);
void cm2_trigger_update(cm2_reason_e reason);
bool cm2_set_addr(cm2_dest_e dest, char *resource);


#endif
