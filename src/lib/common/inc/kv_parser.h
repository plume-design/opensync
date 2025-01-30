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

#ifndef KV_PARSER_H_INCLUDED
#define KV_PARSER_H_INCLUDED

#include <stdbool.h>

#include "ds_tree.h"

typedef struct kv_parser kv_parser_t;
typedef struct kv_parser_str_entry kv_parser_str_entry_t;
typedef void kv_parser_walk_fn_t(void *priv, const char *key, const char *value);

kv_parser_t *kv_parser_new(void);
void kv_parser_del(kv_parser_t *self);
void kv_parser_set_dir(kv_parser_t *self, const char *dir);
bool kv_parser_is_populated(kv_parser_t *self);
void kv_parser_populate(kv_parser_t *self);
void kv_parser_walk(kv_parser_t *self, kv_parser_walk_fn_t fn, void *priv);
void kv_parser_flush(kv_parser_t *self);
const char *kv_parser_get_value(kv_parser_t *self, const char *key);

#endif /* KV_PARSER_H_INCLUDED */
