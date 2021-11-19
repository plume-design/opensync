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

#define FCM_MAX_FILTER_BY_NAME 64

#include "ds_tree.h"
#include "ds_dlist.h"
#include "network_metadata.h"
#include "network_metadata_report.h"
#include "schema.h"

enum {
    FCM_APPNAME_OP_IN = 1,
    FCM_APPNAME_OP_OUT,
    FCM_APPNAME_OP_MAX
};

enum {
    FCM_APPTAG_OP_IN = 1,
    FCM_APPTAG_OP_OUT,
    FCM_APPTAG_OP_MAX
};

enum {
    FCM_DEFAULT_FALSE = 0,
    FCM_DEFAULT_TRUE = 1,
    FCM_RULED_FALSE,
    FCM_RULED_TRUE,
    FCM_UNKNOWN_ACTIONS /* always last */
};

enum {
    FCM_EXCLUDE = 0,
    FCM_INCLUDE,
    FCM_DEFAULT_INCLUDE,
    FCM_MAX_ACTIONS,
};

enum {
    FCM_OP_NONE = 0,
    FCM_OP_IN,
    FCM_OP_OUT,
    FCM_MAX_OP,
};

enum {
    FCM_MATH_NONE = 0,
    FCM_MATH_LT,
    FCM_MATH_LEQ,
    FCM_MATH_GT,
    FCM_MATH_GEQ,
    FCM_MATH_EQ,
    FCM_MATH_NEQ,
    FCM_MAX_MATH_OP,
};

struct ip_port
{
    uint16_t port_min;
    uint16_t port_max;  /* if it set to 0 then no range */
};

struct fcm_filter_rule
{
    char *name;
    int index;
    bool smac_rule_present;
    int smac_op;
    struct str_set *smac;
    bool dmac_rule_present;
    int dmac_op;
    struct str_set *dmac;
    bool vlanid_rule_present;
    int vlanid_op;
    struct int_set *vlanid;
    bool src_ip_rule_present;
    int src_ip_op;
    struct str_set *src_ip;
    bool dst_ip_rule_present;
    int dst_ip_op;
    struct str_set *dst_ip;
    bool src_port_rule_present;
    int src_port_op;
    struct ip_port *src_port;
    int src_port_len;
    bool dst_port_rule_present;
    int dst_port_op;
    struct ip_port *dst_port;
    int dst_port_len;
    bool proto_rule_present;
    int proto_op;
    struct int_set *proto;
    bool pktcnt_rule_present;
    int pktcnt_op;
    int pktcnt;
    bool appname_present;
    int  appname_op;
    struct str_set *appnames;
    bool app_tag_present;
    int  app_tag_op;
    struct str_set *app_tags;
    ds_tree_t *other_config;
    int action;
};

struct fcm_filter
{
    struct filter_table *table;
    char *filter_name;
    struct fcm_filter_rule filter_rule;
    ds_tree_node_t filter_node;
    ds_dlist_t filter_rules;
};

struct fcm_filter_mgr
{
    int initialized;
    ds_tree_t fcm_filters;
    ds_tree_t clients;
    void (*ovsdb_init)(void);
    void (*ovsdb_exit)(void);
};

#define FCM_MAX_FILTERS 60
#define FILTER_NAME_SIZE 32

struct filter_table
{
    char name[FILTER_NAME_SIZE];
    ds_tree_t filters;
    ds_dlist_t filter_rules;
    struct fcm_filter *lookup_array[FCM_MAX_FILTERS];
    ds_tree_node_t table_node;
};

struct fcm_filter_client
{
    struct fcm_session *session;
    char *name;
    struct filter_table *table;
    void (*update_client)(struct fcm_session *, struct filter_table *);
    char *(*session_name)(struct fcm_filter_client *);
    ds_tree_node_t client_node;
};

#define FCM_FILTER_IP_SIZE 128

typedef struct fcm_filter_l3_info
{
    char        src_ip[FCM_FILTER_IP_SIZE];
    char        dst_ip[FCM_FILTER_IP_SIZE];
    uint16_t    sport;
    uint16_t    dport;
    uint8_t     l4_proto;
    uint8_t     ip_type;    // l3 proto ipv4 or ipv6
    bool    src_ip_op_exists;
    bool    dst_ip_op_exists;
    bool    src_port_op_exists;
    bool    dst_port_op_exists;
    bool    proto_op_exists;
} fcm_filter_l3_info_t;

#define FCM_FILTER_MAC_SIZE 18

typedef struct fcm_filter_l2_info
{
    char            src_mac[FCM_FILTER_MAC_SIZE];
    char            dst_mac[FCM_FILTER_MAC_SIZE];
    unsigned int    vlan_id;
    unsigned int    eth_type;
    bool        smac_op_exists;
    bool        dmac_op_exists;
    bool        vlanid_op_exists;
} fcm_filter_l2_info_t;

typedef struct fcm_filter_stats
{
    int pkt_cnt;
    unsigned long bytes;
    bool pktcnt_op_exists;
} fcm_filter_stats_t;

struct fcm_filter_req
{
   fcm_filter_l2_info_t *l2_info;
   fcm_filter_l3_info_t *l3_info;
   struct fcm_filter_stats *pkts;
   struct filter_table *table;
   struct flow_key *fkey;
   bool action;
};

void fcm_apply_filter(struct fcm_session *session, struct fcm_filter_req *req);

void fcm_filter_layer2_apply(char *filter_name,
                             struct fcm_filter_l2_info *data,
                             struct fcm_filter_stats *pkts,
                             bool *action);

struct fcm_filter_mgr* get_filter_mgr(void);
int fcm_filter_init(void);
struct fcm_filter *fcm_filter_lookup(struct schema_FCM_Filter *filter);
void fcm_filter_cleanup(void);
void fcm_free_filter(struct fcm_filter *ffilter);
int free_schema_struct(struct fcm_filter_rule *rule);
int free_filter_app(struct fcm_filter_rule *app);
void fcm_filter_client_init(void);
void fcm_filter_register_client(struct fcm_filter_client *client);
void fcm_filter_deregister_client(struct fcm_filter_client *client);
void fcm_filter_update_clients(struct filter_table *table);

#endif /* FCM_FILTER_H_INCLUDED */
