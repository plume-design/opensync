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

#include <stdint.h>
#include <time.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "fsm_policy.h"
#include "log.h"
#include "os.h"
#include "os_types.h"
#include "dns_cache.h"
#include "memutil.h"

#define GATEKEEPER  "gatekeeper"
#define WEBPULSE    "webpulse"
#define BRIGHTCLOUD "brightcloud"

static struct dns_cache_mgr
mgr =
{
    .initialized = false,
    .refcount = 0,
};

struct dns_cache_mgr *
dns_cache_get_mgr(void)
{
    return &mgr;
}

static int
dns_cache_ip2action_cmp(void *_a, void *_b)
{
    struct ip2action *a = (struct ip2action *)_a;
    struct ip2action *b = (struct ip2action *)_b;
    size_t len;
    uint8_t *i_a;
    uint8_t *i_b;
    int fdiff;
    int idiff;
    size_t i;
    int mac_cmp;

    /* compare mac-address */
    mac_cmp = memcmp(a->device_mac, b->device_mac, sizeof(os_macaddr_t));
    if (mac_cmp != 0) return mac_cmp;

    /* Compare af families */
    fdiff = (a->af_family - b->af_family);
    if (fdiff != 0) return fdiff;

    /* Compare ip address */
    len = (a->af_family == AF_INET) ? 4 : 16;
    i_a = a->ip_tbl;
    i_b = b->ip_tbl;

    for (i = 0; i != len; i++)
    {
        idiff = (*i_a - *i_b);
        if (idiff) return idiff;
        i_a++;
        i_b++;
    }

    return 0;
}

static void
print_dns_cache_entry(struct ip2action *i2a)
{
    char                   ipstr[INET6_ADDRSTRLEN] = { 0 };
    os_macaddr_t           nullmac = {{ 0 }};
    os_macaddr_t           *pmac;
    const char             *ip;
    size_t                 index;

    if (!i2a) return;

    ip = inet_ntop(i2a->ip_addr->ss_family, i2a->ip_tbl, ipstr, sizeof(ipstr));
    if (ip == NULL)
    {
        LOGD("%s: inet_ntop failed: %s", __func__, strerror(errno));
        return;
    }

    pmac = (i2a->device_mac != NULL) ? i2a->device_mac : &nullmac;
    LOGD("ip %s, mac "PRI_os_macaddr_lower_t
         " action: %d action_by_name: %d ttl: %d policy_idx: %d service_id: %d redirect flag: %d"
         " unknown_cat: %d", ipstr, FMT_os_macaddr_pt(pmac),
         i2a->action, i2a->action_by_name, i2a->cache_ttl, i2a->policy_idx,
         i2a->service_id, i2a->redirect_flag, i2a->cat_unknown_to_service);

    if (i2a->service_id == IP2ACTION_WP_SVC)
    {
        LOGD("%s risk_level: %d", __func__, i2a->cache_wb.risk_level);
    }
    else if (i2a->service_id == IP2ACTION_BC_SVC)
    {
        LOGD("%s reputationScore: %d", __func__, i2a->cache_bc.reputation);
    }
    else
    {
        LOGD("%s confidence_level: %d category_id: %d gk_policy: %s",
             __func__, i2a->cache_gk.confidence_level,
             i2a->cache_gk.category_id, i2a->cache_gk.gk_policy);
    }

    for (index = 0; index < i2a->nelems; index++)
    {
        LOGD(" %s categories: %d", __func__, i2a->categories[index]);

        if (i2a->service_id == IP2ACTION_BC_SVC)
        {
            LOGD(" %s confidence_levels: %d", __func__,
                 i2a->cache_bc.confidence_levels[index]);
        }
    }

}

uint8_t
dns_cache_get_service_provider(char *service_provider)
{
    int rc;

    rc = strncmp(service_provider, GATEKEEPER, strlen(GATEKEEPER));
    if (!rc) return IP2ACTION_GK_SVC;

    rc = strncmp(service_provider, WEBPULSE, strlen(WEBPULSE));
    if (!rc) return IP2ACTION_WP_SVC;

    rc = strncmp(service_provider, BRIGHTCLOUD, strlen(BRIGHTCLOUD));
    if (!rc) return IP2ACTION_BC_SVC;

    return IP2ACTION_UNKNOWN_SVC;
}

void
dns_cache_disable(void)
{
    struct dns_cache_mgr *mgr;
    uint8_t cache_source;
    uint8_t service_id;

    mgr = dns_cache_get_mgr();
    if (!mgr->initialized) return;

    dns_cache_cleanup();

    for (service_id = 0; service_id < SERVICE_PROVIDER_MAX_ELEMS; service_id++)
    {
        mgr->cache_hit_count[service_id] = 0;
    }

    for (cache_source = 0; cache_source < DNS_CACHE_SOURCE_MAX; cache_source++)
    {
        mgr->disable_dns_cache[cache_source] = false;
    }
    mgr->refcount = 0;
    mgr->initialized = false;
}

static bool
disable_dns_cache(struct dns_cache_settings *cache_init)
{
    struct dns_cache_mgr *mgr;

    mgr = dns_cache_get_mgr();
    if (!mgr->initialized) return false;

    /* Disable dns_cache if DNS and ipthreat using gatekeeper as service */
    if (mgr->disable_dns_cache[MODULE_DNS_PARSE] &&
        mgr->disable_dns_cache[MODULE_IPTHREAT_DPI])
    {
        dns_cache_disable();
        return true;
    }

    return false;
}

/**
 * @brief initialize dns_cache manager.
 */
void
dns_cache_init_mgr(struct dns_cache_mgr *mgr)
{
    if (!mgr) return;

    if (mgr->initialized)
    {
        mgr->refcount++;
        return;
    }

    ds_tree_init(&mgr->ip2a_tree, dns_cache_ip2action_cmp,
                 struct ip2action, ip2a_tnode);

    mgr->initialized = true;
    mgr->refcount++;
    return;

}

/**
 * @brief initialize dns_cache.
 *
 * receive none
 *
 * @return true for success and false for failure.
 */
bool
dns_cache_init(struct dns_cache_settings *cache_init)
{
    struct dns_cache_mgr *mgr;
    bool rc;

    mgr = dns_cache_get_mgr();
    rc = (cache_init->dns_cache_source == MODULE_DNS_PARSE);
    rc &= (cache_init->service_provider == IP2ACTION_GK_SVC);
    if (!rc)
    {
        dns_cache_init_mgr(mgr);
    }

    if (cache_init->service_provider == IP2ACTION_GK_SVC)
    {
        mgr->disable_dns_cache[cache_init->dns_cache_source] = true;
    }

    rc = disable_dns_cache(cache_init);
    if (rc == true)
    {
        LOGD("%s : disabling DNS cache", __func__);
    }

    return mgr->initialized;
}

/**
 * @brief gk cleanup allocated memory.
 *
 * receive cached structure.
 *
 * @return void.
 */
void
dns_cache_free_gk_cache_entry(struct ip2action *i2a)
{
   if (i2a->service_id == IP2ACTION_GK_SVC)
   {
       FREE(i2a->cache_gk.gk_policy);
   }
}

static void
dns_cache_free_ip2action(struct ip2action *i2a)
{
   if (!i2a) return;

   FREE(i2a->device_mac);
   FREE(i2a->ip_addr);
   dns_cache_free_gk_cache_entry(i2a);
   return;
}

void
dns_cache_cleanup(void)
{
    struct dns_cache_mgr *mgr = dns_cache_get_mgr();
    struct ip2action *i2a_entry, *i2a_next;
    ds_tree_t *tree;

    if (!mgr->initialized) return;

    tree = &mgr->ip2a_tree;
    i2a_entry = ds_tree_head(tree);
    while (i2a_entry != NULL)
    {
        i2a_next = ds_tree_next(tree, i2a_entry);
        ds_tree_remove(tree, i2a_entry);
        dns_cache_free_ip2action(i2a_entry);
        FREE(i2a_entry);
        mgr->entries--;
        i2a_entry = i2a_next;
    }
    return;
}

/**
 * @brief cleanup allocated memory.
 *
 * receive none
 *
 * @return void.
 */
void
dns_cache_cleanup_mgr(void)
{
    struct dns_cache_mgr *mgr = dns_cache_get_mgr();

    if (!mgr->initialized) return;
    mgr->refcount--;

    if (mgr->refcount == 0) dns_cache_disable();
}


static void
dns_cache_set_ip(struct ip2action *i2a)
{
    struct sockaddr_storage *ipaddr;

    if (!i2a) return;

    if (i2a->af_family == 0) i2a->af_family = i2a->ip_addr->ss_family;

    ipaddr = i2a->ip_addr;
    if (i2a->af_family == AF_INET)
    {
        struct sockaddr_in   *in4;
        in4 = (struct sockaddr_in *)ipaddr;
        i2a->ip_tbl = (uint8_t *)(&in4->sin_addr.s_addr);
    }
    else if (i2a->af_family == AF_INET6)
    {
        struct sockaddr_in6  *in6;
        in6 = (struct sockaddr_in6 *)ipaddr;
        i2a->ip_tbl = (uint8_t *)(&in6->sin6_addr.s6_addr);
    }
    return;
}


static bool
dns_cache_update_ip2action(struct ip2action *i2a, struct ip2action_req *to_upd)
{
    if (!i2a || !to_upd) return false;

    if (to_upd->action != FSM_ACTION_NONE)
    {
        i2a->action = to_upd->action;
    }

    if (to_upd->action_by_name != FSM_ACTION_NONE)
    {
        i2a->action_by_name = to_upd->action_by_name;
    }

    i2a->redirect_flag = to_upd->redirect_flag;
    i2a->cache_ttl = to_upd->cache_ttl;
    i2a->cache_ts  = time(NULL);
    return true;
}

static struct ip2action *
dns_cache_lookup_ip2action(struct ip2action_req *req)
{
    struct dns_cache_mgr *mgr = dns_cache_get_mgr();
    struct ip2action     i2a_lkp;
    struct ip2action     *i2a;

    if (!req) return NULL;

    if (!req->ip_addr || !req->device_mac) return NULL;

    memset(&i2a_lkp, 0, sizeof(struct ip2action));

    i2a_lkp.ip_addr = req->ip_addr;
    dns_cache_set_ip(&i2a_lkp);
    i2a_lkp.device_mac = req->device_mac;

    i2a = ds_tree_find(&mgr->ip2a_tree, &i2a_lkp);
    if (i2a != NULL) return i2a;

    return NULL;
}

/**
 * @brief Set wb details..
 *
 * receive wb dst and src.
 *
 * @return void.
 *
 */
bool
dns_cache_set_wb_cache_entry(struct ip2action_wb_info *i2a_cache_wb,
                             struct ip2action_wb_info *to_add_cache_wb)
{
    if (!to_add_cache_wb->risk_level) return false;
    i2a_cache_wb->risk_level = to_add_cache_wb->risk_level;

    return true;
}

/**
 * @brief Set bc details.
 *
 * receive bc dst, src and nelems.
 *
 * @return void.
 *
 */
bool
dns_cache_set_bc_cache_entry(struct ip2action_bc_info *i2a_cache_bc,
                             struct ip2action_bc_info *to_add_cache_bc,
                             uint8_t nelems)
{
    size_t index;

    if (!to_add_cache_bc->reputation) return false;
    i2a_cache_bc->reputation = to_add_cache_bc->reputation;

    if (!nelems) return false;
    for (index = 0; index < nelems; index++)
    {
        i2a_cache_bc->confidence_levels[index] =
            to_add_cache_bc->confidence_levels[index];
    }

    return true;
}

/**
 * @brief Set gk details.
 *
 * receive gk dst and src.
 *
 * @return void.
 *
 */
bool
dns_cache_set_gk_cache_entry(struct ip2action_gk_info *i2a_cache_gk,
                             struct ip2action_gk_info *to_add_cache_gk)
{
    i2a_cache_gk->confidence_level = to_add_cache_gk->confidence_level;
    i2a_cache_gk->category_id = to_add_cache_gk->category_id;

    if (to_add_cache_gk->gk_policy)
    {
        i2a_cache_gk->gk_policy = STRDUP(to_add_cache_gk->gk_policy);
    }

    return true;
}

bool
dns_cache_update_entry_action(struct ip2action_req *req, struct ip2action *i2a)
{
    if (req == NULL) return false;
    if (i2a == NULL) return false;

    if ((i2a->action_by_name == FSM_ACTION_NONE) && (i2a->action != FSM_ACTION_NONE))
    {
         /* entry added by IP threat. First request */
        req->action = i2a->action;
    }
    else if ((i2a->action_by_name == FSM_ALLOW) && (i2a->action == FSM_ACTION_NONE))
    {
        /* entry added by dns. First request */
        req->action = i2a->action_by_name;
    }
    else if ((i2a->action_by_name == FSM_ALLOW) && (i2a->action != FSM_ACTION_NONE))
    {
        req->action = i2a->action;
    }
    else if ((i2a->action_by_name == FSM_BLOCK) && (i2a->action == FSM_ACTION_NONE))
    {
        /* entry added during dns response ips. action is block enforcing IP check */
        LOGT("%s: dns action is block, enforcing gatekeeper for IP check", __func__);
        return false;
    }
    else if ((i2a->action_by_name == FSM_BLOCK) && (i2a->action != FSM_ACTION_NONE))
    {
        req->action = i2a->action;
    }

    return true;
}

/**
 * @brief Lookup cached action for given ip address and mac.
 * Caller is responsible for freeing the memory as it uses STRDUP().
 *
 * receive ipaddress and mac of device.
 *
 * output action.
 *
 * @return true for success and false for failure.
 */
bool
dns_cache_ip2action_lookup(struct ip2action_req *req)
{
   struct dns_cache_mgr *mgr;
   struct ip2action *i2a;
   size_t index;
   bool rc;

   if (!req) return false;

   mgr = dns_cache_get_mgr();
   i2a = dns_cache_lookup_ip2action(req);
   if (i2a == NULL)
   {
       return false;
   }

   rc = dns_cache_update_entry_action(req, i2a);
   if (!rc) return false;

   if (i2a->action_by_name != FSM_ACTION_NONE)
        req->action_by_name = i2a->action_by_name;

   req->cache_ttl = i2a->cache_ttl;
   req->policy_idx = i2a->policy_idx;
   req->service_id = i2a->service_id;
   req->nelems = i2a->nelems;
   req->cat_unknown_to_service = i2a->cat_unknown_to_service;

   if (!req->cat_unknown_to_service) mgr->cache_hit_count[req->service_id]++;
   req->redirect_flag = i2a->redirect_flag;

   for (index = 0; index < i2a->nelems; ++index)
   {
       req->categories[index] = i2a->categories[index];
   }

   if (req->service_id == IP2ACTION_BC_SVC)
   {
       dns_cache_set_bc_cache_entry(&req->cache_bc, &i2a->cache_bc,
                                    i2a->nelems);
   }
   else if (req->service_id == IP2ACTION_WP_SVC)
   {
       dns_cache_set_wb_cache_entry(&req->cache_wb, &i2a->cache_wb);
   }
   else if (req->service_id == IP2ACTION_GK_SVC)
   {
       dns_cache_set_gk_cache_entry(&req->cache_gk, &i2a->cache_gk);
   }
   else
   {
       LOGD("%s : service id %d no recognized", __func__, i2a->service_id);
       return false;
   }

   LOGD("%s: found entry:", __func__);

   return true;
}


/**
 * @brief Lookup cached action and policy idx for given ip address and mac.
 *
 * receive ipaddress and mac of device.
 *
 * output action and policy idx.
 *
 * @return true for success and false for failure.
 */
bool
dns_cache_get_policy_action(struct ip2action_req *req)
{
   struct dns_cache_mgr *mgr;
   struct ip2action *i2a;

   if (!req) return false;

   mgr = dns_cache_get_mgr();
   if (!mgr->initialized) return false;

   i2a = dns_cache_lookup_ip2action(req);
   if (i2a == NULL) return false;

   req->action = i2a->action;
   req->policy_idx = i2a->policy_idx;
   return true;
}


static struct ip2action *
dns_cache_alloc_ip2action(struct ip2action_req  *to_add)
{
    struct ip2action *i2a;
    size_t index;
    bool rc;

    i2a = CALLOC(1, sizeof(struct ip2action));
    if (i2a == NULL)
    {
        LOGE("%s: Couldn't allocate memory for ip2action entry.",__func__);
        return NULL;
    }
    i2a->device_mac = CALLOC(1, sizeof(os_macaddr_t));
    memcpy(i2a->device_mac, to_add->device_mac, sizeof(os_macaddr_t));

    i2a->ip_addr = CALLOC(1, sizeof(struct sockaddr_storage));
    memcpy(i2a->ip_addr, to_add->ip_addr, sizeof(struct sockaddr_storage));

    if (to_add->action != FSM_ACTION_NONE)
        i2a->action  = to_add->action;

    if (to_add->action_by_name != FSM_ACTION_NONE)
        i2a->action_by_name = to_add->action_by_name;

    i2a->cache_ttl  = to_add->cache_ttl;
    i2a->cache_ts  = time(NULL);
    i2a->policy_idx = to_add->policy_idx;
    i2a->service_id = to_add->service_id;
    i2a->redirect_flag = to_add->redirect_flag;
    i2a->nelems = to_add->nelems;
    i2a->cat_unknown_to_service = to_add->cat_unknown_to_service;

    for (index = 0; index < to_add->nelems; ++index)
    {
        i2a->categories[index] = to_add->categories[index];
    }

    if (i2a->service_id == IP2ACTION_BC_SVC)
    {
        rc = dns_cache_set_bc_cache_entry(&i2a->cache_bc, &to_add->cache_bc,
                                          to_add->nelems);
    }
    else if (i2a->service_id == IP2ACTION_WP_SVC)
    {
        rc = dns_cache_set_wb_cache_entry(&i2a->cache_wb, &to_add->cache_wb);
    }
    else if (i2a->service_id == IP2ACTION_GK_SVC)
    {
        rc = dns_cache_set_gk_cache_entry(&i2a->cache_gk, &to_add->cache_gk);
    }
    else
    {
        rc = false;
        LOGD("%s : service id %d no recognized", __func__, to_add->service_id);
    }

    if (!rc) goto err;

    dns_cache_set_ip(i2a);
    return i2a;

err:
    FREE(i2a->ip_addr);
    FREE(i2a->device_mac);
    FREE(i2a);

    return NULL;
}


/**
 * @brief Add ip-action entry.
 *
 * receive ip2action_req.
 *
 * @return true for success and false for failure.
 */
bool
dns_cache_add_entry(struct ip2action_req *to_add)
{
    struct dns_cache_mgr *mgr = dns_cache_get_mgr();
    struct ip2action     *i2a;

    if (!mgr->initialized || !to_add) return false;

    i2a = dns_cache_lookup_ip2action(to_add);
    if (i2a != NULL)
    {
        dns_cache_update_ip2action(i2a, to_add);

        LOGD("%s: ip2action_cache updated entry in cache:", __func__);
        return true;
    }

    /* Allocate and insert a new ip-action */
    i2a = dns_cache_alloc_ip2action(to_add);
    if (i2a == NULL) return false;

    LOGD("%s: ip2action_cache adding to cache:", __func__);

    mgr->entries++;
    ds_tree_insert(&mgr->ip2a_tree, i2a, i2a);

    return true;
}

/**
 * @brief Delete fqdn entry.
 *
 * receive dns_cache_entry.
 *
 * @return true for success and false for failure.
 */
bool
dns_cache_del_entry(struct ip2action_req *to_del)
{
    struct dns_cache_mgr *mgr = dns_cache_get_mgr();
    struct ip2action     *i2a;

    if (!mgr->initialized) return false;

    i2a = dns_cache_lookup_ip2action(to_del);
    if (i2a == NULL) return true;

    LOGD("%s: ip2action_cache removing entry:", __func__);

    /* free ip2action entry  */
    dns_cache_free_ip2action(i2a);
    ds_tree_remove(&mgr->ip2a_tree, i2a);
    FREE(i2a);

    mgr->entries--;
    return true;
}


/**
 * @brief remove old cache entres.
 *
 * @param ttl
 */
bool
dns_cache_ttl_cleanup(void)
{
    struct dns_cache_mgr *mgr = dns_cache_get_mgr();
    struct ip2action     *i2a, *next;
    ds_tree_t            *tree;
    time_t               now;

    if (!mgr->initialized) return false;

    LOGD("%s: ip2action_cache removing ttl expired entries", __func__);
    now = time(NULL);

    tree = &mgr->ip2a_tree;

    i2a = ds_tree_head(tree);
    while (i2a != NULL)
    {
        if ((now - i2a->cache_ts) < i2a->cache_ttl)
        {
            i2a = ds_tree_next(tree, i2a);
            continue;
        }
        next = ds_tree_next(tree, i2a);
        ds_tree_remove(tree, i2a);
        dns_cache_free_ip2action(i2a);
        FREE(i2a);
        mgr->entries--;
        i2a = next;
    }
    return true;
}

/**
 * @brief print cache'd entres.
 *
 */
void
print_dns_cache(void)
{
    struct dns_cache_mgr *mgr = dns_cache_get_mgr();
    struct ip2action     *i2a;
    ds_tree_t            *tree;

    if (!mgr->initialized) return;

    tree = &mgr->ip2a_tree;

    LOGT("%s: dns_cache dump", __func__);

    ds_tree_foreach(tree, i2a)
    {
        print_dns_cache_entry(i2a);
    }
    LOGT("=====END=====");
    return;
}

/**
 * @brief returns cache size.
 *
 * @param None
 *
 * @return cache size
 */
int
dns_cache_get_size(void)
{
    struct dns_cache_mgr *mgr = dns_cache_get_mgr();

    if (!mgr->initialized) return -1;
    return mgr->entries;
}

/**
 * @brief print cache size.
 *
 * @param None
 *
 * @return None
 */
void
print_dns_cache_size(void)
{
    int no_of_elements;
    no_of_elements = dns_cache_get_size();
    LOGT("%s: ip2action_cache %d IPs cached", __func__, no_of_elements);
    return;
}

/**
 * @brief returns cache ref count.
 *
 * @param None
 *
 * @return cache refcount
 */
uint8_t
dns_cache_get_refcount(void)
{
    struct dns_cache_mgr *mgr;

    mgr = dns_cache_get_mgr();
    if (!mgr->initialized) return 0;
    return mgr->refcount;
}

/**
 * @brief returns cache hit count.
 *
 * @param None
 *
 * @return cache hit count
 */
uint32_t
dns_cache_get_hit_count(uint8_t service_id)
{
    struct dns_cache_mgr *mgr;

    mgr = dns_cache_get_mgr();
    if (!mgr->initialized) return 0;
    return mgr->cache_hit_count[service_id];
}

/**
 * @brief print cache hit count.
 *
 * @param service_id
 *
 * @return None
 */
void
print_dns_cache_hit_count(void)
{
    uint32_t hit_count;
    uint8_t service_id;

    for (service_id = 0; service_id < SERVICE_PROVIDER_MAX_ELEMS; service_id++)
    {
        hit_count = dns_cache_get_hit_count(service_id);
        LOGT("%s: service_id: %u cache hit count: %u", __func__,
             service_id, hit_count);
    }
    return;
}

/**
 * @brief print dns_cache details.
 *
 * @param service_id
 *
 * @return None
 */
void
print_dns_cache_details(void)
{
    struct dns_cache_mgr *mgr;

    mgr = dns_cache_get_mgr();
    if (!mgr->initialized) return;

    print_dns_cache_size();
    print_dns_cache_hit_count();
}

/**
 * @brief dns_cache status
 *
 * @return true on dns_cache disabled
 */
bool
is_dns_cache_disabled(void)
{
    struct dns_cache_mgr *mgr;

    mgr = dns_cache_get_mgr();
    if (!mgr->initialized) return true;

    return false;
}
