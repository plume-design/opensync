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

#ifndef OX_LOG_H_INCLUDED
#define OX_LOG_H_INCLUDED

#define PARAM_NAME(param_path)                                   \
    ({                                                           \
        const char *last_dot = strrchr((param_path) ?: "", '.'); \
        (last_dot) ? (last_dot + 1) : (param_path);              \
    })
#define LOG_PREFIX(fmt, ...) "ox: " fmt, ##__VA_ARGS__

#define LOG_PREFIX_FUNC(fmt, ...) LOG_PREFIX("%s: " fmt, __func__, ##__VA_ARGS__)

#define LOG_PREFIX_TABLE(table_ctx, fmt, ...) \
    LOG_PREFIX("table: %s: " fmt, ((table_ctx) ? (table_ctx)->route->tr181_table : ""), ##__VA_ARGS__)

#define LOG_PREFIX_INSTANCE(table_instance, fmt, ...)                              \
    LOG_PREFIX_TABLE(                                                              \
            (table_instance) ? (table_instance)->table_ctx : NULL,                 \
            "instance: %p: %s: %s: " fmt,                                          \
            (table_instance),                                                      \
            (table_instance) ? ((table_instance)->uuid ?: "no-uuid") : "??",       \
            (table_instance) ? ((table_instance)->tr181_path ?: "no-path") : "??", \
            ##__VA_ARGS__)

#define LOG_PREFIX_PARAM(param_ctx, fmt, ...)                                           \
    LOG_PREFIX(                                                                         \
            "param: %s: " fmt,                                                          \
            ((param_ctx) && (param_ctx)->route ? (param_ctx)->route->tr181_param : ""), \
            ##__VA_ARGS__)

#define LOG_PREFIX_INSTANCE_PARAM(table_instance, param_path, fmt, ...) \
    LOG_PREFIX_INSTANCE((table_instance), "param: %s: " fmt, PARAM_NAME(param_path), ##__VA_ARGS__)

#define LOG_PREFIX_ROW(row, fmt, ...)                              \
    LOG_PREFIX(                                                    \
            "row: %s: %s: " fmt,                                   \
            ((row) && (row)->table_name ? (row)->table_name : ""), \
            ((row) && (row)->uuid ? (row)->uuid : ""),             \
            ##__VA_ARGS__)

#endif /* OX_LOG_H_INCLUDED */
