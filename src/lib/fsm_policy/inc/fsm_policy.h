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

#ifndef FSM_POLICY_H_INCLUDED
#define FSM_POLICY_H_INCLUDED

#include <ev.h>
#include <time.h>

#include "ds_tree.h"
#include "ds_list.h"
#include "ovsdb_utils.h"
#include "os_types.h"
#include "schema.h"

enum {
    FSM_ACTION_NONE = 0,
    FSM_BLOCK,
    FSM_ALLOW,
    FSM_OBSERVED,
    FSM_NO_MATCH,
    FSM_REDIRECT,
    FSM_REDIRECT_ALLOW,
    FSM_FORWARD,
    FSM_UPDATE_TAG,
    FSM_GATEKEEPER_REQ,
    FSM_FLUSH_CACHE,
    FSM_FLUSH_ALL_CACHE,
    FSM_NOANSWER,
    FSM_NUM_ACTIONS, /* always last */
};

/*
 * Report value order matters when multiple policies apply.
 * The highest will win.
 */
enum {
    FSM_REPORT_NONE = 0,
    FSM_REPORT_BLOCKED,
    FSM_REPORT_ALL,
};

enum {
    FSM_FQDN_CAT_NOP = 0,
    FSM_FQDN_CAT_FAILED,
    FSM_FQDN_CAT_PENDING,
    FSM_FQDN_CAT_SUCCESS,
};

enum {
    FSM_FQDN_OP_XM = 0, /* exact match */
    FSM_FQDN_OP_SFR,    /* start from right */
    FSM_FQDN_OP_SFL,    /* start from left */
    FSM_FQDN_OP_WILD,    /* match based on a pattern */
};

enum {
    IPv4_REDIRECT = 0,
    IPv6_REDIRECT = 1,
    FQDN_REDIRECT = 2,
    RD_SZ = 3,
};


enum {
    MAC_OP_OUT = 0,
    MAC_OP_IN,
};

enum {
    FQDN_OP_IN = 0,
    FQDN_OP_SFR_IN,
    FQDN_OP_SFL_IN,
    FQDN_OP_WILD_IN,
    FQDN_OP_OUT,
    FQDN_OP_SFR_OUT,
    FQDN_OP_SFL_OUT,
    FQDN_OP_WILD_OUT,
    FQDN_OP_TRUE,
};

enum {
    CAT_OP_OUT = 0,
    CAT_OP_IN,
    CAT_OP_TRUE,
};

enum {
    APP_OP_OUT = 0,
    APP_OP_IN,
    APP_OP_TRUE,
};

enum {
    RISK_OP_EQ = 0,
    RISK_OP_NEQ,
    RISK_OP_GT,
    RISK_OP_LT,
    RISK_OP_GTE,
    RISK_OP_LTE,
    RISK_OP_TRUE,
};

enum {
    IP_OP_OUT = 0,
    IP_OP_IN,
    IP_OP_TRUE,
};

enum {
    FSM_INLINE_REPLY = 0,
    FSM_ASYNC_REPLY,
};

enum fsm_supported_features {
    FSM_CNAME_FEATURE = (1 << 0),
    FSM_NOANSWER_FEATURE = (1 << 1),
    FSM_PROXIMITY_FEATURE = (1 << 2),
};

struct dns_device
{
    os_macaddr_t device_mac;
    ds_tree_t fqdn_pending_reqs;
    ds_tree_t dns_policy_replies_tree; /* stores the policy replies for dns request */
    ds_tree_node_t device_node;
};

enum {
    URL_BC_SVC,
    URL_GK_SVC,
};

#define URL_REPORT_MAX_ELEMS 8

struct fsm_bc_info
{
    uint8_t confidence_levels[URL_REPORT_MAX_ELEMS];
    uint8_t reputation;
};

struct fsm_wp_info
{
    uint8_t risk_level;
};

struct fsm_gk_info
{
    uint32_t confidence_level;
    uint32_t category_id;
    char *gk_policy;
};

struct fsm_url_reply
{
    int service_id;
    int lookup_status;
    bool connection_error;
    int error;
    size_t nelems;
    uint8_t categories[URL_REPORT_MAX_ELEMS];
    union
    {
        struct fsm_bc_info bc_info;
        struct fsm_wp_info wp_info;
        struct fsm_gk_info gk_info;
    } reply_info;
#define bc reply_info.bc_info
#define wb reply_info.wp_info
#define gk reply_info.gk_info
};

struct fsm_url_request
{
    os_macaddr_t dev_id;
    char url[255];
    int req_id;
    struct fsm_url_reply *reply;
};

#define MAX_RESOLVED_ADDRS 32


enum
{
    FSM_UNKNOWN_REQ_TYPE = -1,
    FSM_FQDN_REQ,
    FSM_URL_REQ,
    FSM_HOST_REQ,
    FSM_SNI_REQ,
    FSM_IPV4_REQ,
    FSM_IPV6_REQ,
    FSM_APP_REQ,
    FSM_IPV4_FLOW_REQ,
    FSM_IPV6_FLOW_REQ,
    FSM_BULK_REQ,
};


struct fsm_policy_req;
struct fsm_policy;

struct dns_response_s
{
    int num_replies;    /* number of dns replies */
    int ipv4_cnt;       /* number of IPv4 resolved address */
    int ipv6_cnt;       /* number of IPv6 resolved address */
    char *ipv4_addrs[MAX_RESOLVED_ADDRS];   /* resolved IPv4 address */
    char *ipv6_addrs[MAX_RESOLVED_ADDRS];   /* resolved IPv6 address */
};

struct fqdn_pending_req
{
    os_macaddr_t dev_id;                  /* device mac address */
    uint16_t req_id;                      /* DNS message ID  */
    int dedup;                            /* duplicate dns request counter */
    int numq;                             /* Number of questions in the request */
    struct fsm_url_request *req_info;     /* FQDN questions */
    struct fsm_session *fsm_context;      /* FSM session */
    int rd_ttl;                           /* TTL */
    uint8_t *dns_reply_pkt;               /* DNS reply packet */
    int dns_reply_pkt_len;                /* DNS reply packet length */
    struct dns_response_s dns_response;   /* DNS response */
    struct dns_device *dev_session;       /* DNS session */
    time_t timestamp;                     /* timestamp used for purging old requests */
    struct net_md_stats_accumulator *acc; /* accumulator */
    ds_tree_node_t req_node;              /* DS tree request node */
};

struct fsm_policy_reply
{
    int req_id;          /* request id, used for mapping request */
    int req_type;        /* fsm request type, same as in policy request */
    int cat_match;       /* category match */
    int reply_type;      /* is policy check processed using blocking or async*/
    int action;          /* action to take */
    char *log_action;    /* action to log mqtt report */
    int rd_ttl;          /* redirected response's ttl */
    int cache_ttl;       /* ttl value for cache */
    int flow_marker;      /* flow marker */
    int categorized;     /* categorization status */
    int log;             /* log policy */
    char *policy;        /* the last matching policy */
    int policy_idx;      /* the policy index */
    char *rule_name;     /* the last matching rule name with the policy */
    char *updatev4_tag;  /* Tag to store ipv4 dns results with if any */
    char *updatev6_tag;  /* Tag to store ipv6 dns results with if any */
    char *excluded_devices; /* Tag containing list of excludede devices */
    bool redirect;       /* Redirect dns reply */
    char redirect_cname[256]; /* Redirect cname, in case of url redirect */
    char redirects[2][256]; /* Redirect IP addresses, in case of redirect */
    int risk_level;         /* Risk level determined by the security provider */
    bool fsm_checked;       /* flag to indicate if fsm policy check is performed */
    bool from_cache;        /* indicates if the reply was read from cache */
    bool cat_unknown_to_service; /* category unknown to the provider */
    struct policy_table *policy_table; /* policy table to check */
    char *provider;         /* security provider */
    bool to_report;         /* if report needs to sent */
    void (*send_report)(struct fsm_session *, char *);
    bool (*categories_check)(struct fsm_policy_req *req,
                             struct fsm_policy *policy,
                             struct fsm_policy_reply *policy_reply);
    bool (*risk_level_check)(struct fsm_policy_req *req,
                             struct fsm_policy *policy,
                             struct fsm_policy_reply *policy_reply);
    bool (*gatekeeper_req)(struct fsm_policy_req *req,
                           struct fsm_policy_reply *policy_reply);
    void (*policy_response)(struct fsm_policy_req *policy_request,
                            struct fsm_policy_reply *policy_reply);
    void (*gatekeeper_response)(struct fsm_policy_req *policy_request,
                                struct fsm_policy_reply *policy_reply);

    ds_tree_node_t reply_node;         // DS tree reply node
};

struct fsm_policy_req
{
    os_macaddr_t *device_id;
    struct fsm_session *session;
    int req_type;
    char *url;
    struct sockaddr_storage *ip_addr;
    struct net_md_stats_accumulator *acc;
    struct fqdn_pending_req *fqdn_req;
    struct fsm_policy *policy;
    char *rule_name;
    int action;
    int policy_index;
    bool report;
    char *network_id;
    uint64_t supported_features;
};


#define FSM_MAX_POLICIES 60

/**
 * @brief representation of a policy rule.
 *
 * A Rule a sequence of checks of an object against a set of provisioned values:
 * Each check of a rule follows this pattern:
 * - Is the check enforced?
 * - Is the object included in the set of provisioned values?
 * - Is the rule targeting objects in or out of the set of provisioned values?
 * - If the object is included and the policy targets included objects,
 *   move on to the next check
 * - If the object is out of the set and the policy targets excluded objects,
 *   move on to the next check.
 * - Else the rule has failed.
 */
struct fsm_policy_rules
{
    bool mac_rule_present;
    int mac_op;
    struct str_set *macs;

    bool fqdn_rule_present;
    int fqdn_op;
    struct str_set *fqdns;

    bool cat_rule_present;
    int cat_op;
    struct int_set *categories;

    bool risk_rule_present;
    int risk_op;
    int risk_level;

    bool ip_rule_present;
    int ip_op;
    struct str_set *ipaddrs;

    bool app_rule_present;
    int app_op;
    struct str_set *apps;
};


struct fsm_policy
{
    struct policy_table *table;
    char *table_name;
    size_t idx;
    char *rule_name;
    struct fsm_policy_rules rules;
    struct str_set *redirects;
    ds_tree_t *next;
    ds_tree_t *other_config;
    int action;
    int report_type;
    bool jump_table;
    char *next_table;
    int next_table_index;
    size_t lookup_prev;
    size_t lookup_next;
    ds_tree_node_t policy_node;
};

struct fsm_url_stats {
    int64_t cloud_lookups;           /* Cloud lookup requests */
    int64_t cloud_hits;              /* Cloud lookup processed */
    int64_t cache_lookups;           /* Cache lookup requests */
    int64_t cache_hits;              /* Cache hits */
    int64_t cloud_lookup_failures;   /* service not reached */
    int64_t cache_lookup_failures;   /* Cache lookup failures */
    int64_t categorization_failures; /* Service reports a processing error */
    int64_t uncategorized;           /* Service reached, uncategorized url */
    int64_t cache_entries;           /* number of cached entries */
    int64_t cache_size;              /* size of the cache */
    int64_t min_lookup_latency;      /* minimal lookup latency */
    int64_t max_lookup_latency;      /* maximal lookup latency */
    int64_t avg_lookup_latency;      /* average lookup latency */
};

struct fsm_url_report_stats {
    uint32_t total_lookups;
    uint32_t cache_hits;
    uint32_t remote_lookups;
    uint32_t connectivity_failures;
    uint32_t service_failures;
    uint32_t uncategorized;
    uint32_t min_latency;
    uint32_t max_latency;
    uint32_t avg_latency;
    uint32_t cached_entries;
    uint32_t cache_size;
};

#define POLICY_NAME_SIZE 32
struct policy_table
{
    char name[POLICY_NAME_SIZE];
    ds_tree_t policies;
    struct fsm_policy *lookup_array[FSM_MAX_POLICIES];
    ds_tree_node_t table_node;
};

struct fsm_policy_client
{
    void *session;
    char *name;
    struct policy_table *table;
    void (*update_client)(void *, struct policy_table *);
    char *(*session_name)(struct fsm_policy_client *);
    int (*flush_cache)(void *, struct fsm_policy *);
    ds_tree_node_t client_node;
};

struct fsm_policy_session
{
    bool initialized;
    ds_tree_t policy_tables;
    ds_tree_t clients;
};

struct fsm_request_args
{
    os_macaddr_t *device_id;
    int request_type;
    struct fsm_session *session;
    struct fqdn_pending_req *fqdn_req;
    struct net_md_stats_accumulator *acc;
    int policy_action;
};

void fsm_init_manager(void);
struct fsm_policy_session * fsm_policy_get_mgr(void);
void fsm_walk_policy_macs(struct fsm_policy *p);
void fsm_policy_init(void);
const char *fsm_policy_get_action_str(int action);
bool fqdn_pre_validation(char *fqdn_string);
struct fsm_policy *fsm_policy_lookup(struct schema_FSM_Policy *policy);
struct fsm_policy *fsm_policy_get(struct schema_FSM_Policy *policy);
int fsm_cat_cmp(const void *c1, const void *c2);
void fsm_add_policy(struct schema_FSM_Policy *spolicy);
void fsm_delete_policy(struct schema_FSM_Policy *spolicy);
void fsm_update_policy(struct schema_FSM_Policy *spolicy);
void fsm_free_policy(struct fsm_policy *fpolicy);
struct policy_table *fsm_policy_find_table(char *name);
int fsm_apply_policies(struct fsm_policy_req *req,
                        struct fsm_policy_reply *policy_reply);
bool fsm_fqdncats_in_set(struct fsm_policy_req *req,
                         struct fsm_policy *p,
                         struct fsm_policy_reply *policy_reply);
bool fsm_device_in_set(struct fsm_policy_req *req, struct fsm_policy *p);
void fsm_policy_client_init(void);
void fsm_policy_register_client(struct fsm_policy_client *client);
void fsm_policy_deregister_client(struct fsm_policy_client *client);
void fsm_policy_update_clients(struct policy_table *table);
bool find_mac_in_set(os_macaddr_t *mac, struct str_set *macs_set);
void fsm_free_url_reply(struct fsm_url_reply *reply);
int fsm_policy_get_req_type(struct fsm_policy_req *req);
void fsm_walk_clients_tree(const char *caller);
void fsm_policy_flush_cache(struct fsm_policy *policy);
bool fsm_policy_wildmatch(char *pattern, char *domain);
struct fsm_policy_req *
fsm_policy_initialize_request(struct fsm_request_args *request_args);
void fsm_policy_free_request(struct fsm_policy_req *policy_request);
struct fsm_policy_reply*
fsm_policy_initialize_reply(struct fsm_session *session);
void fsm_policy_free_reply(struct fsm_policy_reply *policy_reply);
void fsm_policy_free_url(struct fqdn_pending_req* pending_req);
int gk_reply_type(struct fsm_policy_req *policy_request);
void process_gk_response_cb(struct fsm_policy_req *policy_request,
                       struct fsm_policy_reply *policy_reply);
void fsm_policy_set_supported_feature(struct fsm_policy_req *policy_request, int feature);

/**
 * @brief helper api to extract redirect ip from policy_reply redirects array.
 *
 * @param redirect redirect_prefix + redirect ip
 *
 * @return redirect ip.
 */
char *
fsm_dns_check_redirect(char *redirect, int id);

#endif /* FSM_POLICY_H_INCLUDED */
