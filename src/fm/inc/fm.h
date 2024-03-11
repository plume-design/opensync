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

#ifndef FM_H_INCLUDED
#define FM_H_INCLUDED

#include <stdbool.h>

#include "ev.h"

#define FM_MESSAGES_ORIGINAL CONFIG_FM_LOG_PATH "/" CONFIG_FM_LOG_FILE
#define FM_MESSAGES_ORIGINAL_ROTATED CONFIG_FM_LOG_PATH "/" CONFIG_FM_LOGFILE_MONITOR
#define FM_MESSAGES_LIVECOPY CONFIG_FM_LOG_FLASH_ARCHIVE_PATH "/" CONFIG_FM_LOG_FILE
#define FM_MESSAGES_LIVECOPY_ROTATED CONFIG_FM_LOG_FLASH_ARCHIVE_PATH "/" CONFIG_FM_LOGFILE_MONITOR
#define FM_CRASH_BT_MESSAGES CONFIG_FM_LOG_FLASH_ARCHIVE_PATH "/" CONFIG_FM_CRASH_LOG_DIR

#define FM_PERIODIC_TIMER 10.0 /* 10 seconds */
#define FM_READ_BLOCK_SIZE 1024
#define FM_RAMOOPS_BUFFER CONFIG_FM_RAMOOPS_BUFFER
#define FM_RAMOOPS_HEADER "LOG "
#define FM_PERSISTENT_STATE CONFIG_FM_LOG_PERSISTENT_STATE

typedef struct
{
    bool fm_log_flash;
    bool fm_log_ramoops;
} fm_log_type_t;

void fm_event_init(struct ev_loop *loop);
void fm_set_logging(const fm_log_type_t options);
int fm_set_persistent(const bool persist, const char *buf, const int buf_size);
int fm_get_persistent(char *buf, const int buf_size);
int fm_ovsdb_init(void);
int fm_ovsdb_set_default_log_state(void);

#endif /* FM_H_INCLUDED */
