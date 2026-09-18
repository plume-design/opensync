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

#ifndef DM_H_INCLUDED
#define DM_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <ev.h>

#include "log.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

/*
 * OVSDB server related functions
 */
bool init_managers();
bool init_statem();

bool dm_hook_init(struct ev_loop *loop);
bool dm_hook_close();
void dm_st_in_progress_set(bool value);
bool dm_st_in_progress_get();
bool dm_manager_stop_all(char *except_mgrs);
void chkmem_init(void);
int chkmem_check_pss(int pid, char *pname, int memmax, int maxmem_cnt, int *highest, int *cnt);
bool dm_no_reboot_set(char *module);
bool dm_no_reboot_clear(char *module);
bool dm_no_reboot_get(char *module);
bool dm_no_reboot_clear_all();
bool dm_no_reboot_list();


/**
 * Register a new manager
 *
 * @param[in]   path                Path to manager (can be a full path)
 * @param[in]   plan_b              True whether manager requires plan B
 * @param[in]   always_restart      True if manager should be always restarted, even
 *                                  when killed by signals that usually do not
 *                                  trigger a restart
 * @param[in]   restart_timer       Restart timer in seconds or 0 to use default
 * @param[in]   memmax              Soft PSS memory limit in kB; -1 disables
 * @param[in]   memmax_cnt          Consecutive PSS-over-limit samples before a
 *                                  memory-crash report is triggered
 * @param[in]   crash_count_max     Crashes before entering backoff; 0 disables
 *                                  (ignored when plan_b is true)
 * @param[in]   crash_count_period  Crash counting window in seconds; 0 counts
 *                                  for the DM lifetime, >0 uses a sliding
 *                                  window (larger crash_count_max values are
 *                                  capped to the timestamp buffer size)
 * @param[in]   crash_recovery      Absolute path to recovery script/binary run
 *                                  on backoff entry, or NULL/empty for none
 */
bool dm_manager_register(
        const char *path,
        bool plan_b,
        bool always_restart,
        int restart_timer,
        int memmax,
        int memmax_cnt,
        int crash_count_max,
        int crash_count_period,
        const char *crash_recovery);

/*
 * DM cli
 */
enum dm_cli_status
{
    DM_CLI_CONTINUE,
    DM_CLI_DONE_OK,
    DM_CLI_DONE_ERR,
};

int dm_cli(int argc, char *argv[]);

#endif /* DM_H_INCLUDED */
