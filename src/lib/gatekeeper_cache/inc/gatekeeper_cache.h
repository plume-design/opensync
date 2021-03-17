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
#include "ds_tree.h"
#include "os.h"
#include "os_types.h"
#include "fsm_policy.h"

#define GK_MAX_CACHE_ENTRIES 100000
#define GK_DEFAULT_TTL 300

/* enum with supported request types */
enum gk_cache_request_type
{
    GK_CACHE_REQ_TYPE_FQDN = 0,
    GK_CACHE_REQ_TYPE_URL,
    GK_CACHE_REQ_TYPE_HOST,
    GK_CACHE_REQ_TYPE_SNI,
    GK_CACHE_REQ_TYPE_IPV4,
    GK_CACHE_REQ_TYPE_IPV6,
    GK_CACHE_REQ_TYPE_APP,
    GK_CACHE_REQ_TYPE_INBOUND,
    GK_CACHE_REQ_TYPE_OUTBOUDND,
    GK_CACHE_MAX_REQ_TYPES
};

/* supported attribute types */
union attribute_type
{
    char *url;
    char *fqdn;
    char *host;
    char *app_name;
    char *sni;
    char *ipv4;
    char *ipv6;
};

/* enum for flow direction */
enum gkc_flow_direction
{
    GKC_FLOW_DIRECTION_UNSPECIFIED = 0,
    GKC_FLOW_DIRECTION_OUTBOUND,
    GKC_FLOW_DIRECTION_INBOUND,
    GKC_FLOW_DIRECTION_LAN2LAN,
};

/**
 * @brief struct for storing attribute
 *
 */
struct attr_cache
{
    union attribute_type attr; /* attribute type */
    int cache_ttl;             /* TLL value for this entry */
    time_t cache_ts;           /* time when the entry was added */
    int action;                /* action specified : Allow or block */
    char *gk_policy;              /* gatekeeper rule string */
    uint32_t category_id;      /* category plume id */
    uint32_t confidence_level; /* risk/confidence level */
    uint64_t hit_count;        /* number of times lookup is performed */
    int categorized;           /* categorized */
    ds_tree_node_t attr_tnode;
};

/**
 * @brief struct for storing 5-tuple IP flows
 *
 */
struct ip_flow_cache
{
    uint8_t *src_ip_addr; /* src ip in Network byte order */
    uint8_t *dst_ip_addr; /* dst ip in Network byte order */
    uint8_t ip_version;   /* ipv4 (4), ipv6 (6) */
    uint16_t src_port;    /* source port value */
    uint16_t dst_port;    /* destination port value */
    uint8_t protocol;     /* protocol value */
    uint8_t direction;    /* direction of this flow: inbound or outbound */
    int action;           /* action specified: Allow or Block */
    int cache_ttl;        /* TLL value for this flow entry */
    char *gk_policy;              /* gatekeeper rule string */
    uint32_t category_id;      /* category plume id */
    uint32_t confidence_level; /* risk/confidence level */
    time_t cache_ts;      /* time when the entry was added */
    uint64_t hit_count;   /* number of times lookup is performed */
    ds_tree_node_t ipflow_tnode;
};

/**
 * @brief device instance storing attributes and flows
 *
 * It embeds:
 * - a fqdn attribute tree
 * - a url attribute tree
 * - a sni attribute tree
 * - a inbound flow tree
 * - a outbound flow tree
 */
struct per_device_cache
{
    os_macaddr_t *device_mac;   /* key: device mac address */
    uint64_t counter;           /* counter to keep track of number of cache entries */
    uint64_t req_counter[GK_CACHE_MAX_REQ_TYPES]; /* request counter array for
                                                     each request types */
    uint64_t allowed[GK_CACHE_MAX_REQ_TYPES]; /* number of allowed action for
                                                 this device */
    uint64_t blocked[GK_CACHE_MAX_REQ_TYPES]; /* number of blocked action for
                                                 this device */
    ds_tree_t fqdn_tree;     /* tree to hold entries of attr_cache */
    ds_tree_t url_tree;      /* attr_cache */
    ds_tree_t host_tree;      /* attr_cache */
    ds_tree_t sni_tree;      /* attr_cache */
    ds_tree_t ipv4_tree;      /* attr_cache */
    ds_tree_t ipv6_tree;      /* attr_cache */
    ds_tree_t app_tree;      /* attr_cache */
    ds_tree_t outbound_tree; /* ip_flow_cache */
    ds_tree_t inbound_tree;  /* ip_flow_cache */
    ds_tree_node_t perdevice_tnode;
};

/**
 * @brief tree structure for storing devices with its
 *        attributes and flows
 */
struct gk_cache_mgr
{
    bool initialized;
    uint64_t count;
    ds_tree_t per_device_tree; /* per_device_cache */
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
    os_macaddr_t *device_mac;                  /* device mac address */
    int attribute_type;                        /* request type */
    char *attr_name;                           /* attribute name */
    uint64_t cache_ttl;   /* TTL value that should be set */
    uint8_t action;       /* action req when adding will be set when lookup is
                             performed */
    char *gk_policy;              /* gatekeeper rule string */
    uint32_t category_id;      /* category plume id */
    uint32_t confidence_level; /* risk/confidence level */
    uint64_t hit_counter; /* hit count will be set when lookup is performed */
    int categorized;
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
    os_macaddr_t *device_mac; /* device mac address */
    uint8_t *src_ip_addr;     /* src ip in Network byte order */
    uint8_t *dst_ip_addr;     /* dst ip in Network byte order */
    uint8_t ip_version;       /* ipv4 (4), ipv6 (6) */
    uint16_t src_port;        /* source port value */
    uint16_t dst_port;        /* dst port value */
    uint8_t protocol;         /* protocol value  */
    uint8_t direction;        /* used to check inbound/outbound cache */
    uint64_t cache_ttl;       /* TTL value that should be set */
    uint8_t action;           /* action req when adding will be set when lookup is
                                 performed */
    char *gk_policy;              /* gatekeeper rule string */
    uint32_t category_id;      /* category plume id */
    uint32_t confidence_level; /* risk/confidence level */
    uint64_t hit_counter;     /* will be updated when lookup is performed */
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
gk_cache_init(void);

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
 * @brief check ttl value for the given attribue
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
 * @brief check ttl value for the given attribue
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
 * @brief print cache'd entres.
 *
 */
void
gkc_cache_entries(void);

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
 * @params: req: interface structure specifing the attribute request
 *
 * @return true for success and false for failure.
 */
bool
gkc_lookup_attribute_entry(struct gk_attr_cache_interface *req, bool update_count);

/**
 * @brief add the given attribue to cache.
 *
 * @params: req: interface structure specifing the attribute request
 *
 * @return true for success and false for failure.
 */
bool
gkc_add_attribute_entry(struct gk_attr_cache_interface *entry);

/**
 * @brief delete the given attribute from cache
 *
 * @params req: interface structure specifing the attribute request
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
 * @params: req: interface structure specifing the flow request
 *
 * @return true for success and false for failure.
 */
bool
gkc_add_flow_entry(struct gkc_ip_flow_interface *entry);

/**
 * @brief lookup the given IP tuple flow to cache.
 *
 * @params: req: interface structure specifing the flow request
 *
 * @return true for success and false for failure.
 */
bool
gkc_lookup_flow(struct gkc_ip_flow_interface *req, int update_count);

/**
 * @brief delete the given IP tuple flow to cache.
 *
 * @params: req: interface structure specifing the flow request
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
                            int update_count);

/**
 * @brief comparator function for 5-tuple
 *
 * @params: _a: first 5-tuple structure to compare
 * @params: -b: second 5-tuple structure to compare
 *
 * @return 0 if equal
 */
int
gkc_flow_entry_cmp(void *_a, void *_b);

/**
 * @brief validate the request input.
 *
 * @params: req: interface structure specifing the attribute request
 * @return true for success and false for failure.
 */
bool
gkc_is_input_valid(struct gkc_ip_flow_interface *req);

/**
 * @brief delete the given flow from the flow
 *        tree if TTL is expired
 *
 * @params: tree attribute tree pointer
 * @params: pdevice per device pointer
 * @params: attr_type attribute type to check
 */
void
gkc_cleanup_ttl_flow_tree(ds_tree_t *flow_tree,
                          struct per_device_cache *pdevice,
                          enum gk_cache_request_type attr_type);

unsigned long
gk_get_cache_count(void);

void
clear_gatekeeper_cache(void);

#endif /* GK_CACHE_H_INCLUDED */
