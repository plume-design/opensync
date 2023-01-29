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

#ifndef QOSM_IC_TEMPATE_INCLUDED
#define QOSM_IC_TEMPATE_INCLUDED

#include "qosm.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "policy_tags.h"
#include "log.h"
#include "ds_tree.h"
#include "ds_list.h"
#include "osn_tc.h"

typedef struct
{
    char    *token;
    char    *match;
    char    *action;
    struct qosm_ip_iface *parent;
    bool    ingress;
    int     priority;
    ds_tree_t tags; // Tree of om_tag_list_entry_t
    ds_tree_node_t dst_node;
} qosm_ic_tmpl_filter_t;

bool
qosm_ic_is_template_rule(struct schema_Interface_Classifier *config);

void
qosm_ic_template_init(void);

bool
qosm_ic_template_filter_update(osn_tc_t *ipi_tc, om_action_t type, qosm_ic_tmpl_filter_t *tflow);

qosm_ic_tmpl_filter_t *
qosm_ic_filter_find_by_token(char *token);

void
qosm_ic_template_set_parent(struct qosm_ip_iface *ipi, char *token);

bool qosm_ic_template_tag_update(om_tag_t *tag,
                                ds_tree_t *removed,
                                ds_tree_t *added,
                                ds_tree_t *updated);

bool
qosm_ic_template_add_from_schema(struct schema_Interface_Classifier *config);

bool
qosm_ic_template_del_from_schema(char *token);


#endif // QOSM_IC_TEMPATE_INCLUDED

