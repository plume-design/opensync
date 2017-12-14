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

#ifndef OVSDB_SYNC_H_INCLUDED
#define OVSDB_SYNC_H_INCLUDED

#include <stdarg.h>
#include <stdbool.h>
#include <jansson.h>

#include "ovsdb.h"

// ovsdb sync api

json_t* ovsdb_where_simple(const char *column, const char *value);
json_t* ovsdb_where_uuid(const char *column, const char *uuid);
json_t* ovsdb_mutation(char *column, json_t *mutation, json_t *value);
int     ovsdb_get_update_result_count(json_t *result, char *table, char *oper);
bool    ovsdb_get_insert_result_uuid(json_t *result, char *table, char *oper, ovs_uuid_t *uuid);
json_t* ovsdb_sync_select_where(char *table, json_t *where);
json_t* ovsdb_sync_select(char *table, char *column, char *value);
bool    ovsdb_sync_insert(char *table, json_t *row, ovs_uuid_t *uuid);
int     ovsdb_sync_delete_where(char *table, json_t *where);
int     ovsdb_sync_update_where(char *table, json_t *where, json_t *row);
int     ovsdb_sync_update(char *table, char *column, char *value, json_t *row);
int     ovsdb_sync_update_get_uuid(char *table, json_t *where, json_t *row, ovs_uuid_t *uuid);
bool    ovsdb_sync_upsert_where(char *table, json_t *where, json_t *row, ovs_uuid_t *uuid);
bool    ovsdb_sync_upsert(char *table, char *column, char *value, json_t *row, ovs_uuid_t *uuid);
int     ovsdb_sync_mutate_uuid_set(char *table, json_t *where, char *column, ovsdb_tro_t op, char *uuid);
bool    ovsdb_sync_insert_with_parent(char *table, json_t *row, ovs_uuid_t *uuid,
        char *parent_table, json_t *parent_where, char *parent_column);
bool    ovsdb_sync_upsert_with_parent(char *table, json_t *where, json_t *row, ovs_uuid_t *uuid,
        char *parent_table, json_t *parent_where, char *parent_column);
int     ovsdb_sync_delete_with_parent(char *table, json_t *where,
        char *parent_table, json_t *parent_where, char *parent_column);

#endif

