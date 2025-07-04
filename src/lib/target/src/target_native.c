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

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>

#include <errno.h>

#include "ev.h"

#include "os.h"
#include "log.h"
#include "os_nif.h"
#include "os_types.h"
#include "util.h"
#include "os_util.h"
#include "os_regex.h"
#include "dpp_types.h"
#include "target_native.h"

#define MODULE_ID LOG_MODULE_ID_TARGET

/*
 *  TARGET definitions
 */

void target_managers_restart_helper(const char *calling_func)
{
    (void)calling_func;
}

bool target_is_radio_interface_ready(char *phy_name)
{
    (void)phy_name;
    return true;
}

bool target_is_interface_ready(char *if_name)
{
    (void)if_name;
    return true;
}

target_survey_record_t *target_survey_record_alloc()
{
    target_survey_record_t *ptr = malloc(sizeof(*ptr));
    return ptr;
}

void target_survey_record_free(target_survey_record_t *p)
{
    free(p);
}

bool target_stats_capacity_get(radio_entry_t *radio_cfg,
                               target_capacity_data_t *capacity_new)
{
    (void)radio_cfg;
    (void)capacity_new;
    return true;
}
//    (void)
