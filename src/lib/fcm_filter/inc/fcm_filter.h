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

#ifndef FCM_FILTER_H_INCLUDED
#define FCM_FILTER_H_INCLUDED

#define FCM_FILTER_MAC_SIZE 18
#define FCM_FILTER_IP_SIZE 128

#include "ds_tree.h"
#include "ds_dlist.h"
#include "network_metadata.h"
#include "network_metadata_report.h"

typedef struct fcm_filter_l3_info
{
    char        src_ip[FCM_FILTER_IP_SIZE];
    char        dst_ip[FCM_FILTER_IP_SIZE];
    uint16_t    sport;
    uint16_t    dport;
    uint8_t     l4_proto;
    uint8_t     ip_type;    // l3 proto ipv4 or ipv6
} fcm_filter_l3_info_t;

typedef struct fcm_filter_l2_info
{
    char            src_mac[FCM_FILTER_MAC_SIZE];
    char            dst_mac[FCM_FILTER_MAC_SIZE];
    unsigned int    vlan_id;
    unsigned int    eth_type;
} fcm_filter_l2_info_t;

typedef struct fcm_filter_stats
{
    unsigned long pkt_cnt;
    unsigned long bytes;
} fcm_filter_stats_t;

#define FCM_MAX_FILTER_BY_NAME 64

enum fcm_appname_op
{
    FCM_APPNAME_OP_IN = 1,
    FCM_APPNAME_OP_OUT,
    FCM_APPNAME_OP_MAX
};

enum fcm_apptag_op
{
    FCM_APPTAG_OP_IN = 1,
    FCM_APPTAG_OP_OUT,
    FCM_APPTAG_OP_MAX
};

enum fcm_rule_op
{
    FCM_DEFAULT_FALSE = 0,
    FCM_DEFAULT_TRUE = 1,
    FCM_RULED_FALSE,
    FCM_RULED_TRUE,
    FCM_UNKNOWN_ACTIONS /* always last */
};

enum fcm_action
{
    FCM_EXCLUDE = 0,
    FCM_INCLUDE,
    FCM_DEFAULT_INCLUDE,
    FCM_MAX_ACTIONS,
};

enum fcm_operation
{
    FCM_OP_NONE = 0,
    FCM_OP_IN,
    FCM_OP_OUT,
    FCM_MAX_OP,
};

enum fcm_math
{
    FCM_MATH_NONE = 0,
    FCM_MATH_LT,
    FCM_MATH_LEQ,
    FCM_MATH_GT,
    FCM_MATH_GEQ,
    FCM_MATH_EQ,
    FCM_MATH_NEQ,
    FCM_MAX_MATH_OP,
};

typedef struct ip_port
{
    uint16_t port_min;
    uint16_t port_max;  /* if it set to 0 then no range */
} ip_port_t;

typedef struct _FCM_Filter_rule
{
    char *name;
    int index;

    struct str_set *smac;
    struct str_set *dmac;
    struct int_set *vlanid;
    struct str_set *src_ip;
    struct str_set *dst_ip;

    struct ip_port *src_port;
    int src_port_len;
    struct ip_port *dst_port;
    int dst_port_len;

    struct int_set *proto;

    enum fcm_operation dmac_op;
    enum fcm_operation smac_op;
    enum fcm_operation vlanid_op;
    enum fcm_operation src_ip_op;
    enum fcm_operation dst_ip_op;
    enum fcm_operation src_port_op;
    enum fcm_operation dst_port_op;
    enum fcm_operation proto_op;
    unsigned long pktcnt;
    enum fcm_math pktcnt_op;
    enum fcm_action action;
    void *other_config;
} schema_FCM_Filter_rule_t;

struct fcm_filter_app
{
    bool name_present;
    int  name_op;
    struct str_set  *names;
    bool tag_present;
    int  tag_op;
    struct str_set  *tags;
};

typedef struct fcm_filter
{
    schema_FCM_Filter_rule_t filter_rule;
    struct fcm_filter_app app;
    bool valid;
    ds_dlist_node_t dl_node;
} fcm_filter_t;

typedef struct rule_name_tree
{
    char *key;
    ds_dlist_t filter_type_list;
    ds_tree_node_t  dl_node;
} rule_name_tree_t;

struct fcm_filter_mgr
{
    int initialized;
    ds_dlist_t filter_type_list[FCM_MAX_FILTER_BY_NAME];
    ds_tree_t name_list;
    char pid[16];
    void (*ovsdb_init)(void);
};


struct fcm_filter_mgr* get_filter_mgr(void);

void fcm_filter_layer2_apply(char *filter_name,
                           struct fcm_filter_l2_info *data,
                           struct fcm_filter_stats *pkts,
                           bool *action);

void fcm_filter_7tuple_apply(char *filter_name, struct fcm_filter_l2_info *l2_info,
                             struct fcm_filter_l3_info *l3_info,
                             struct fcm_filter_stats *pkts,
                             struct flow_key *fkey,
                             bool *action);

void fcm_filter_app_apply(char *filter_name,
                          struct flow_key *fkey,
                          bool *action);
int fcm_filter_init(void);
void fcm_filter_cleanup(void);
void fcm_filter_print();
void fcm_filter_app_print(struct fcm_filter_app *app);

#endif /* FCM_FILTER_H_INCLUDED */
