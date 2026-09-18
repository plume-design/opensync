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

#ifndef OX_ROUTE_H_INCLUDED
#define OX_ROUTE_H_INCLUDED

#include <ox_types.h>
#include <stdbool.h>

bool ox_route_is_table(const ox_route_t *route);

bool ox_route_is_param(const ox_route_t *route);

bool ox_route_is_object(const ox_route_t *route);

const ox_route_table_t *ox_route_as_table(const ox_route_t *route);

const ox_route_param_t *ox_route_as_param(const ox_route_t *route);

const ox_route_object_t *ox_route_as_object(const ox_route_t *route);

bool ox_route_is_valid(const ox_route_t *route);

void ox_route_param_path_to_instance_path(const char *param_path, char *instance_path, size_t instance_path_size);

char *ox_route_param_extract_instance_base_path(const ox_route_param_t *route_param, const char *param_path);

os_tr181_error_t ox_route_handle_get(const char *param_path, os_tr181_val_t *value, void *user_data);

os_tr181_error_t ox_route_handle_set(const char *param_path, const os_tr181_val_t *value, void *user_data);

os_tr181_error_t ox_route_handle_add(
        const char *object_path,
        int instance_num,
        const os_tr181_val_t *initial_values,
        void *user_data);

os_tr181_error_t ox_route_handle_del(const char *param_path, int instance_num, void *user_data);

os_tr181_error_t ox_route_handle_get_uuid(const char *param_path, os_tr181_val_t *value, void *user_data);

char *ox_route_sibling_row_to_primary_uuid(const ox_route_table_t *route_table, json_t *sibling_row_borrowed);

json_t *ox_route_row_from_uuid(const ox_route_table_t *route_table, const char *uuid);

char *ox_route_param_path_for_instance(const ox_route_param_t *param_route, ox_table_instance_t *table_instance);

#endif /* OX_ROUTE_H_INCLUDED */
