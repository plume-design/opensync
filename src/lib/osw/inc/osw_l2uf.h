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

#ifndef OSW_L2UF_H_INCLUDED
#define OSW_L2UF_H_INCLUDED

#include <stdbool.h>
#include <osw_types.h>

struct osw_l2uf;
struct osw_l2uf_if;

typedef void osw_l2uf_seen_fn_t(struct osw_l2uf_if *i,
                                const struct osw_hwaddr *sa_addr);

struct osw_l2uf_if *
osw_l2uf_if_alloc(struct osw_l2uf *m,
                  const char *if_name);

void
osw_l2uf_if_free(struct osw_l2uf_if *i);

void
osw_l2uf_if_set_data(struct osw_l2uf_if *i,
                     void *data);

void *
osw_l2uf_if_get_data(struct osw_l2uf_if *i);

void
osw_l2uf_if_set_seen_fn(struct osw_l2uf_if *i,
                        osw_l2uf_seen_fn_t *fn);

#endif
