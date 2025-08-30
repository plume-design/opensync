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

#ifndef GK_CACHE_H_INCLUDED
#define GK_CACHE_H_INCLUDED

#include <stdint.h>
#include <time.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "os_types.h"
#include "fsm.h"
#include "fsm_policy.h"
#include "ds_tree.h"
#include "ds_dlist.h"
#include "util.h"
#include "os.h"
#include "network_metadata_report.h"
#include "gatekeeper.pb-c.h"
#include "gatekeeper_msg.h"


/* persistent store name */
#define GATEKEEPER_CACHE_STORE_NAME "gatekeeper_cache_store"

/* cache data key */
#define GATEKEEPER_CACHE_DATA_KEY "cache_data"

/* enum with supported request types */
enum gk_cache_request_type
{
    /* Order matters. See related enum in fsm_policy.h */
    GK_CACHE_UNKNOWN_REQ_TYPES = FSM_UNKNOWN_REQ_TYPE,
    GK_CACHE_REQ_TYPE_FQDN     = FSM_FQDN_REQ,
    GK_CACHE_REQ_TYPE_URL      = FSM_URL_REQ,
    GK_CACHE_REQ_TYPE_HOST     = FSM_HOST_REQ,
    GK_CACHE_REQ_TYPE_SNI      = FSM_SNI_REQ,
    GK_CACHE_REQ_TYPE_IPV4     = FSM_IPV4_REQ,
    GK_CACHE_REQ_TYPE_IPV6     = FSM_IPV6_REQ,
    GK_CACHE_REQ_TYPE_APP      = FSM_APP_REQ,
    GK_CACHE_REQ_TYPE_INBOUND,
    GK_CACHE_REQ_TYPE_OUTBOUND,
    GK_CACHE_INTERNAL_TYPE_HOSTNAME,  /* This is an internal type */
    GK_CACHE_MAX_REQ_TYPES,
};

typedef enum gk_cache_request_type gk_cache_request_type_t;

#define CHECK_GK_CACHE_REQ_TYPE(t) \
        ((t >= GK_CACHE_REQ_TYPE_FQDN) && (t < GK_CACHE_MAX_REQ_TYPES))

struct counter_s
{
    uint64_t total;
    uint64_t previous;
};

/* supported attribute types */
struct attr_generic_s
{
    char             *name;
    struct counter_s  hit_count;  /* number of times lookup is performed */
};

struct attr_ip_addr_s
{
    struct sockaddr_storage ip_addr;
    struct counter_s        hit_count;  /* number of times lookup is performed */
    int                     action_by_name;
};

struct attr_hostname_s
{
    char             *name;
    struct counter_s  count_fqdn;
    struct counter_s  count_host;
    struct counter_s  count_sni;
    int added_by;
};

union attribute_type
{
    struct attr_hostname_s *host_name;
    struct attr_generic_s  *url;
    struct attr_generic_s  *app_name;
    struct attr_ip_addr_s  *ipv4;
    struct attr_ip_addr_s  *ipv6;
};

/* enum for flow direction */
enum gkc_flow_direction
{
    GKC_FLOW_DIRECTION_UNSPECIFIED     = NET_MD_ACC_UNSET_DIR,
    GKC_FLOW_DIRECTION_OUTBOUND        = NET_MD_ACC_OUTBOUND_DIR,
    GKC_FLOW_DIRECTION_INBOUND         = NET_MD_ACC_INBOUND_DIR,
    GKC_FLOW_DIRECTION_LAN2LAN         = NET_MD_ACC_LAN2LAN_DIR,
    GKC_FLOW_DIRECTION_INTERNAL_IGNORE,
};

/**
 * @brief structure to store parameters
 * required for checking and deleting
 * entries with expired TTL values.
 */
struct gkc_del_info_s
{
    enum gk_cache_request_type attr_type;
    struct per_device_cache *pdevice;
    uint64_t attr_del_count;
    uint64_t flow_del_count;
    ds_tree_t *tree;
};

/**
 * @brief structure to store fqdn
 * redirect entries
 */
struct fqdn_redirect_s
{
    char *redirect_cname;
    char redirect_ips[2][256];
    int redirect_ttl;
    bool redirect;
};

/**
 * @brief struct for storing attribute
 *
 */
struct attr_cache
{
    union attribute_type    attr;             /* attribute type */
    int                     cache_ttl;        /* TLL value for this entry */
    time_t                  cache_ts;         /* refresh timestamp when cache is hit */
    time_t                  original_ts;      /* time when the entry was added */
    int                     action;           /* action specified : Allow or block */
    char                   *gk_policy;        /* gatekeeper rule string */
    uint32_t                category_id;      /* category plume id */
    uint32_t                confidence_level; /* risk/confidence level */
    int                     categorized;      /* categorized */
    struct fqdn_redirect_s *fqdn_redirect;
    uint8_t                 direction;        /* inbound or outbound */
    uint64_t                key;              /* used to differentiate entries */
    bool                    is_private_ip;
    bool                    redirect_flag;
    ds_tree_node_t          attr_tnode;
    uint32_t                flow_marker;
    char                    *network_id;
    ds_dlist_node_t         lru_node;
    ds_tree_t               *attr_tree;
    gk_cache_request_type_t type; /* request type */
};

/**
 * @brief struct for storing 5-tuple IP flows
 *
 */
struct ip_flow_cache
{
    uint8_t *src_ip_addr;       /* src ip in Network byte order */
    uint8_t *dst_ip_addr;       /* dst ip in Network byte order */
    uint16_t src_port;          /* source port value */
    uint16_t dst_port;          /* destination port value */
    uint8_t ip_version;         /* ipv4 (4), ipv6 (6) */
    uint8_t protocol;           /* protocol value */
    uint8_t direction;          /* direction of flow: inbound or outbound */
    int action;                 /* action specified: Allow or Block */
    int cache_ttl;              /* TLL value for this flow entry */
    char *gk_policy;            /* gatekeeper rule string */
    uint32_t category_id;       /* category plume id */
    uint32_t confidence_level;  /* risk/confidence level */
    time_t cache_ts;            /* refresh timestamp when cache is hit */
    time_t original_ts;         /* time when the entry was added */
    struct counter_s hit_count; /* number of times lookup is performed */
    bool is_private_ip;
    char *network_id;
    ds_tree_node_t ipflow_tnode;
};


/**
 * @brief device instance storing attributes and flows
 *
 * It embeds:
 * - a hostname attribute tree (embedding http_host, SNI and FQDN)
 * - a url attribute tree
 * - a IPv4 attribute tree
 * - a IPv6 attribute tree
 * - a application attribute tree
 * - a inbound flow tree
 * - a outbound flow tree
 */
struct per_device_cache
{
    os_macaddr_t *device_mac;   /* key: device mac address */
    uint64_t counter;           /* counter to keep track of number of cache
                                   entries */
    uint64_t req_counter[GK_CACHE_MAX_REQ_TYPES]; /* request counter array for
                                                     each request types */
    uint64_t allowed[GK_CACHE_MAX_REQ_TYPES]; /* number of allowed action for
                                                 this device */
    uint64_t blocked[GK_CACHE_MAX_REQ_TYPES]; /* number of blocked action for
                                                 this device */

    ds_tree_t hostname_tree;   /* attr_cache */
    ds_tree_t url_tree;        /* attr_cache */
    ds_tree_t ipv4_tree;       /* attr_cache */
    ds_tree_t ipv6_tree;       /* attr_cache */
    ds_tree_t app_tree;        /* attr_cache */
    ds_tree_t outbound_tree;   /* ip_flow_cache */
    ds_tree_t inbound_tree;    /* ip_flow_cache */
    ds_tree_node_t perdevice_tnode;
    struct attr_cache *category_cache;
};

/**
 * @brief tree structure for storing devices with its
 *        attributes and flows
 */
struct gk_cache_mgr
{
    bool initialized;
    uint64_t total_entry_count;
    ds_tree_t per_device_tree; /* per_device_cache */
    ds_dlist_t lru_list;       /* LRU of attr_cache */
    size_t lru_size;
    struct attr_cache *attr_cache_array;
    struct attr_cache *lru_free;
    size_t lru_available;
    size_t lru_recycled;
    ds_tree_t location_wide_cache;
    int category_ids[96];
    int location_categories;
};

/**
 * @brief attribute interface structure that should be used by the caller,
 *        for adding, deleting or looking up attribute entry
 *
 * When creating an entry, device_mac, type, name, action and ttl
 * value should be provided.
 * When lookup is performed, action and hit counter will be updated
 */
struct gk_attr_cache_interface
{
    os_macaddr_t *device_mac;         /* device mac address */
    gk_cache_request_type_t attribute_type; /* request type */
    char *attr_name;                  /* attribute name */
    struct sockaddr_storage *ip_addr; /* attribute ip address if used */
    uint64_t cache_ttl;               /* TTL value that should be set */
    uint8_t action;                   /* action req when adding will be set when
                                         lookup is performed */
    uint8_t direction;                /* direction for the request */
    char *gk_policy;                  /* gatekeeper rule string */
    uint32_t category_id;             /* category plume id */
    uint32_t confidence_level;        /* risk/confidence level */
    uint64_t hit_counter;             /* hit count will be set when lookup
                                         is performed */
    uint64_t cache_key;               /* key for a request is computed only once */
    struct fqdn_redirect_s *fqdn_redirect;
    int categorized;
    bool is_private_ip;
    bool redirect_flag;
    int  action_by_name;
    uint32_t flow_marker;             /* conntrack mark for the flow */
    char *network_id;
};

/**
 * @brief 5-tuple flow interface structure that should be used by the caller,
 *        for adding, deleting or looking up attribute entry
 *
 * When creating an entry 5-tuple value, action and ttl
 * value should be provided.
 * When lookup is performed, action and hit counter will be updated
 */
struct gkc_ip_flow_interface
{
    os_macaddr_t *device_mac;  /* device mac address */
    uint8_t *src_ip_addr;      /* src ip in Network byte order */
    uint8_t *dst_ip_addr;      /* dst ip in Network byte order */
    uint16_t src_port;         /* source port value */
    uint16_t dst_port;         /* dst port value */
    uint8_t ip_version;        /* ipv4 (4), ipv6 (6) */
    uint8_t protocol;          /* protocol value  */
    uint8_t direction;         /* used to check inbound/outbound cache */
    uint8_t action;            /* action req when adding will be set when lookup is
                                  performed */
    char *gk_policy;           /* gatekeeper rule string */
    uint32_t category_id;      /* category plume id */
    uint32_t confidence_level; /* risk/confidence level */
    uint64_t hit_counter;      /* will be updated when lookup is performed */
    uint64_t cache_ttl;        /* TTL value that should be set */
    bool is_private_ip;
    bool redirect_flag;
    char *network_id;
};

struct gk_cache_mgr *
gk_cache_get_mgr(void);

/**
 * @brief initialize gk_cache handle manager.
 */
void
gk_cache_init_mgr(struct gk_cache_mgr *mgr);

/**
 * @brief initialize gk_cache.
 *
 * receive: none
 *
 * @return true for success and false for failure.
 */
bool
gk_cache_init(size_t lru_size);

/**
 * @brief setter for the number of entries in the cache
 *
 * @param n total number of records in the cache.
 */
void
gk_cache_set_size(size_t n);

/**
 * @brief getter for the number of entries in the cache
 *
 * @return max number of records allowed in the cache.
 */
size_t
gk_cache_get_size(void);

/**
 * @brief cleanup allocated memory used by cache structure.
 *
 * receive none
 *
 * @return void.
 */
void
gkc_cleanup_mgr(void);

/**
 * @brief cleanup allocated memory.
 *
 * receive none
 *
 * @return void.
 */
void
gk_cache_cleanup(void);

/**
 * @brief check ttl value for the given attribute
 *        or flow tree
 *
 * @params: attribute or flow tree
 *
 * @return void.
 */
void
gk_cache_check_ttl_device_tree(ds_tree_t *tree);

/**
 * @brief get the count of devices having allowed action
 *
 * @params: device_mac mac address of the device
 * @params: attr_type attribute type
 * @return counter value of the device.
 */
uint64_t
gkc_get_allowed_counter(os_macaddr_t *device_mac,
                        enum gk_cache_request_type attr_type);

/**
 * @brief check ttl value for the given attribute
 *        or flow tree
 *
 * @params: attribute or flow tree
 *
 * @return void.
 */
uint64_t
gkc_get_blocked_counter(os_macaddr_t *device_mac,
                        enum gk_cache_request_type attr_type);

/**
 * @brief print cache'd entries for ALL REQ types.
 *
 */
void
gkc_print_cache_entries(void);

/**
 * @brief print cache'd entries for a specified type (ALL types
 *        printed when using GK_CACHE_MAX_REQ_TYPES)
 *
 */
void
gkc_print_cache_parts(enum gk_cache_request_type cache_type);

/**
 * @brief get the count of the devices stored in cache
 *
 * @return returns the device count value
 */
int
gk_get_device_count(void);

/******************************************************************************
 *  attribute type operations
 *******************************************************************************/

/**
 * @brief Lookup the given attribute in cache.
 *
 * @params: req: interface structure specifying the attribute request
 *
 * @return true for success and false for failure.
 */
bool
gkc_lookup_attribute_entry(struct gk_attr_cache_interface *req, bool update_count);

/**
 * @brief add the given attribute to cache.
 *
 * @params: req: interface structure specifying the attribute request
 *
 * @return true for success and false for failure.
 */
bool
gkc_add_attribute_entry(struct gk_attr_cache_interface *entry);


/**
 * @brief update or add the given attribute to the cache.
 *
 * @params: req: interface structure specifying the attribute request
 *
 * @return true for success and false for failure.
 */
bool
gkc_upsert_attribute_entry(struct gk_attr_cache_interface *entry);

/**
 * @brief delete the given attribute from cache
 *
 * @params req: interface structure specifying the attribute request
 * @return true for success and false for failure.
 */
bool
gkc_del_attribute(struct gk_attr_cache_interface *req);

/**
 * @brief delete the given attribute from device tree
 *
 * @params pdevice: tree having the device entries along with
 *         attributes and flows associated with it
 * @return true for success and false for failure.
 */
bool
gkc_del_attr_from_dev(struct per_device_cache *pdevice,
                      struct gk_attr_cache_interface *req);

/**
 * @brief remove old cache entres.
 *
 * @param ttl the cache entry time to live
 */
void
gkc_ttl_cleanup(void);

/******************************************************************************
 * IP flow related operations
 *******************************************************************************/
/**
 * @brief add the given IP tuple flow to cache.
 *
 * @params: req: interface structure specifying the flow request
 *
 * @return true for success and false for failure.
 */
bool
gkc_add_flow_entry(struct gkc_ip_flow_interface *entry);

/**
 * @brief lookup the given IP tuple flow to cache.
 *
 * @params: req: interface structure specifying the flow request
 *
 * @return true for success and false for failure.
 */
bool
gkc_lookup_flow(struct gkc_ip_flow_interface *req, bool update_count);

/**
 * @brief delete the given IP tuple flow to cache.
 *
 * @params: req: interface structure specifying the flow request
 *
 * @return true for success and false for failure.
 */
bool
gkc_del_flow(struct gkc_ip_flow_interface *req);

/**
 * @brief add flow entry for the given device tree
 *
 * @params: pdevice: tree for this device
 * @params: entry: entry to be added
 *
 * @return true for success and false for failure.
 */
bool
gkc_add_flow_tree(struct per_device_cache *pdevice,
                  struct gkc_ip_flow_interface *entry);

/**
 * @brief del flow entry for the given device tree
 *
 * @params: pdevice: tree for this device
 * @params: req: request interface provided
 *
 * @return true for success and false for failure.
 */
bool
gkc_del_flow_from_dev(struct per_device_cache *pdevice,
                      struct gkc_ip_flow_interface *req);

/**
 * @brief lookup flows for the given device tree
 *
 * @params: pdevice: tree for this device
 * @params: req: request interface provided
 *
 * @return true for success and false for failure.
 */
bool
gkc_lookup_flows_for_device(struct per_device_cache *pdevice,
                            struct gkc_ip_flow_interface *req,
                            bool update_count);

/**
 * @brief comparator function for 5-tuple
 *
 * @params: _a: first 5-tuple structure to compare
 * @params: -b: second 5-tuple structure to compare
 *
 * @return 0 if equal
 */
int
gkc_flow_entry_cmp(const void *_a, const void *_b);

/**
 * @brief validate the request input.
 *
 * @params: req: interface structure specifying the attribute request
 * @return true for success and false for failure.
 */
bool
gkc_is_flow_valid(struct gkc_ip_flow_interface *req);

/**
 * @brief delete the given flow from the flow
 *        tree if TTL is expired
 *
 * @params: gk_del_info cache delete info structure
 */
void
gkc_cleanup_ttl_flow_tree(struct gkc_del_info_s *gk_del_info);

/**
 * @brief Get the number of entries cached by gatekeeper
 *
 * @return the total number of cached entries in GK_cache
 */
unsigned long
gk_get_cache_count(void);

/**
 * @brief returns the number of recycled cache entries
 *
 * @return number of recycled cache entries
 */
size_t
gk_cache_get_recycled_count(void);

/**
 * @brief reset the number of recycled cache entries
 *
 */
void
gk_cache_reset_recycled_count(void);

/**
 * @brief Deletes any/all entries in the cache
 */
void
clear_gatekeeper_cache(void);

/**
 * @brief create a new flow entry initializing with 5-tuple values
 *        from input.
 *        (Exposed for testing)
 *
 * @params: req: interface structure specifying the attribute request.
 *
 * @return ip_flow_cache pointer on success or NULL on failure.
 */
struct ip_flow_cache *
gkc_new_flow_entry(struct gkc_ip_flow_interface *req);

/**
 * @brief free memory used by the flow entry.
 *        (Exposed for testing)
 *
 * @params: flow_tree: pointer to flow tree from which flow is to be
 *          freed.
 */
void
gkc_free_flow_members(struct ip_flow_cache *flow_entry);

/**
 * @brief create a new attribute entry fo the given attribute type.
 *        (Exposed for testing)
 *
 * @params: entry: specifying the attribute type
 *
 * @return return pointer to created attribute struct
 *         NULL on failure
 */
struct attr_cache*
gkc_new_attr_entry(ds_tree_t *cache, struct gk_attr_cache_interface *entry);

/**
 * @brief frees memory used by attribute when it is deleted.
 *        (Exposed for testing)
 *
 * @params: tree pointer to attribute tree
 * @params: req_type: request type
 */
void
gkc_free_attr_entry(struct attr_cache *attr_entry,
                    enum gk_cache_request_type attr_type);

/**
 * @brief Fetch a reference to the attribute entry in its cache
 *
 * @return a pointer to the entry in the cache.
 *         DO NOT FREE as the entry remains in use in the ds_tree.
 */
struct attr_cache*
gkc_fetch_attribute_entry(struct gk_attr_cache_interface *req);

/**
 * @brief Flush an entire cache for a specific device
 *
 * @param pd_cache the per device cache to be flushed
 * @return number of total cache entries that were deleted for
 * the device (attr or flow)
 */
int
gk_clean_per_device_entry(struct per_device_cache *pd_cache);

/**
 * @brief flush cache entries matching the policy rules.
 *
 * @param rules the policy rules of cache entries to be flushed
 * @return the number of rules effectively flushed or -1 in case
 *         of any error.
 */
int
gkc_flush_rules(struct fsm_policy_rules *rules);

/**
 * @brief flush the entire cache.
 *
 * @param rules the policy rules of cache entries to be flushed
 *              (only rule potentially present is a mac rule)
 * @return the number of rules effectively flushed or -1 in case
 *         of any error.
 */
int
gkc_flush_all(struct fsm_policy_rules *rules);

/**
 * @brief callback hook so FSM can trigger cache flushes based on policy
 *
 * @param context
 * @param policy
 * @return number of cache entries flushed or -1 on error
 */
int
gkc_flush_client(void *context, struct fsm_policy *policy);

/**
 * @brief Compute a unique key for the request passed.
 *
 * @param req
 * @return unique key matching the req (or 0 in case of error)
 *
 * @remark there is a 1 in 2^64 chance of a collision for the error value.
 * @remark This is exposed for UT's benefit.
 */
uint64_t
get_attr_key(struct gk_attr_cache_interface *req);

void
gkc_remove_entry(ds_tree_t *tree, struct attr_cache *entry);

void
dump_attr_entry(struct attr_cache *attr_entry, enum gk_cache_request_type req_type);
/*
 * gk_store_cache_in_persistence - Store the current cache in persistent storage
 *
 * This function serializes the current cache entries and stores them in
 * persistent storage for later retrieval.
 */
void gk_store_cache_in_persistence();

/**
    * @brief Restore cache from persistent storage
    *
    * This function attempts to restore the cache from the persistent storage.
    * It reads the serialized cache data and populates the cache entries.
*/
void gk_restore_cache_from_persistance();

/**
 * @brief Serialize the current cache entries into a buffer
 *
 * @return a pointer to the serialized buffer or NULL on failure
 */
void gk_store_serialized_cache(ds_tree_t *tree);

bool gk_populate_bulk_reply_arrays(ds_tree_t *device_tree, Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply);

void gk_free_cache_interface_entry(struct gk_attr_cache_interface *entry);


/**
 * @brief Clean up allocated resources in bulk reply on error
 *
 * @param bulk_reply pointer to the bulk reply to clean up
 */
void gk_cleanup_bulk_reply(Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply);

/**
    * @brief Restore cache from a packed buffer
    *
    * This function restores the cache from a packed buffer containing serialized cache data.
    * It populates the gk_reply structure and adds the entries to the cache.
    *
    * @param pb Pointer to the packed buffer containing serialized cache data.
    * @return true if successful, false otherwise.
*/
bool gk_restore_cache_from_buffer(struct gk_packed_buffer *pb);

void gk_cache_sync_location_entries(void);

bool gk_populate_cache_entry(struct gk_device2app_repl *dev_repl, struct gk_attr_cache_interface *entry);

#endif /* GK_CACHE_H_INCLUDED */
