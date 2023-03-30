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

#ifndef LNX_TC_H_INCLUDED
#define LNX_TC_H_INCLUDED

#include "const.h"
#include "osn_tc.h"
#include "ds_tree.h"

typedef struct lnx_tc lnx_tc_t;

struct lnx_tc_filter
{
    bool            ingress;
    int             priority;
    char            *match;
    char            *action;
    ds_tree_node_t  lt_node;
};

struct lnx_tc
{
    char                    *lt_ifname;
    bool                    lt_tc_begin;
    bool                    lt_tc_filter_begin;
    ds_tree_t               lt_filters;
};

bool lnx_tc_init(lnx_tc_t *self, const char *ifname);
void lnx_tc_fini(lnx_tc_t *self);

bool lnx_tc_apply(lnx_tc_t *self);
bool lnx_tc_begin(lnx_tc_t *self);
bool lnx_tc_end(lnx_tc_t *self);

bool lnx_tc_filter_begin(
        lnx_tc_t *self,
        bool ingress,
        int   priority,
        const char  *match,
        const char  *action);

bool lnx_tc_filter_end(lnx_tc_t *self);

#endif /* LNX_TC_H_INCLUDED */
