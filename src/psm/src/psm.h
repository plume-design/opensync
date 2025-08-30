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

#ifndef PSM_H_INCLUDED
#define PSM_H_INCLUDED

#include <jansson.h>

/** Persistent store */
#define PSM_STORE                   "ovsdb_persist"
/** Column/key used as persistence flag */
#define PSM_TABLE_KEY               "os_persist"

#define PSM_DEBOUNCE_TIME           0.5
#define PSM_DEBOUNCE_TIME_MAX       3.0
/*
 * The first debounce interval must way larger in order to absorb in all OVS
 * changes without causing too much rewrites of the current persistent data.
 * This is the factor that PSM_DEBOUNCE_TIME and PSM_DEBOUNCE_TIME_MAX are
 * multiplied to calculate the initial timers.
 */
#define PSM_DEBOUNCE_INIT_FACTOR    10.0

bool psm_ovsdb_schema_init(bool monitor);
bool psm_ovsdb_schema_column_exists(const char *table, const char *column);
bool psm_ovsdb_schema_column_is_ephemeral(const char *table, const char *column);

bool psm_ovsdb_row_init(void);
bool psm_ovsdb_row_update(const char *table, json_t *row);
bool psm_ovsdb_row_delete(const char *table, json_t *row);
bool psm_ovsdb_row_restore(void);

void psm_erase_config_init(void);

#endif /* PSM_H_INCLUDED */
