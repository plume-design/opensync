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

#ifndef CELLM_MGR_H_INCLUDED
#define CELLM_MGR_H_INCLUDED

#include <ev.h>          // libev routines
#include <time.h>
#include <sys/sysinfo.h>

#include "const.h"
#include "ds_tree.h"
#include "osn_types.h"
#include "schema.h"
#include "cell_info.h"
#include "osn_cell_modem.h"

// Intervals and timeouts in seconds
#define CELLM_TIMER_INTERVAL   1
#define CELLM_MQTT_INTERVAL     60
#define CELLM_MODEM_INFO_INTERVAL 60

enum  cellm_header_ids
{
    CELLM_NO_HEADER          = -1,
    CELLM_HEADER_LOCATION_ID =  0,
    CELLM_HEADER_NODE_ID     =  1,
    CELLM_NUM_HEADER_IDS     =  2,
};

enum  cellm_interface_state
{
    CELLM_INT_STATE_UNKNOWN,
    CELLM_INT_STATE_INIT,
    CELLM_INT_STATE_UP,
    CELLM_INT_STATE_DOWN,
    CELLM_INT_STATE_NUM,
};

typedef struct cellm_handlers_
{
    bool (*cellm_mgr_init) (struct ev_loop *loop);
    int (*system_call) (const char *cmd);
} cellm_handlers_t;

typedef struct cellm_mgr_
{
    struct ev_loop *loop;
    ev_timer timer;                  /* manager's event timer */
    time_t periodic_ts;              /* periodic timestamp */
    time_t mqtt_periodic_ts;         /* periodic timestamp for MQTT reports */
    time_t log_modem_info_ts;        /* periodic timestamp to log modem info */
    time_t init_time;                /* init time */
    char pid[16];                    /* manager's pid */
    struct sysinfo sysinfo;          /* system information */
    enum cellm_interface_state cell_state;
    cellm_handlers_t handlers;
    osn_cell_modem_info_t *modem_info;
    time_t mqtt_interval;
    char topic[256];
    char node_id[64];
    char location_id[64];
} cellm_mgr_t;

bool cellm_init_mgr(struct ev_loop *loop);
cellm_mgr_t *cellm_get_mgr(void);
void cellm_event_init(void);
void cellm_set_cell_state(enum cellm_interface_state cell_state);
char *cellm_get_cell_state_name(enum cellm_interface_state state);
void cellm_event_init(void);
void cellm_reset_modem(void);
int cellm_build_mqtt_report(time_t now);
int cellm_set_mqtt_topic(void);
void cellm_dump_modem_info(void);
int cellm_serialize_report(void);
void cellm_mqtt_cleanup(void);
bool cellm_init_cell_modem(void);
void cellm_fini_cell_modem(void);
int cellm_ovsdb_init(void);

#endif /* CELLM_MGR_H_INCLUDED */
