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

#include "gatekeeper.h"
#include "gatekeeper_cache.h"
#include "gatekeeper_hero_stats.h"
#include "gatekeeper_hero_stats.pb-c.h"
#include "log.h"
#include "memutil.h"
#include "os.h"
#include "os_time.h"

static size_t MAX_WINDOWS = 10;
static size_t MAX_REPORTS = 50;
static size_t GK_HERO_STATS_MAX_REPORT_SIZE = 4096;

/**
 * @brief Time conversion helper which puts error information
 *        into output time string on failure
 */
static void time_to_string(const time_t from, char *to, const size_t size)
{
    if (!time_to_str(from, to, size))
    {
        snprintf(to, size, "TIME_ERR");
    }
}

/**
 * @brief The global hero cache report aggregator.
 *        This is a singleton and can/should not be directly
 *        be manipulated outside of this file.
 */
static struct gkc_report_aggregator report_aggr =
{
    .initialized = false
};

/* forward definition */
static size_t
get_report_header_size(struct gkc_report_aggregator *aggr);
static void
free_stats(Gatekeeper__HeroStats__HeroStats *pb);
static void
free_observation_window(Gatekeeper__HeroStats__HeroObservationWindow *window);

/**
 * @brief get the protobuf enum matching the internal representation of direction
 *
 * @param dir the internal enum representing flow direction
 * @return the value of the corresponding protobuf enum
 */
static Gatekeeper__HeroStats__HeroDirections
get_protobuf_direction_value(enum gkc_flow_direction dir)
{
    switch (dir)
    {
        case GKC_FLOW_DIRECTION_UNSPECIFIED : return GATEKEEPER__HERO_STATS__HERO_DIRECTIONS__HERO_DIR_UNSPECIFIED;
        case GKC_FLOW_DIRECTION_OUTBOUND    : return GATEKEEPER__HERO_STATS__HERO_DIRECTIONS__HERO_DIR_OUTBOUND;
        case GKC_FLOW_DIRECTION_INBOUND     : return GATEKEEPER__HERO_STATS__HERO_DIRECTIONS__HERO_DIR_INBOUND;
        case GKC_FLOW_DIRECTION_LAN2LAN     : return GATEKEEPER__HERO_STATS__HERO_DIRECTIONS__HERO_DIR_LAN2LAN;
        default                             : break;
    }
    LOGD("%s(): no such direction %d", __func__, dir);
    return GATEKEEPER__HERO_STATS__HERO_DIRECTIONS__HERO_DIR_UNSPECIFIED;
}

/**
 * @brief get the protobuf enum matching the internal representation of action
 *
 * @param action the internal enum representing the action to be performed
 * @return the value of the corresponding protobuf enum
 */
static Gatekeeper__HeroStats__HeroActions
get_protobuf_action_value(int action)
{
    switch (action)
    {
        case FSM_ALLOW    : return GATEKEEPER__HERO_STATS__HERO_ACTIONS__HERO_ACTION_ALLOW;
        case FSM_BLOCK    : return GATEKEEPER__HERO_STATS__HERO_ACTIONS__HERO_ACTION_BLOCK;
        case FSM_REDIRECT : return GATEKEEPER__HERO_STATS__HERO_ACTIONS__HERO_ACTION_REDIRECT;
        case FSM_FORWARD  : return GATEKEEPER__HERO_STATS__HERO_ACTIONS__HERO_ACTION_FORWARD;
        case FSM_REDIRECT_ALLOW  : return GATEKEEPER__HERO_STATS__HERO_ACTIONS__HERO_ACTION_REDIRECT_ALLOW;
        case FSM_NOANSWER   : return GATEKEEPER__HERO_STATS__HERO_ACTIONS__HERO_ACTION_NOANSWER;
    }
    LOGD("%s(): no such action %d", __func__, action);
    return GATEKEEPER__HERO_STATS__HERO_ACTIONS__HERO_ACTION_UNSPECIFIED;
}

struct gkc_report_aggregator *
gkhc_get_aggregator(void)
{
    return &report_aggr;
}

bool
gkhc_init_aggregator(struct gkc_report_aggregator *aggr, struct fsm_session *session)
{
    char *hs_max_report_size;
    long max_report_size;

    if (aggr->initialized) return true;

    LOGD("%s: initializing", __func__);

    if (session == NULL) return false;
    if (session->node_id == NULL) return false;
    if (session->location_id == NULL) return false;

    aggr->node_id = STRDUP(session->node_id);
    if (aggr->node_id == NULL) goto cleanup;
    aggr->location_id = STRDUP(session->location_id);
    if (aggr->location_id == NULL) goto cleanup;

    aggr->windows_max = MAX_WINDOWS;
    aggr->windows_idx = 0;
    aggr->windows_prov = aggr->windows_max;
    aggr->windows = CALLOC(aggr->windows_prov, sizeof(*aggr->windows));
    if (aggr->windows == NULL) goto cleanup;

    aggr->stats_max = MAX_REPORTS;
    aggr->stats_idx = 0;
    aggr->stats_prov = aggr->stats_max;
    aggr->stats = CALLOC(aggr->stats_prov, sizeof(*aggr->stats));
    if (aggr->stats == NULL) goto cleanup;

    aggr->start_observation_window = time(NULL);
    aggr->end_observation_window = aggr->start_observation_window;

    aggr->header_size = get_report_header_size(aggr);

    aggr->report_max_size = GK_HERO_STATS_MAX_REPORT_SIZE;
    if (session && session->ops.get_config)
    {
        hs_max_report_size = session->ops.get_config(session,
                                                     "wc_hero_stats_max_report_size");
        if (hs_max_report_size != NULL)
        {
            max_report_size = strtoul(hs_max_report_size, NULL, 10);
            aggr->report_max_size = (size_t)max_report_size;
        }
    }

    /* Make eventual testing easier */
    aggr->send_report = qm_conn_send_direct;

    aggr->initialized = true;

    LOGD("%s: fully initialized", __func__);

    return true;

cleanup:
    FREE(aggr->windows);
    FREE(aggr->location_id);
    FREE(aggr->node_id);

    /* Reset the structure completely */
    MEMZERO(*aggr);
    aggr->initialized = false;

    LOGD("%s: cannot initialize", __func__);

    return false;
}

void
gkhc_release_aggregator(struct gkc_report_aggregator *aggr)
{
    size_t i;

    if (aggr == NULL || !aggr->initialized) return;

    for (i = 0; i < aggr->windows_idx; i++)
    {
        free_observation_window(aggr->windows[i]);
        FREE(aggr->windows[i]);
    }
    FREE(aggr->windows);

    /* The content of stats gets transferred over to windows, so all
     * that's left to be freed is the array itself.
     */
    FREE(aggr->stats);

    FREE(aggr->location_id);
    FREE(aggr->node_id);

    /* Reset the structure completely */
    MEMZERO(*aggr);
    aggr->initialized = false;
}

void
gkhc_set_records_per_report(struct gkc_report_aggregator *aggr, size_t n)
{
    if (aggr->initialized)
    {
        LOGT("%s(): Cannot change MAX_REPORTS while initialized", __func__);
        return;
    }

    MAX_REPORTS = n;
}

void
gkhc_set_max_record_size(struct gkc_report_aggregator *aggr, size_t n)
{
    if (aggr->initialized)
    {
        LOGT("%s(): Cannot change MAX_REPORTS while initialized", __func__);
        return;
    }

    GK_HERO_STATS_MAX_REPORT_SIZE = n;
}

void
gkhc_set_number_obs_windows(struct gkc_report_aggregator *aggr, size_t n)
{
    if (aggr->initialized)
    {
        LOGT("%s(): Cannot change MAX_WINDOWS while initialized", __func__);
        return;
    }

    MAX_WINDOWS = n;
}

static Gatekeeper__HeroStats__HeroObservationPoint *
serialize_observation_point(struct gkc_report_aggregator *aggr)
{
    Gatekeeper__HeroStats__HeroObservationPoint *pb;

    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    gatekeeper__hero_stats__hero_observation_point__init(pb);

    /* STRDUP() is not needed as the 2 variables come from the global aggr */
    pb->node_id = aggr->node_id;
    pb->location_id = aggr->location_id;

    return pb;
}

static Gatekeeper__HeroStats__HeroReport *
finalize_one_report(struct gkc_report_aggregator *aggr, size_t idx)
{
    Gatekeeper__HeroStats__HeroReport *pb;

    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    gatekeeper__hero_stats__hero_report__init(pb);

    pb->reported_at = time(NULL);
    pb->observation_point = serialize_observation_point(aggr);
    if (pb->observation_point == NULL) goto cleanup_pb;

    /* At this stage we will only support one observation window per message.
     * Even is we are supporting more than one observation window for each reporting
     * cycle, each observation window will be reported using a isolated set of messages.
     */
    pb->n_observation_window = 1;
    pb->observation_window = CALLOC(pb->n_observation_window, sizeof(*pb->observation_window));
    if (pb->observation_window == NULL) goto cleanup_obs_point;

    pb->observation_window[0] = aggr->windows[idx];

    return pb;

cleanup_obs_point:
    FREE(pb->observation_point);
cleanup_pb:
    FREE(pb);

    return NULL;
}

static Gatekeeper__HeroStats__HeroObservationWindow *
get_blank_observation_window(struct gkc_report_aggregator *aggr)
{
    Gatekeeper__HeroStats__HeroObservationWindow *obs_window;

    obs_window = CALLOC(1, sizeof(*obs_window));
    if (obs_window == NULL) return NULL;

    gatekeeper__hero_stats__hero_observation_window__init(obs_window);

    obs_window->started_at = aggr->start_observation_window;
    obs_window->ended_at = aggr->end_observation_window;

    /* We over-provision the array of pointers. At most we'll have
     * ALL the current records be inserted in the observation window. */
    obs_window->hero_stats = CALLOC(aggr->stats_prov, sizeof(*obs_window->hero_stats));
    if (obs_window->hero_stats == NULL) goto cleanup_obs_window;
    obs_window->n_hero_stats = 0;

    return obs_window;

cleanup_obs_window:
    FREE(obs_window);
    return NULL;
}

static void
expand_obs_window_array(struct gkc_report_aggregator *aggr)
{
    Gatekeeper__HeroStats__HeroObservationWindow **new_obs_windows;
    size_t new_size;

    /* This should really never happen */
    if (aggr->windows_idx < aggr->windows_prov) return;

    new_size = aggr->windows_prov + aggr->windows_max;
    new_obs_windows = REALLOC(aggr->windows, new_size * sizeof(*new_obs_windows));
    if (!new_obs_windows)
    {
        LOGD("%s(): cannot realloc for %zu entries", __func__, new_size);
        return;
    }

    aggr->windows = new_obs_windows;
    aggr->windows_prov = new_size;

    LOGT("%s(): New windows provisioned for %zu entries", __func__, aggr->windows_prov);
}

static void
free_observation_window(Gatekeeper__HeroStats__HeroObservationWindow *window)
{
    size_t i;

    for (i = 0; i < window->n_hero_stats; i++)
    {
        free_stats(window->hero_stats[i]);
        FREE(window->hero_stats[i]);
    }
    FREE(window->hero_stats);
}

/**
 * @brief Pre-compute the serialized report's header size. Data needs to be populated since
 * protobuf will remove 'zero-ed' entries.
 *
 * @param aggr a pointer to the initialized global aggregator (we need some of the values)
 * @return the size in bytes of the reports' header (cannot change during one execution)
 *
 * @remark Should only be executed once.
 */
static size_t
get_report_header_size(struct gkc_report_aggregator *aggr)
{
    Gatekeeper__HeroStats__HeroObservationWindow *new_window;
    Gatekeeper__HeroStats__HeroReport pb;
    ssize_t s;

    new_window = get_blank_observation_window(aggr);
    if (new_window == NULL) return 0;

    gatekeeper__hero_stats__hero_report__init(&pb);
    pb.reported_at = time(NULL);
    pb.observation_point = serialize_observation_point(aggr);
    pb.n_observation_window = 1;
    pb.observation_window = CALLOC(pb.n_observation_window, sizeof(*pb.observation_window));
    pb.observation_window[0] = new_window;

    s = gatekeeper__hero_stats__hero_report__get_packed_size(&pb);

    FREE(pb.observation_point);
    FREE(pb.observation_window);
    free_observation_window(new_window);
    FREE(new_window);

    return s;
}

static void
finalize_observation_window(struct gkc_report_aggregator *aggr)
{
    Gatekeeper__HeroStats__HeroObservationWindow *new_window;
    size_t current_report_size;
    size_t record_size;
    size_t i;

    /* aggr not NULL by construction */
    if (!aggr->initialized || (aggr->stats_idx == 0)) return;

    /* We'll keep a running count of the anticipated report size,
     * starting with the header, then adding just enough records to the
     * observation window.
     */
    current_report_size = aggr->header_size;

    new_window = get_blank_observation_window(aggr);
    if (new_window == NULL) return;

    for (i = 0; i < aggr->stats_idx; i++)
    {
        /* We need to check if this will push report size ovr the limit */
        record_size =
            gatekeeper__hero_stats__hero_stats__get_packed_size(aggr->stats[i]);

        /* Report will be too big with this window, so stash the window,
         * and start a new one
         */
        if ((current_report_size + record_size + 2) > aggr->report_max_size)
        {
            aggr->windows[aggr->windows_idx] = new_window;
            aggr->windows_idx++;

            if (aggr->windows_idx >= aggr->windows_prov)
            {
                expand_obs_window_array(aggr);
            }

            new_window = get_blank_observation_window(aggr);
            if (new_window == NULL) return;

            current_report_size = aggr->header_size;
        }

        new_window->hero_stats[new_window->n_hero_stats] = aggr->stats[i];
        new_window->n_hero_stats++;

        /* Assign new report size */
        current_report_size += (record_size + 2);
    }

    /* If we have any records in the observation window, we need to close it */
    if (new_window->n_hero_stats != 0)
    {
        aggr->windows[aggr->windows_idx] = new_window;
        aggr->windows_idx++;

        if (aggr->windows_idx >= aggr->windows_prov)
        {
            expand_obs_window_array(aggr);
        }
    }
    else
    {
        /* While this was unused, don't forget to free it */
        free_observation_window(new_window);
        FREE(new_window);
    }

    /* We have transferred ownership of all stats to observation windows,
     * so clean up the stats array now, and resize to the basic size.
     */
    FREE(aggr->stats);
    aggr->stats_idx = 0;
    aggr->stats_prov = aggr->stats_max;
    aggr->stats = CALLOC(report_aggr.stats_prov, sizeof(*report_aggr.stats));
    if (aggr->stats == NULL) gkhc_release_aggregator(aggr);
}

/**
 * @brief Create a blank hero stats record for an attribute
 */
static Gatekeeper__HeroStats__HeroStats *
get_blank_hero_stats_attr(os_macaddr_t *device_id, struct attr_cache *entry)
{
    Gatekeeper__HeroStats__HeroStats *pb;

    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;
    gatekeeper__hero_stats__hero_stats__init(pb);

    pb->device_id.data = device_id->addr;
    pb->device_id.len = sizeof(device_id->addr);
    pb->action = get_protobuf_action_value(entry->action);
    pb->direction = get_protobuf_direction_value(entry->direction);
    pb->category_id = entry->category_id;
    if (!IS_NULL_PTR(entry->gk_policy))
    {
        pb->policy = STRDUP(entry->gk_policy);
    }
    else
    {
        pb->policy = STRDUP("Not GK policy");
    }
    pb->last_access_ts = entry->cache_ts;
    if (!IS_NULL_PTR(entry->network_id))
    {
        pb->network_zone = STRDUP(entry->network_id);
    }
    else
    {
        pb->network_zone = STRDUP("unknown");
    }

    return pb;
}

/**
 * @brief Create a blank hero stats record for a flow
 */
static Gatekeeper__HeroStats__HeroStats *
get_blank_hero_stats_flow(os_macaddr_t *device_id, struct ip_flow_cache *entry)
{
    Gatekeeper__HeroStats__HeroStats *pb;

    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;
    gatekeeper__hero_stats__hero_stats__init(pb);

    pb->device_id.data = device_id->addr;
    pb->device_id.len = sizeof(device_id->addr);
    pb->action = get_protobuf_action_value(entry->action);
    pb->direction = get_protobuf_direction_value(entry->direction);
    pb->category_id = entry->category_id;
    if (entry->gk_policy)
    {
        pb->policy = STRDUP(entry->gk_policy);
    }
    else
    {
        pb->policy = STRDUP("Not GK policy");
    }
    pb->last_access_ts = entry->cache_ts;
    if (entry->network_id)
    {
        pb->network_zone = STRDUP(entry->network_id);
    }
    else
    {
        pb->network_zone = STRDUP("unknown");
    }

    return pb;
}

static void
expand_hero_stats_array(struct gkc_report_aggregator *aggr)
{
    Gatekeeper__HeroStats__HeroStats **new_stats;
    size_t new_size;

    /* This should really never happen */
    if (aggr->stats_idx < aggr->stats_prov) return;

    new_size = aggr->stats_prov + aggr->stats_max;
    new_stats = REALLOC(aggr->stats, new_size * sizeof(*new_stats));
    if (new_stats == NULL)
    {
        LOGD("%s(): cannot realloc for %zu entries", __func__, new_size);
        return;
    }

    aggr->stats = new_stats;
    aggr->stats_prov = new_size;

    LOGT("%s(): New stats provisioned for %zu entries", __func__, aggr->stats_prov);
}

static void
free_stats(Gatekeeper__HeroStats__HeroStats *pb)
{
    if (pb == NULL) return;

    FREE(pb->policy);
    FREE(pb->network_zone);
    if (pb->redirect != NULL) FREE(pb->redirect->ipv6.data);
    FREE(pb->redirect);
    FREE(pb->ipv4);
    FREE(pb->ipv4_tuple);
    FREE(pb->ipv6);
    FREE(pb->ipv6_tuple);
    FREE(pb->hostname);
    FREE(pb->url);
    FREE(pb->app);
}

static uint64_t
get_relative_count(struct counter_s *cnt)
{
    uint64_t count;

    count = cnt->total - cnt->previous;
    cnt->previous = cnt->total;

    return count;
}

static void
serialize_hostname_tree(ds_tree_t *tree, os_macaddr_t *device_id, struct gkc_report_aggregator *aggr)
{
    Gatekeeper__HeroStats__HeroRedirect *new_redirect;
    Gatekeeper__HeroStats__HeroStats *new_pb;
    Gatekeeper__HeroStats__HeroStats **pb;
    struct fqdn_redirect_s *fqdn_redirect;
    struct attr_hostname_s *attr;
    struct attr_cache *entry;
    uint64_t count_fqdn;
    uint64_t count_host;
    struct in6_addr in6;
    struct in_addr in4;
    uint64_t count_sni;
    uint64_t rel_total;
    bool sr_cname;
    int rc;

    ds_tree_foreach(tree, entry)
    {
        attr = entry->attr.host_name;
        count_fqdn = get_relative_count(&attr->count_fqdn);
        count_host = get_relative_count(&attr->count_host);
        count_sni  = get_relative_count(&attr->count_sni);
        rel_total = count_fqdn + count_host + count_sni;

        /* This entry has not changed since last window */
        if (rel_total == 0) continue;

        /* Need to keep this assignment in the loop as we might REALLOC */
        pb = aggr->stats;

        new_pb = get_blank_hero_stats_attr(device_id, entry);
        if (new_pb == NULL) continue;

        new_pb->hostname = CALLOC(1, sizeof(*new_pb->hostname));
        if (new_pb->hostname == NULL)
        {
            free_stats(new_pb);
            FREE(new_pb);
            continue;
        }
        gatekeeper__hero_stats__hero_hostname__init(new_pb->hostname);

        new_pb->hostname->name       = attr->name;
        new_pb->hostname->count_fqdn = count_fqdn;
        new_pb->hostname->count_host = count_host;
        new_pb->hostname->count_sni  = count_sni;
        fqdn_redirect = entry->fqdn_redirect;

        if (!IS_NULL_PTR(fqdn_redirect) && (fqdn_redirect->redirect))
        {
            new_pb->redirect = CALLOC(1, sizeof(*new_pb->redirect));
            if (new_pb->redirect == NULL) continue;

            new_redirect = new_pb->redirect;
            gatekeeper__hero_stats__hero_redirect__init(new_redirect);
            sr_cname = (fqdn_redirect->redirect_cname != NULL);
            if (sr_cname) sr_cname &= (strlen(fqdn_redirect->redirect_cname) >= 3);
            if (sr_cname) new_redirect->cname = &fqdn_redirect->redirect_cname[2];

            rc = inet_pton(AF_INET, &fqdn_redirect->redirect_ips[0][2], &in4);
            if (rc == 1)
            {
                new_redirect->ipv4 = htonl(in4.s_addr);
            }

            rc = inet_pton(AF_INET6, &fqdn_redirect->redirect_ips[1][3], &in6);
            if (rc == 1)
            {
                new_redirect->ipv6.data = CALLOC(1, 16);
                MEM_CPY(new_redirect->ipv6.data, (uint8_t *)&(in6.s6_addr), 16);
                new_redirect->ipv6.len  = 16;
            }
        }

        pb[aggr->stats_idx] = new_pb;
        aggr->stats_idx++;

        if (aggr->stats_idx >= aggr->stats_prov) expand_hero_stats_array(aggr);
    }
}

static void
serialize_url_tree(ds_tree_t *tree, os_macaddr_t *device_id, struct gkc_report_aggregator *aggr)
{
    Gatekeeper__HeroStats__HeroStats *new_pb;
    Gatekeeper__HeroStats__HeroStats **pb;
    struct attr_generic_s *attr;
    struct attr_cache *entry;
    uint64_t rel_count;

    ds_tree_foreach(tree, entry)
    {
        attr = entry->attr.url;
        rel_count = get_relative_count(&attr->hit_count);

        /* This entry has not changed since last window */
        if (rel_count == 0) continue;

        /* Need to keep this assignment in the loop as we might REALLOC */
        pb = aggr->stats;

        new_pb = get_blank_hero_stats_attr(device_id, entry);
        if (new_pb == NULL) continue;

        new_pb->url = CALLOC(1, sizeof(*new_pb->url));
        if (new_pb->url == NULL)
        {
            free_stats(new_pb);
            FREE(new_pb);
            continue;
        }
        gatekeeper__hero_stats__hero_url__init(new_pb->url);

        new_pb->url->url = attr->name;
        new_pb->url->count = rel_count;

        pb[aggr->stats_idx] = new_pb;
        aggr->stats_idx++;

        if (aggr->stats_idx >= aggr->stats_prov) expand_hero_stats_array(aggr);
    }
}

static void
serialize_ipv4_tree(ds_tree_t *tree, os_macaddr_t *device_id, struct gkc_report_aggregator *aggr)
{
    Gatekeeper__HeroStats__HeroStats *new_pb;
    Gatekeeper__HeroStats__HeroStats **pb;
    struct attr_ip_addr_s *attr;
    struct attr_cache *entry;
    struct sockaddr_in *in;
    uint64_t rel_count;

    ds_tree_foreach(tree, entry)
    {
        attr = entry->attr.ipv4;
        rel_count = get_relative_count(&attr->hit_count);

        /* This entry has not changed since last window */
        if (rel_count == 0) continue;

        /* Need to keep this assignment in the loop as we might REALLOC */
        pb = aggr->stats;

        new_pb = get_blank_hero_stats_attr(device_id, entry);
        if (new_pb == NULL) continue;

        new_pb->ipv4 = CALLOC(1, sizeof(*new_pb->ipv4));
        if (new_pb->ipv4 == NULL)
        {
            free_stats(new_pb);
            FREE(new_pb);
            continue;
        }

        if (entry->action == FSM_ACTION_NONE)
        {
            new_pb->action = get_protobuf_action_value(attr->action_by_name);
        }

        gatekeeper__hero_stats__hero_ipv4__init(new_pb->ipv4);

        /*
         * Since this is a cast, s_addr will turn into host-order.
         * Swap back to network-order.
         */
        in = (struct sockaddr_in *)&(attr->ip_addr);
        new_pb->ipv4->addr_ipv4 = htonl(in->sin_addr.s_addr);
        new_pb->ipv4->count = rel_count;

        pb[aggr->stats_idx] = new_pb;
        aggr->stats_idx++;

        if (aggr->stats_idx >= aggr->stats_prov) expand_hero_stats_array(aggr);
    }
}

static void
serialize_ipv6_tree(ds_tree_t *tree, os_macaddr_t *device_id, struct gkc_report_aggregator *aggr)
{
    Gatekeeper__HeroStats__HeroStats *new_pb;
    Gatekeeper__HeroStats__HeroStats **pb;
    struct attr_ip_addr_s *attr;
    struct sockaddr_in6 *in6;
    struct attr_cache *entry;
    uint64_t rel_count;

    ds_tree_foreach(tree, entry)
    {
        attr = entry->attr.ipv6;
        rel_count = get_relative_count(&attr->hit_count);

        /* This entry has not changed since last window */
        if (rel_count == 0) continue;

        /* Need to keep this assignment in the loop as we might REALLOC */
        pb = aggr->stats;

        new_pb = get_blank_hero_stats_attr(device_id, entry);
        if (new_pb == NULL) continue;

        new_pb->ipv6 = CALLOC(1, sizeof(*new_pb->ipv6));
        if (new_pb->ipv6 == NULL)
        {
            free_stats(new_pb);
            FREE(new_pb);
            continue;
        }

        if (entry->action == FSM_ACTION_NONE)
        {
            new_pb->action = get_protobuf_action_value(attr->action_by_name);
        }

        gatekeeper__hero_stats__hero_ipv6__init(new_pb->ipv6);

        in6 = (struct sockaddr_in6 *)&(attr->ip_addr);
        new_pb->ipv6->addr_ipv6.data = (uint8_t *)&(in6->sin6_addr);
        new_pb->ipv6->addr_ipv6.len  = 16;
        new_pb->ipv6->count = rel_count;

        pb[aggr->stats_idx] = new_pb;
        aggr->stats_idx++;

        if (aggr->stats_idx >= aggr->stats_prov) expand_hero_stats_array(aggr);
    }
}

static void
serialize_app_tree(ds_tree_t *tree, os_macaddr_t *device_id, struct gkc_report_aggregator *aggr)
{
    Gatekeeper__HeroStats__HeroStats *new_pb;
    Gatekeeper__HeroStats__HeroStats **pb;
    struct attr_generic_s *attr;
    struct attr_cache *entry;
    uint64_t rel_count;

    ds_tree_foreach(tree, entry)
    {
        attr = entry->attr.url;
        rel_count = get_relative_count(&attr->hit_count);

        /* This entry has not changed since last window */
        if (rel_count == 0) continue;

        /* Need to keep this assignment in the loop as we might REALLOC */
        pb = aggr->stats;

        new_pb = get_blank_hero_stats_attr(device_id, entry);
        if (new_pb == NULL) continue;

        new_pb->app = CALLOC(1, sizeof(*new_pb->app));
        if (new_pb->app == NULL)
        {
            free_stats(new_pb);
            FREE(new_pb);
            continue;
        }
        gatekeeper__hero_stats__hero_app__init(new_pb->app);

        new_pb->app->name = attr->name;
        new_pb->app->count = rel_count;

        pb[aggr->stats_idx] = new_pb;
        aggr->stats_idx++;

        if (aggr->stats_idx >= aggr->stats_prov) expand_hero_stats_array(aggr);
    }
}

static void
serialize_flow_tree(ds_tree_t *tree, os_macaddr_t *device_id, struct gkc_report_aggregator *aggr)
{
    Gatekeeper__HeroStats__HeroIpv4FlowTuple *new_ipv4_tuple;
    Gatekeeper__HeroStats__HeroIpv6FlowTuple *new_ipv6_tuple;
    Gatekeeper__HeroStats__HeroStats *new_pb;
    Gatekeeper__HeroStats__HeroStats **pb;
    struct ip_flow_cache *entry;
    uint64_t rel_count;

    ds_tree_foreach(tree, entry)
    {
        rel_count = get_relative_count(&entry->hit_count);

        /* This entry has not changed since last window */
        if (rel_count == 0) continue;

        /* Need to keep this assignment in the loop as we might REALLOC */
        pb = aggr->stats;

        new_pb = get_blank_hero_stats_flow(device_id, entry);
        if (new_pb == NULL) continue;

        if (entry->ip_version == 4)
        {
            new_pb->ipv4_tuple = CALLOC(1, sizeof(*new_pb->ipv4_tuple));
            if (new_pb->ipv4_tuple == NULL)
            {
                free_stats(new_pb);
                FREE(new_pb);
                continue;
            }

            new_ipv4_tuple = new_pb->ipv4_tuple;
            gatekeeper__hero_stats__hero_ipv4_flow_tuple__init(new_ipv4_tuple);

            /*
             * we are casting network_order uint8_t* into host_order uint32_t.
             * Ensure bytes are swapped back properly.
             */
            new_ipv4_tuple->source_ipv4 = htonl(*((uint32_t *)entry->src_ip_addr));
            new_ipv4_tuple->destination_ipv4 = htonl(*((uint32_t *)entry->dst_ip_addr));
            new_ipv4_tuple->transport = entry->protocol;
            new_ipv4_tuple->source_port = entry->src_port;
            new_ipv4_tuple->destination_port = entry->dst_port;
            new_ipv4_tuple->count = rel_count;
        }
        else if (entry->ip_version == 6)
        {
            new_pb->ipv6_tuple = CALLOC(1, sizeof(*new_pb->ipv6_tuple));
            if (new_pb->ipv6_tuple == NULL)
            {
                free_stats(new_pb);
                FREE(new_pb);
                continue;
            }

            new_ipv6_tuple = new_pb->ipv6_tuple;
            gatekeeper__hero_stats__hero_ipv6_flow_tuple__init(new_ipv6_tuple);

            new_ipv6_tuple->source_ipv6.data = entry->src_ip_addr;
            new_ipv6_tuple->source_ipv6.len = sizeof(struct in6_addr);
            new_ipv6_tuple->destination_ipv6.data = entry->dst_ip_addr;
            new_ipv6_tuple->destination_ipv6.len = sizeof(struct in6_addr);
            new_ipv6_tuple->transport = entry->protocol;
            new_ipv6_tuple->source_port = entry->src_port;
            new_ipv6_tuple->destination_port = entry->dst_port;
            new_ipv6_tuple->count = rel_count;
        }
        else
        {
            LOGD("%s(): wrong IP format = %u", __func__, entry->ip_version);
            free_stats(new_pb);
            FREE(new_pb);
            continue;
        }

        pb[aggr->stats_idx] = new_pb;
        aggr->stats_idx++;
        if (aggr->stats_idx >= aggr->stats_prov) expand_hero_stats_array(aggr);
    }
}

/**
 * @brief Capture all the serialized cache reports for a given observation window
 *
 * Upon returning, the aggr->stats array will contain protobufs for each
 * cache entry.
 */
bool
gkhc_serialize_cache_entries(struct gkc_report_aggregator *aggr)
{
    struct per_device_cache *entry;
    struct gk_cache_mgr *mgr;
    ds_tree_t *tree;

    if (aggr == NULL || aggr->initialized == false) return false;

    /* Fetch the cache */
    mgr = gk_cache_get_mgr();
    if (!mgr->initialized) return false;

    /* go over every device in the tree */
    tree = &mgr->per_device_tree;

    ds_tree_foreach(tree, entry)
    {
        serialize_hostname_tree(&entry->hostname_tree, entry->device_mac, aggr);
        serialize_url_tree(&entry->url_tree, entry->device_mac, aggr);
        serialize_ipv4_tree(&entry->ipv4_tree, entry->device_mac, aggr);
        serialize_ipv6_tree(&entry->ipv6_tree, entry->device_mac, aggr);
        serialize_app_tree(&entry->app_tree, entry->device_mac, aggr);
        serialize_flow_tree(&entry->inbound_tree, entry->device_mac, aggr);
        serialize_flow_tree(&entry->outbound_tree, entry->device_mac, aggr);
    }

    return true;
}

/**
 * @copydoc gkhc_activate_window
 */
void
gkhc_activate_window(struct gkc_report_aggregator *aggr)
{
    time_t now;
    char time_str[TIME_STR_SZ];

    if (aggr == NULL || aggr->initialized == false) return;

    now = time(NULL);
    time_to_string(now, time_str, TIME_STR_SZ);
    LOGT("%s: Activating window at %s", __func__, time_str);
    aggr->start_observation_window = now;
}

/**
 * @copydoc gkhc_close_window
 */
void
gkhc_close_window(struct gkc_report_aggregator *aggr)
{
    time_t now;
    bool rc;
    char time_str[TIME_STR_SZ];

    if (aggr == NULL || aggr->initialized == false) return;

    now = time(NULL);
    aggr->end_observation_window = now;
    time_to_string(now, time_str, TIME_STR_SZ);
    LOGT("%s: Closing window at %s", __func__, time_str);

    rc = gkhc_serialize_cache_entries(aggr);
    if (!rc) return;

    /* Build a new observation window */
    finalize_observation_window(aggr);

    /* We just closed one window */
    aggr->num_observation_windows++;
}

static void
free_hero_stats_report_pb(Gatekeeper__HeroStats__HeroReport *pb)
{
    size_t i;

    for (i = 0; i < pb->n_observation_window; i++)
    {
        free_observation_window(pb->observation_window[i]);
    }
    FREE(pb->observation_window);
    FREE(pb->observation_point);
}

/**
 * @copydoc gkhc_build_and_send_report()
 *
 * @details The observation window must have been closed prior to create
 * the serialized report. Each "window" will be stored individually in the
 * windows[] array, and a report will be created for each of them.
 * Once every window has been sent back, we can reset our local aggregator.
 */
int
gkhc_build_and_send_report(struct gkc_report_aggregator *aggr, char *mqtt_topic)
{
    Gatekeeper__HeroStats__HeroReport *pb;
    struct gk_packed_buffer serialized_pb;
    const size_t buf_len = 4096;
    uint8_t pre_alloc_buf[buf_len]; /* make sure we don't have to allocate
                                       in most cases */
    size_t num_sent_reports = 0;
    qm_response_t res;
    uint8_t *buf;
    size_t len;
    size_t i;
    bool ret;
    char time_str[TIME_STR_SZ];

    if (aggr == NULL || aggr->initialized == false) return -1;

    if (mqtt_topic == NULL || strlen(mqtt_topic) == 0)
    {
        LOGD("%s(): MQTT topic is undefined. Skipping report.", __func__);

        /* Clean the windows created during gkhc_close_window */
        for (i = 0; i < aggr->windows_idx; i++)
        {
            free_observation_window(aggr->windows[i]);
            FREE(aggr->windows[i]);
        }

        num_sent_reports = -1;
        goto reset_windows;
    }

    /* the observation window was not closed !! */
    if (aggr->start_observation_window > aggr->end_observation_window)
    {
        time_to_string(aggr->start_observation_window, time_str, TIME_STR_SZ);
        LOGT("%s(): obs window not closed after active at %s", __func__, time_str);
        return -1;
    }

    for (i = 0; i < aggr->windows_idx; i++)
    {
        pb = finalize_one_report(aggr, i);

        if (pb == NULL)
        {
            free_observation_window(aggr->windows[i]);
            FREE(aggr->windows[i]);
            continue;
        }

        /* Get serialization length */
        len = gatekeeper__hero_stats__hero_report__get_packed_size(pb);
        if (len == 0) goto cleanup_serialized_buf;

        /* Allocate more space if needed */
        if (len > buf_len)
        {
            buf = MALLOC(len * sizeof(*buf));
            if (buf == NULL) goto cleanup_serialized_buf;
        }
        else
        {
            buf = pre_alloc_buf;
        }

        /* Populate serialization output structure */
        serialized_pb.len = gatekeeper__hero_stats__hero_report__pack(pb, buf);
        serialized_pb.buf = buf;

        /* This is qm_conn_send_direct() unless overwritten in UT */
        ret = aggr->send_report(QM_REQ_COMPRESS_IF_CFG, mqtt_topic,
                                serialized_pb.buf, serialized_pb.len, &res);

        /* Cleanup buf _only_ if allocated */
        if (buf != pre_alloc_buf) FREE(buf);

        if (ret)
        {
            num_sent_reports++;
        }
        else
        {
            LOGD("%s(): Can't send the report[%zu] %s", __func__, num_sent_reports, qm_error_str(res.error));
        }

        /* cleanup locally allocated sent serialized protobuf */
        free_hero_stats_report_pb(pb);
        FREE(pb);

        /* Free the obs windows as we go along */
        FREE(aggr->windows[i]);
    }

    /* Need to reset the aggr as we have emptied it! */
reset_windows:
    FREE(aggr->windows);
    aggr->windows_idx = 0;
    aggr->windows_prov = aggr->windows_max;
    aggr->windows = CALLOC(aggr->windows_prov, sizeof(*aggr->windows));
    if (aggr->windows == NULL)
    {
        gkhc_release_aggregator(aggr);
        return -1;
    }

    return num_sent_reports;

cleanup_serialized_buf:
    free_hero_stats_report_pb(pb);
    FREE(pb);

    return -1;
}

int
gkhc_send_report(struct fsm_session *session, long interval)
{
    struct fsm_gk_session *fsm_gk_session;
    time_t cmp_report;
    bool get_stats;
    int retval;
    time_t now;

    retval = 0;

    if (session == NULL) return retval;

    fsm_gk_session = (struct fsm_gk_session *)session->handler_ctxt;
    if (!fsm_gk_session) return retval;

    now = time(NULL);

    cmp_report = now - fsm_gk_session->hero_stats_report_ts;
    get_stats = (cmp_report >= interval);
    if (!get_stats)
        return retval;

    LOGT("%s: Reporting HERO stats", __func__);
    gkhc_close_window(fsm_gk_session->hero_stats);

    /* Report to mqtt */
    retval = gkhc_build_and_send_report(fsm_gk_session->hero_stats,
                                        fsm_gk_session->hero_stats_report_topic);

    if (retval > 0)
    {
        LOGI("%s: HERO stats sent in %d reports after %ld seconds", __func__, retval, (long int)cmp_report);
    }
    else
    {
        LOGD("%s: HERO stats didn't generate any report after %ld seconds", __func__, (long int)cmp_report);
    }

    /* Set up the start of next observation window */
    fsm_gk_session->hero_stats_report_ts = now;
    gkhc_activate_window(fsm_gk_session->hero_stats);

    return retval;
}
