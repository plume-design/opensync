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

#include <ox_table_singleton.h>
#include <ox_table_instance.h>  // FIXME: does not belong here
#include <ovsdb_sync.h>

os_tr181_error_t ox_table_singleton_auto_get(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    const char *table = route->ovsdb_table;
    const char *column = route->ovsdb_column;
    json_t *where_owned = json_array();
    json_t *rows_owned = ovsdb_sync_select_where2(table, where_owned);
    json_t *first_row_borrowed = json_array_get(rows_owned, 0);
    json_t *value_borrowed = json_object_get(first_row_borrowed, column);
    if (value_borrowed == NULL)
    {
        json_decref(rows_owned);
        return OS_TR181_ERROR;
    }
    json_t *converted_value_owned = ox_table_instance_xlate_from_ovsdb(value_borrowed, route, table_instance);
    json_decref(rows_owned);
    const os_tr181_error_t err = os_val_from_json(tr181_value, converted_value_owned);
    json_decref(converted_value_owned);
    return err;
}
