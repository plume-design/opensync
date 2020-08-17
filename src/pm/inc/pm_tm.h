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

#ifndef PM_TM_H_INCLUDED
#define PM_TM_H_INCLUDED

#include <stdbool.h>

#include "osp_tm.h"
#include "osp_led.h"

struct osp_tm_ctx
{
    struct ev_timer therm_timer;

    const struct osp_tm_therm_state *therm_tbl;
    unsigned int therm_state_cnt;
    unsigned int temp_src_cnt;

    unsigned int crit_temp_periods;
    unsigned int prev_fan_rpm;
    unsigned int prev_state;
    unsigned int fan_failure;

    int temp_avg_sum[OSP_TM_TEMP_SRC_MAX];
    int temp_avg_meas[OSP_TM_TEMP_SRC_MAX][OSP_TM_TEMP_AVG_CNT];
    unsigned int temp_avg_idx;

    void *tgt_priv;
};

// enable support for reading temperature and
// fan_rpm from the OVSDB for debugging
//#define PM_TM_DEBUG

int pm_tm_ovsdb_init(struct osp_tm_ctx *ctx);
bool pm_tm_ovsdb_is_radio_enabled(const char *if_name);
int pm_tm_ovsdb_set_radio_txchainmask(const char *if_name, unsigned int txchainmask);

int pm_tm_ovsdb_thermtbl_get_radio_temp(unsigned int state, unsigned int radio_idx, int *temp);
int pm_tm_ovsdb_thermtbl_get_radio_txchainmask(unsigned int state, unsigned int radio_idx, unsigned int *txchainmask);
int pm_tm_ovsdb_thermtbl_get_fan_rpm(unsigned int state, unsigned int *rpm);
int pm_tm_ovsdb_set_state(unsigned int state);
int pm_tm_ovsdb_set_led_state(enum osp_led_state led_state, bool clear);

#ifdef PM_TM_DEBUG
int pm_tm_ovsdb_dbg_get_temperature(unsigned int radio_idx, int *temp);
int pm_tm_ovsdb_dbg_get_fan_rpm(unsigned int *rpm);
#endif

#endif /* PM_TM_H_INCLUDED */
