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

#ifndef QOSM_FILTER_INTERNAL_H_INCLUDED
#define QOSM_FILTER_INTERNAL_H_INCLUDED

#include "ovsdb_update.h"
#include "ds_tree.h"
#include "ovsdb_table.h"
#include "qosm_ic_template.h"
#include "reflink.h"
#include "const.h"
#include "osn_tc.h"
#include "evx.h"

struct qosm_filter
{
    ds_tree_t qosm_ip_iface_tree;        /* tree for storing IP_Interface */
    ds_tree_t qosm_intf_classifier_tree; /* tree for storing IP_Interface */
    ds_tree_t qosm_ic_template_tree;     /* tree for storing the template tags */
};

struct intf_classifier_entry
{
    struct qosm_intf_classifier *ic;
    bool ingress;
    ds_tree_node_t ic_node;
};

struct qosm_ip_iface
{
    char ipi_ifname[C_IFNAME_LEN];      /* Interface name */
    ovs_uuid_t ipi_uuid;                /* UUID of this object */
    ds_tree_t ipi_intf_classifier_tree; /* tree to store ingress/egrees classifiers */
    reflink_t ipi_classifier_reflink;   /* Reflink to classifier changes */
    osn_tc_t *ipi_tc;                   /* OSN QoS configuration object */
    ds_tree_node_t ipi_tnode;           /* Tree node */
};

struct qosm_intf_classifier
{
    ovs_uuid_t ic_uuid;           /* UUID of this object */
    reflink_t ic_reflink;         /* Reflink of this object */
    struct qosm_ip_iface *parent; /* backpointer reference to nmb_ip_iface struct */
    char *ic_token;               /* token name */
    char *ic_match;               /* match to be applied to TC */
    char *ic_action;              /* action parameter required for TC command */
    int ic_priority;              /* priority of the TC filter rule */
    ds_tree_node_t ic_tnode;      /* Tree node */
};

/* Get the singleton qosm_filter object. */
struct qosm_filter *qosm_filter_get(void);

#endif /* QOSM_FILTER_INTERNAL_H_INCLUDED */
