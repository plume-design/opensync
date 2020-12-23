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

#ifndef CAPTIVE_PORTAL_H_INCLUDED
#define CAPTIVE_PORTAL_H_INCLUDED
#include <stdint.h>
#include <stdbool.h>
#include <ev.h>

#include "ds_tree.h"

typedef enum proto_type {
	PT_NONE = 0,
	PT_HTTP,
	PT_HTTPS
} proto_type;

struct url_s {
    proto_type              proto;
    char                    *domain_name;
    char                    *port;
};

struct cportal {
    bool                    enabled;
    int                     proxy_method;
    char                    *name;
    char                    *pkt_mark;
    char                    *rt_tbl_id;
    char                    *uam_url;
    struct url_s            *url;
    ds_tree_t               *other_config;
    ds_tree_t               *additional_headers;

    ds_tree_node_t           cp_tnode;
};



enum cportal_proxy_method
{
    FORWARD = 0,
    REVERSE
};

bool cportal_proxy_init(void);
bool cportal_proxy_set(struct cportal *self);
bool cportal_proxy_start(struct cportal *self);
bool cportal_proxy_stop(struct cportal *self);
int cportal_ovsdb_init(void);
#endif /* CAPTIVE_PORTAL_H_INCLUDED */
