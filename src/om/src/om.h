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

/*
 * openflow Manager - Main Include file
 */

#ifndef OM_H_INCLUDED
#define OM_H_INCLUDED

#include "os.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "log.h"
#include "ds_tree.h"
#include "ds_list.h"
#include "policy_tags.h"


/******************************************************************************
 * Template Flow Definitions
 *****************************************************************************/



/***********************************************************************
 * Declarations and definitions for Range Rule tracking
 * ********************************************************************/

typedef enum {
    NONE,
    IPV4_RANGE_SRC,
    IPV4_RANGE_DST,
    IPV6_RANGE_SRC,
    IPV6_RANGE_DST,
    PORT_RANGE_SRC,
    PORT_RANGE_DST,
} RANGE;

#define TEMPLATE_RANGE              "$<"
#define TEMPLATE_IPV4_RANGE_SRC     "nw_src=$<"
#define TEMPLATE_IPV4_RANGE_DST     "nw_dst=$<"
#define TEMPLATE_IPV6_RANGE_SRC     "ipv6_src=$<"
#define TEMPLATE_IPV6_RANGE_DST     "ipv6_dst=$<"
#define TEMPLATE_PORT_RANGE_SRC     "tp_src=$<"
#define TEMPLATE_PORT_RANGE_DST     "tp_dst=$<"

struct om_rule_node {
    struct schema_Openflow_Config   rule;
    ds_list_node_t                  lnode;          /* Single list node data */
};

extern bool         om_range_add_range_rule(struct schema_Openflow_Config *rule);
extern bool         om_range_clear_range_rules(void);
extern ds_list_t    *om_range_get_range_rules(void);
extern bool         om_range_generate_range_rules(struct schema_Openflow_Config *ofconf);


/******************************************************************************
 * Misc External Function Definitions
 *****************************************************************************/
extern bool     om_monitor_init(void);

#endif /* OM_H_INCLUDED */
