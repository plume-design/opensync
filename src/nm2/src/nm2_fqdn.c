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

#ifdef CONFIG_LIBEVX_USE_CARES

#include <ares.h>
#include <ev.h>
#include <netdb.h>

#include "ds_tree.h"
#include "ds_util.h"
#include "evx.h"
#include "log.h"
#include "nm2.h"
#include "os_time.h"
#include "ovsdb_table.h"

#define MODULE_ID LOG_MODULE_ID_ARES

static ovsdb_table_t table_FQDN_Resolve;
static ovsdb_table_t table_Openflow_Tag;
static ovsdb_table_t table_Openflow_Local_Tag;

static struct evx_ares g_ares;

#define NM2_FQDN_DELAY_NOW 0
#define NM2_FQDN_DELAY_BUSY 5
#define NM2_FQDN_DELAY_TTL_MIN 60

// exponential retry delay on error
// 15 sec, 1 minute, 4 min, 15 min, 1 hour
static const int nm2_fqdn_delay_table[] = {15, 60, 4 * 60, 15 * 60, 60 * 60};
static const int nm2_fqdn_delay_table_len = ARRAY_LEN(nm2_fqdn_delay_table);

static ev_timer g_nm2_fqdn_resolve_timer;

typedef enum fqdn_entry_state
{
    FQDN_ENTRY_INIT = 0,
    FQDN_ENTRY_NOP,
    FQDN_ENTRY_PROGRESS,
    FQDN_ENTRY_SUCCESS,
    FQDN_ENTRY_ERROR,
} fqdn_entry_state_t;

typedef struct fqdn_entry fqdn_entry_t;
typedef struct fqdn_entry_af fqdn_entry_af_t;

struct fqdn_entry_af  // address family
{
    int af_type;
    char *tag;
    char (*result)[INET6_ADDRSTRLEN];
    int num_result;
};

struct fqdn_entry
{
    ds_tree_node_t node;
    char *key;
    // ovsdb config
    char *fqdn;
    char *server;
    int interval;
    fqdn_entry_af_t ipv4;
    fqdn_entry_af_t ipv6;
    // entry state
    fqdn_entry_state_t state;
    bool discarded;
    bool busy;
    time_t retry_at_mono;
    int retry_count;
    int ttl;
    struct evx_ares *e_ares;
};

ds_tree_t g_fqdn_list = DS_TREE_INIT(ds_str_cmp, fqdn_entry_t, node);
ds_tree_t g_fqdn_discard = DS_TREE_INIT(ds_str_cmp, fqdn_entry_t, node);

void callback_FQDN_Resolve(
        ovsdb_update_monitor_t *mon,
        struct schema_FQDN_Resolve *old,
        struct schema_FQDN_Resolve *new);

void nm2_fqdn_schedule_update(double delay);

// address family to IPvN str
static char *af_ip_str(int af)
{
    switch (af)
    {
        case AF_INET:
            return "IPv4";
        case AF_INET6:
            return "IPv6";
        case AF_UNSPEC:
            return "UNSPEC";
        default:
            return "UNK";
    }
}

char *nm2_sockaddr_to_str(struct sockaddr *sa, char *str, int size)
{
    struct sockaddr_in *sa4 = (struct sockaddr_in *)sa;
    struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)sa;
    switch (sa->sa_family)
    {
        case AF_INET:
            return (char *)inet_ntop(sa4->sin_family, &sa4->sin_addr, str, size);
        case AF_INET6:
            return (char *)inet_ntop(sa6->sin6_family, &sa6->sin6_addr, str, size);
        default:
            return NULL;
    }
}

char *nm2_fqdn_entry_key(struct schema_FQDN_Resolve *rec)
{
    ASSERT(rec != NULL, "");
    char *key = strfmt("%s %s %s", rec->fqdn, rec->ipv4_tag, rec->ipv6_tag);
    ASSERT(key != NULL, "");
    return key;
}

fqdn_entry_t *nm2_fqdn_entry_new(struct schema_FQDN_Resolve *rec)
{
    fqdn_entry_t *e = CALLOC(sizeof(fqdn_entry_t), 1);
    e->fqdn = STRDUP(rec->fqdn);
    e->ipv4.tag = STRDUP(rec->ipv4_tag);
    e->ipv6.tag = STRDUP(rec->ipv6_tag);
    e->server = STRDUP(rec->server);
    e->key = nm2_fqdn_entry_key(rec);
    ds_tree_insert(&g_fqdn_list, e, e->key);
    e->ipv4.af_type = AF_INET;
    e->ipv6.af_type = AF_INET6;
    if (rec->interval_exists)
    {
        // interval defined: override ttl value,
        // interval 0 means never update
        e->interval = rec->interval;
    }
    else
    {
        // interval not defined - use dns ttl
        e->interval = -1;
    }
    if (rec->server_exists && *e->server)
    {
        // custom dns server, create a separate ares channel
        e->e_ares = CALLOC(1, sizeof(*e->e_ares));
        evx_init_ares(EV_DEFAULT, e->e_ares, NULL);
        evx_ares_set_server(e->e_ares, e->server);
    }
    return e;
}

void nm2_fqdn_entry_free_af(fqdn_entry_af_t *af)
{
    free(af->tag);
    free(af->result);
}

void nm2_fqdn_entry_free(fqdn_entry_t *e)
{
    if (!e) return;
    if (e->e_ares)
    {
        evx_stop_ares(e->e_ares);
        free(e->e_ares);
    }
    free(e->fqdn);
    free(e->key);
    free(e->server);
    nm2_fqdn_entry_free_af(&e->ipv4);
    nm2_fqdn_entry_free_af(&e->ipv6);
    free(e);
}

fqdn_entry_t *nm2_fqdn_entry_find(struct schema_FQDN_Resolve *rec)
{
    char *key = nm2_fqdn_entry_key(rec);
    fqdn_entry_t *e = (fqdn_entry_t *)ds_tree_find(&g_fqdn_list, key);
    free(key);
    return e;
}

void nm2_fqdn_entry_discard(fqdn_entry_t *e)
{
    if (!e) return;
    if (e->discarded) return;
    ds_tree_remove(&g_fqdn_list, e);
    ds_tree_insert(&g_fqdn_discard, e, e->key);
    e->discarded = true;
}

void nm2_fqdn_entry_delete_discarded(fqdn_entry_t *e)
{
    if (!e) return;
    if (!e->discarded) return;
    ds_tree_remove(&g_fqdn_discard, e);
    nm2_fqdn_entry_free(e);
}

struct evx_ares *nm2_fqdn_entry_ares(fqdn_entry_t *e)
{
    if (e->e_ares) return e->e_ares;
    return &g_ares;
}

bool nm2_fqdn_entry_compare_results(fqdn_entry_af_t *af, char ips[][INET6_ADDRSTRLEN], int num_ips)
{
    // ovsdb fields: Openflow_Local_Tag.values and Openflow_Tag.device_value
    // are sets, as such they don't preserve the order so use a set compare
    // when determining if ovsdb needs an update
    if (num_ips != af->num_result) return false;
    ds_set_str_t *a = ds_set_str_new();
    ds_set_str_t *b = ds_set_str_new();
    ds_set_str_insert_vl_array(a, af->num_result, sizeof(af->result[0]), af->result);
    ds_set_str_insert_vl_array(b, num_ips, sizeof(ips[0]), ips);
    bool equal = ds_set_str_compare(a, b) == 0;
    ds_set_str_delete(&a);
    ds_set_str_delete(&b);
    return equal;
}

void nm2_fqdn_entry_set_results(fqdn_entry_af_t *af, char ips[][INET6_ADDRSTRLEN], int num_ips)
{
    int i;
    free(af->result);
    af->result = NULL;
    af->num_result = num_ips;
    if (num_ips == 0) return;
    af->result = CALLOC(num_ips, sizeof(af->result[0]));
    for (i = 0; i < num_ips; i++)
    {
        strscpy(af->result[i], ips[i], sizeof(af->result[0]));
    }
}

bool nm2_fqdn_start_ares_check_busy(fqdn_entry_t *e)
{
    struct evx_ares *p_ares = nm2_fqdn_entry_ares(e);
    if (evx_start_ares(p_ares) != 0)
    {
        return false;
    }
    int busy = evx_ares_get_count_busy_fds(p_ares);
    const int max_busy = ARRAY_SIZE(p_ares->ctx);
    if (busy >= max_busy)
    {
        LOGD("ares[%s]: busy %d/%d", e->server, busy, max_busy);
        return false;
    }
    return true;
}

void nm2_fqdn_get_addrinfo_results(
        fqdn_entry_t *e,
        fqdn_entry_af_t *af,
        struct ares_addrinfo *result,
        char ips[][INET6_ADDRSTRLEN],
        int max_ips,
        int *num_ips,
        int *ttl)
{
    struct ares_addrinfo_node *node = NULL;
    int num = 0;
    int minttl = 0;
    char logstr[256] = "";
    char *s = logstr;
    size_t size = sizeof(logstr);
    for (node = result->nodes; node; node = node->ai_next)
    {
        if (num >= max_ips) break;
        struct sockaddr *sa = node->ai_addr;
        if (sa->sa_family != af->af_type) continue;
        if (!nm2_sockaddr_to_str(sa, ips[num], sizeof(ips[num]))) continue;
        if (!num || node->ai_ttl < minttl) minttl = node->ai_ttl;
        csnprintf(&s, &size, " %s", ips[num]);
        num++;
    }
    LOGT("ares[%s] resolved %s '%s' '%s' ttl: %d ips: [%d]%s",
         e->server,
         af_ip_str(af->af_type),
         e->fqdn,
         af->tag,
         minttl,
         num,
         logstr);
    *num_ips = num;
    *ttl = minttl;
}

bool nm2_fqdn_update_ovsdb_openflow_tag(char *tag, char ips[][INET6_ADDRSTRLEN], int num)
{
    struct schema_Openflow_Tag s;
    int i;
    if (!*tag)
    {
        // empty tag, ignore
        return true;
    }
    MEMZERO(s);
    s._partial_update = true;
    SCHEMA_SET_STR(s.name, tag);
    for (i = 0; i < num; ++i)
    {
        SCHEMA_VAL_APPEND(s.device_value, ips[i]);
    }
    // mark present so that the field gets deleted
    // if there are no ip entries (num_ips == 0)
    s.device_value_present = true;
    // upsert: insert if tag name does not exist
    return ovsdb_table_upsert(&table_Openflow_Tag, &s, false);
}

bool nm2_fqdn_update_ovsdb_openflow_local_tag(char *tag, char ips[][INET6_ADDRSTRLEN], int num)
{
    struct schema_Openflow_Local_Tag s;
    int i;
    if (!*tag)
    {
        // empty tag, ignore
        return true;
    }
    MEMZERO(s);
    s._partial_update = true;
    SCHEMA_SET_STR(s.name, tag);
    for (i = 0; i < num; ++i)
    {
        SCHEMA_VAL_APPEND(s.values, ips[i]);
    }
    // mark present so that the field gets deleted
    // if there are no ip entries (num_ips == 0)
    s.values_present = true;
    // upsert: insert if tag name does not exist
    return ovsdb_table_upsert(&table_Openflow_Local_Tag, &s, false);
}

char *nm2_fqdn_get_tag_and_type(char *tag, bool *local_tag)
{
    *local_tag = false;
    // "tag" or "@tag"  : Openflow_Tag
    // "*tag"           : Openflow_Local_Tag
    if (*tag == '*')
    {
        *local_tag = true;
        tag++;
    }
    else if (*tag == '@')
    {
        tag++;
    }
    return tag;
}

bool nm2_fqdn_update_ovsdb_tag(fqdn_entry_t *e, fqdn_entry_af_t *af, char ips[][INET6_ADDRSTRLEN], int num_ips)
{
    bool local_tag;
    char *tag = nm2_fqdn_get_tag_and_type(af->tag, &local_tag);
    // compare results, if they are equal, skip ovsdb update
    if (nm2_fqdn_entry_compare_results(af, ips, num_ips))
    {
        LOGT("fqdn %s '%s' '%s' same results [%d], skip ovsdb update",
             af_ip_str(af->af_type),
             e->fqdn,
             af->tag,
             num_ips);
        return true;
    }
    // else, store new results
    nm2_fqdn_entry_set_results(af, ips, num_ips);
    // update ovsdb
    if (local_tag)
    {
        return nm2_fqdn_update_ovsdb_openflow_local_tag(tag, ips, num_ips);
    }
    else
    {
        return nm2_fqdn_update_ovsdb_openflow_tag(tag, ips, num_ips);
    }
}

void nm2_fqdn_schedule_entry(fqdn_entry_t *e)
{
    double delay = -1;
    int i;
    if (e->discarded)
    {
        if (!e->busy)
        {
            delay = NM2_FQDN_DELAY_NOW;
        }
        goto update_timer;
    }
    switch (e->state)
    {
        case FQDN_ENTRY_INIT:
            delay = NM2_FQDN_DELAY_NOW;
            break;
        case FQDN_ENTRY_NOP:
            // nothing to do
            break;
        case FQDN_ENTRY_PROGRESS:
            // wait for results, handled by callback
            break;
        case FQDN_ENTRY_SUCCESS:
            if (e->interval >= 0)
            {
                delay = e->interval;
            }
            else
            {
                delay = e->ttl;
            }
            break;
        case FQDN_ENTRY_ERROR:
            // on error retry with exponentially increasing delay
            i = e->retry_count - 1;
            if (i >= nm2_fqdn_delay_table_len) i = nm2_fqdn_delay_table_len - 1;
            if (i < 0) i = 0;
            delay = nm2_fqdn_delay_table[i];
            break;
    }
update_timer:
    if (delay >= 0)
    {
        nm2_fqdn_schedule_update(delay);
        e->retry_at_mono = time_monotonic() + delay;
    }
    else
    {
        e->retry_at_mono = 0;
    }
}

void nm2_fqdn_ares_addrinfo_cb(void *arg, int status, int timeouts, struct ares_addrinfo *result)
{
    fqdn_entry_t *e = arg;
    const int MAX_IPS = C_FIELD_SZ(struct schema_Openflow_Tag, device_value);
    char ips[MAX_IPS][INET6_ADDRSTRLEN];
    int num_ips;
    int ttl = 0;
    int min_ttl = 0;

    if (!e)
    {
        LOGW("%s ares failed: arg=NULL [%d] %s", __func__, status, ares_strerror(status));
        goto ares_free;
    }
    if (e->discarded)
    {
        LOGD("%s ares[%s] discarded: '%s' [%d] %s", __func__, e->server, e->fqdn, status, ares_strerror(status));
        // discarded entry, update busy and schedule removal
        goto out;
    }
    if (!result || status != ARES_SUCCESS)
    {
        LOGD("%s ares[%s] failed '%s': [%d] %s\n", __func__, e->server, e->fqdn, status, ares_strerror(status));
        e->state = FQDN_ENTRY_ERROR;
        e->retry_count++;
        // delete old results if they exist
        nm2_fqdn_update_ovsdb_tag(e, &e->ipv4, NULL, 0);
        nm2_fqdn_update_ovsdb_tag(e, &e->ipv6, NULL, 0);
        goto out;
    }
    LOGT("%s ares[%s] success '%s'", __func__, e->server, e->fqdn);
    // update ovsdb ipv4 tag
    nm2_fqdn_get_addrinfo_results(e, &e->ipv4, result, ips, MAX_IPS, &num_ips, &ttl);
    nm2_fqdn_update_ovsdb_tag(e, &e->ipv4, ips, num_ips);
    if (min_ttl == 0 || ttl < min_ttl) min_ttl = ttl;
    // update ovsdb ipv6 tag
    nm2_fqdn_get_addrinfo_results(e, &e->ipv6, result, ips, MAX_IPS, &num_ips, &ttl);
    nm2_fqdn_update_ovsdb_tag(e, &e->ipv6, ips, num_ips);
    if (min_ttl == 0 || ttl < min_ttl) min_ttl = ttl;
    e->state = FQDN_ENTRY_SUCCESS;
    e->retry_count = 0;
    // prevent too frequent polling if ttl is small
    if (min_ttl < NM2_FQDN_DELAY_TTL_MIN) min_ttl = NM2_FQDN_DELAY_TTL_MIN;
    e->ttl = min_ttl;
out:
    e->busy = false;
    nm2_fqdn_schedule_entry(e);
ares_free:
    if (result) ares_freeaddrinfo(result);
}

bool nm2_fqdn_check_ip_address(fqdn_entry_t *e, double *delay)
{
    osn_ip_addr_t ip_addr;
    osn_ip6_addr_t ip6_addr;
    char ip_str[INET6_ADDRSTRLEN];

    if (STRSCPY(ip_str, e->fqdn) < 0) return false;
    // don't accept prefix (xxx/N) or lft values (xxx,N)
    if (strchr(ip_str, '/') || strchr(ip_str, ','))
    {
        LOGW("invalid chars in ip or name: %s", ip_str);
        return false;
    }
    if (osn_ip_addr_from_str(&ip_addr, ip_str))
    {
        LOGT("%s IPv4 address: %s", __func__, ip_str);
        // set IPv4 address
        nm2_fqdn_update_ovsdb_tag(e, &e->ipv4, &ip_str, 1);
        // clear IPv6 tag if set
        nm2_fqdn_update_ovsdb_tag(e, &e->ipv6, NULL, 0);
    }
    else if (osn_ip6_addr_from_str(&ip6_addr, ip_str))
    {
        LOGT("%s IPv6 address: %s", __func__, ip_str);
        // set IPv6 address
        nm2_fqdn_update_ovsdb_tag(e, &e->ipv6, &ip_str, 1);
        // clear IPv4 tag if set
        nm2_fqdn_update_ovsdb_tag(e, &e->ipv4, NULL, 0);
    }
    else
    {
        // not an IP address
        return false;
    }
    // success, update entry
    e->state = FQDN_ENTRY_SUCCESS;
    e->retry_count = 0;
    e->busy = false;
    // ip address doesn't need to be updated, disable ttl and schedule
    e->ttl = 0;
    e->retry_at_mono = 0;

    return true;
}

bool nm2_fqdn_update_active_entry(fqdn_entry_t *e, double *delay)
{
    time_t time_mono = time_monotonic();
    if (!*e->ipv4.tag && !*e->ipv6.tag)
    {
        // no tag specified, nothing needs to be done.
        e->state = FQDN_ENTRY_NOP;
        return true;
    }
    switch (e->state)
    {
        case FQDN_ENTRY_INIT:
            // initial state, go to resolve
            goto L_resolve;
        case FQDN_ENTRY_NOP:
            // no tag provided, nothing to do
            return true;
        case FQDN_ENTRY_PROGRESS:
            // resolution in progress, wait for results
            return true;
        case FQDN_ENTRY_SUCCESS:
            // resolve successful
            // if interval is 0 nothing else to do
            // otherwise check schedule for refresh
            if (e->interval == 0) return true;
            goto L_check_schedule;
        case FQDN_ENTRY_ERROR:
            // resolve error, check schedule for retry
            goto L_check_schedule;
        default:
            LOGW("%s unexpected state %d '%s'", __func__, e->state, e->fqdn);
    }
    return true;

L_check_schedule:
    if (e->retry_at_mono == 0)
    {
        // no interval scheduled
        return true;
    }
    if (e->retry_at_mono > time_mono)
    {
        // reschedule
        *delay = e->retry_at_mono - time_mono;
        return true;
    }
    // time is up, follow through to ares resolve

L_resolve:
    // check if name is already an IP address
    if (nm2_fqdn_check_ip_address(e, delay))
    {
        return true;
    }
    // not an IP address, resolve name using ares.
    if (!nm2_fqdn_start_ares_check_busy(e))
    {
        // if ares is busy reschedule for later
        *delay = NM2_FQDN_DELAY_BUSY;
        e->retry_at_mono = time_mono + *delay;
        return false;
    }
    e->state = FQDN_ENTRY_PROGRESS;
    e->busy = true;
    e->retry_at_mono = 0;

    // ares_addrinfo_hints:
    //  AF_UNSPEC       means return both AF_INET and AF_INET6.
    //  ARES_AI_NOSORT  Result addresses will not be sorted and no connections
    //                  to resolved addresses will be attempted.
    struct ares_addrinfo_hints hints = {
        .ai_flags = ARES_AI_NOSORT,
        .ai_family = AF_UNSPEC,
        .ai_socktype = 0,
        .ai_protocol = 0,
    };
    if (!*e->ipv4.tag)
    {
        // V4 disabled, only query V6
        hints.ai_family = AF_INET6;
    }
    if (!*e->ipv6.tag)
    {
        // V6 disabled, only query V4
        hints.ai_family = AF_INET;
    }
    LOGT("ares[%s]: request '%s' tags: '%s,%s' af: [%d] %s retry: %d",
         e->server,
         e->fqdn,
         e->ipv4.tag,
         e->ipv6.tag,
         hints.ai_family,
         af_ip_str(hints.ai_family),
         e->retry_count);
    ares_getaddrinfo(nm2_fqdn_entry_ares(e)->ares.channel, e->fqdn, NULL, &hints, nm2_fqdn_ares_addrinfo_cb, e);
    return true;
}

void nm2_fqdn_update_all()
{
    fqdn_entry_t *e;
    fqdn_entry_t *tmp;
    double delay = 0;
    double min_delay = -1;

    // update active entries
    ds_tree_foreach (&g_fqdn_list, e)
    {
        delay = -1;
        nm2_fqdn_update_active_entry(e, &delay);
        if (delay >= 0)
        {
            if (min_delay < 0 || delay < min_delay) min_delay = delay;
        }
    }
    // remove discarded if not busy
    ds_tree_foreach_safe (&g_fqdn_discard, e, tmp)
    {
        if (!e->busy)
        {
            nm2_fqdn_entry_delete_discarded(e);
        }
        else
        {
            delay = NM2_FQDN_DELAY_BUSY;
            if (min_delay < 0 || delay < min_delay) min_delay = delay;
        }
    }
    // update ev timer
    // if nothing needs to be done delay -1 will stop the timer
    nm2_fqdn_schedule_update(min_delay);
}

void nm2_fqdn_timer_cb(struct ev_loop *loop, ev_timer *t, int revents)
{
    nm2_fqdn_update_all();
}

void nm2_fqdn_schedule_update(double delay)
{
    double remain = -1;
    // start timer that calls the update
    if (ev_is_active(&g_nm2_fqdn_resolve_timer))
    {
        // if already active, and will trigger sooner than specified
        // then don't change it.
        remain = ev_timer_remaining(EV_DEFAULT, &g_nm2_fqdn_resolve_timer);
        if (remain < delay)
        {
            // already scheduled sooner than the requested delay
            // nothing needs to be changed
            TRACE("keep timer remain: %.3f delay: %.3f", remain, delay);
            return;
        }
        ev_timer_stop(EV_DEFAULT, &g_nm2_fqdn_resolve_timer);
    }
    int busy_count = evx_ares_get_count_busy_fds(&g_ares);
    if (delay < 0)
    {
        TRACE("stop timer ares: %d busy: %d", g_ares.chan_initialized, busy_count);
        return;
    }
    TRACE("start timer remain: %.3f delay: %.3f ares: %d busy: %d", remain, delay, g_ares.chan_initialized, busy_count);
    ev_timer_init(&g_nm2_fqdn_resolve_timer, nm2_fqdn_timer_cb, delay, 0);
    ev_timer_start(EV_DEFAULT, &g_nm2_fqdn_resolve_timer);
}

void nm2_fqdn_resolve_new(struct schema_FQDN_Resolve *rec)
{
    fqdn_entry_t *p = NULL;
    // check if valid record, at least the fqdn and one of the tags
    // must be defined, otherwise ignore entry
    if (!rec->fqdn[0])
    {
        LOGW("FQDN_Resolve fqdn missing, ignore: %s, %s, %s, %s",
             rec->_uuid.uuid,
             rec->ipv4_tag,
             rec->ipv6_tag,
             rec->server);
        return;
    }
    if (!rec->ipv4_tag[0] && !rec->ipv6_tag[0])
    {
        LOGW("FQDN_Resolve both tags missing, ignore: %s, %s, %s", rec->_uuid.uuid, rec->fqdn, rec->server);
        return;
    }
    // check if duplicate entry
    p = nm2_fqdn_entry_find(rec);
    if (p)
    {
        LOGW("FQDN_Resolve ignore duplicate: %s, %s, %s, %s, %s",
             rec->_uuid.uuid,
             rec->fqdn,
             rec->ipv4_tag,
             rec->ipv6_tag,
             rec->server);
        return;
    }
    p = nm2_fqdn_entry_new(rec);
    if (!p)
    {
        LOGE("FQDN_Resolve alloc: %s, %s, %s, %s", rec->fqdn, rec->ipv4_tag, rec->ipv6_tag, rec->server);
        return;
    }
    LOGD("FQDN_Resolve new: %s, %s, %s, %s", rec->fqdn, rec->ipv4_tag, rec->ipv6_tag, rec->server);

    nm2_fqdn_schedule_update(NM2_FQDN_DELAY_NOW);
}

void nm2_fqdn_resolve_delete(struct schema_FQDN_Resolve *rec)
{
    fqdn_entry_t *e = nm2_fqdn_entry_find(rec);
    if (!e) return;
    LOGD("FQDN_Resolve delete: %s, %s, %s, %s", rec->fqdn, rec->ipv4_tag, rec->ipv6_tag, rec->server);
    // remove associated Openflow_*_Tag device_value
    nm2_fqdn_update_ovsdb_tag(e, &e->ipv4, NULL, 0);
    nm2_fqdn_update_ovsdb_tag(e, &e->ipv6, NULL, 0);
    // mark entry as discarded
    nm2_fqdn_entry_discard(e);
    // start timer so that the discarded entries get deleted
    // by the update callback, if/when the entries are not busy
    nm2_fqdn_schedule_update(NM2_FQDN_DELAY_NOW);
}

void nm2_fqdn_resolve_update(struct schema_FQDN_Resolve *old, struct schema_FQDN_Resolve *new)
{
    fqdn_entry_t *e = nm2_fqdn_entry_find(old);

    if (e == NULL || new->fqdn_changed || new->ipv4_tag_changed || new->ipv6_tag_changed || new->server_changed)
    {
        // major change, remove old add new
        nm2_fqdn_resolve_delete(old);
        nm2_fqdn_resolve_new(new);
        return;
    }
    if (new->interval_changed)
    {
        // minor change: interval changed, schedule update
        e->interval = new->interval;
        nm2_fqdn_schedule_entry(e);
    }
}

/*
 * OVSDB monitor update callback
 */
void callback_FQDN_Resolve(
        ovsdb_update_monitor_t *mon,
        struct schema_FQDN_Resolve *old,
        struct schema_FQDN_Resolve *new)
{
    TRACE();

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            nm2_fqdn_resolve_new(new);
            break;

        case OVSDB_UPDATE_MODIFY:
            nm2_fqdn_resolve_update(old, new);
            break;

        case OVSDB_UPDATE_DEL:
            nm2_fqdn_resolve_delete(old);
            break;

        default:
            LOGW("%s:mon upd error: %d", __func__, mon->mon_type);
            return;
    }

    return;
}

/*
 * Initialize table monitors
 */
bool nm2_fqdn_resolve_init(void)
{
    LOG(DEBUG, "Initializing NM FQDN_Resolve monitor.");
    OVSDB_TABLE_INIT(FQDN_Resolve, fqdn);
    OVSDB_TABLE_MONITOR(FQDN_Resolve, false);
    OVSDB_TABLE_INIT(Openflow_Tag, name);
    OVSDB_TABLE_INIT(Openflow_Local_Tag, name);
    evx_init_ares(EV_DEFAULT, &g_ares, NULL);
    return true;
}

bool nm2_fqdn_resolve_stop(void)
{
    evx_stop_ares(&g_ares);
    return true;
}

#else  // ! CONFIG_LIBEVX_USE_CARES

// FQDN_Resolve requires CONFIG_LIBEVX_USE_CARES
// If it's disabled warn about it and log error if table is used

#include "log.h"
#include "ovsdb_table.h"

static ovsdb_table_t table_FQDN_Resolve;

void callback_FQDN_Resolve(
        ovsdb_update_monitor_t *mon,
        struct schema_FQDN_Resolve *old,
        struct schema_FQDN_Resolve *new)
{
    LOGE("FQDN_Resolve disabled (%s)", new->fqdn);
}

bool nm2_fqdn_resolve_init(void)
{
    LOGW("FQDN_Resolve disabled");
    OVSDB_TABLE_INIT(FQDN_Resolve, fqdn);
    OVSDB_TABLE_MONITOR(FQDN_Resolve, false);
    return true;
}

bool nm2_fqdn_resolve_stop(void)
{
    return true;
}

#endif
