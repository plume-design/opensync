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

#ifndef SM_LAT_OVSDB_H_INCLUDED
#define SM_LAT_OVSDB_H_INCLUDED

#include <ovsdb_table.h>

struct sm_lat_ovsdb;
typedef struct sm_lat_ovsdb sm_lat_ovsdb_t;

#ifdef CONFIG_SM_LATENCY_STATS
sm_lat_ovsdb_t *sm_lat_ovsdb_alloc(void);
void sm_lat_ovsdb_drop(sm_lat_ovsdb_t *o);
bool sm_lat_ovsdb_update_stats(sm_lat_ovsdb_t *o, ovsdb_update_monitor_t *mon);
void sm_lat_ovsdb_update_awlan(sm_lat_ovsdb_t *o, ovsdb_update_monitor_t *mon);
void sm_lat_ovsdb_update_wifi_inet_config(sm_lat_ovsdb_t *o, ovsdb_update_monitor_t *mon);
void sm_lat_ovsdb_update_wifi_vif_state(sm_lat_ovsdb_t *o, ovsdb_update_monitor_t *mon);
#else  /* CONFIG_SM_LATENCY_STATS */
sm_lat_ovsdb_t *sm_lat_ovsdb_alloc(void)
{
    return NULL;
}
void sm_lat_ovsdb_drop(sm_lat_ovsdb_t *o)
{
}
bool sm_lat_ovsdb_update_stats(sm_lat_ovsdb_t *o, ovsdb_update_monitor_t *mon)
{
    return false;
}
void sm_lat_ovsdb_update_awlan(sm_lat_ovsdb_t *o, ovsdb_update_monitor_t *mon)
{
}
void sm_lat_ovsdb_update_wifi_inet_config(sm_lat_ovsdb_t *o, ovsdb_update_monitor_t *mon)
{
}
void sm_lat_ovsdb_update_wifi_vif_state(sm_lat_ovsdb_t *o, ovsdb_update_monitor_t *mon)
{
}
#endif /* CONFIG_SM_LATENCY_STATS */

#endif /* SM_LAT_OVSDB_H_INCLUDED */
