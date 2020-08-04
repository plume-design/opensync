#include <ev.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "dppline.h"
#include "ds_list.h"
#include "log.h"
#include "stats_pub.h"
#include "target.h"

#include "sm_stats_pub.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

#define DATA_EXPIRE_SEC 60

typedef struct sm_stats_pub_survey
{
    ds_dlist_node_t next;

    char radio_name[32];
    int channel;
    time_t ts;
    stats_pub_survey_t stats;
} sm_stats_pub_survey_t;

static ds_dlist_t g_radio_stats_list;
static struct ev_timer g_timer_cleanup;

static void sm_stats_pub_survey_cleanup(struct ev_loop *loop, ev_timer *timer, int revents)
{
    sm_stats_pub_survey_t *entry;
    ds_dlist_iter_t iter;
    void *item;
    time_t now;

    if (ds_dlist_is_empty(&g_radio_stats_list)) {
        return;
    }

    now = time(NULL);
    for (   item = ds_dlist_ifirst(&iter, &g_radio_stats_list);
            item != NULL;
            item = ds_dlist_inext(&iter))
    {
        entry = (sm_stats_pub_survey_t *)item;
        if ((now - entry->ts) < DATA_EXPIRE_SEC) {
            continue;
        }

        ds_dlist_iremove(&iter);
        free(entry);
    }
}

static sm_stats_pub_survey_t* sm_stats_pub_survey_entry_get(char *radio_name, int channel)
{
    sm_stats_pub_survey_t *entry;
    ds_dlist_iter_t iter;
    void *item;

    for (   item = ds_dlist_ifirst(&iter, &g_radio_stats_list);
            item != NULL;
            item = ds_dlist_inext(&iter))
    {
        entry = (sm_stats_pub_survey_t *)item;
        if ((entry->channel == channel) && (strcmp(entry->radio_name, radio_name) == 0)) {
            return entry;
        }
    }

    return NULL;
}

static void sm_stats_pub_survey_record_copy(
        stats_pub_survey_t *stats,
        char *if_name,
        dpp_survey_record_t *rec)
{
    stats->timestamp = rec->info.timestamp_ms / 1000;
    stats->busy = rec->chan_busy;
    stats->busy_tx = rec->chan_tx;
    stats->busy_rx = rec->chan_rx;
    stats->busy_self = rec->chan_self;
    stats->busy_ext = rec->chan_busy_ext;

    if (!target_stats_survey_noise_floor_get(if_name, &stats->noise_floor)) {
        LOGE("%s: Failed to call target API to get Noise Floor", __func__);
    }
}

static int sm_stats_pub_survey_add(
        char *radio_name,
        char *if_name,
        int channel,
        dpp_survey_record_t *rec)
{
    sm_stats_pub_survey_t *entry;

    entry = calloc(1, sizeof(*entry));
    if (entry == NULL) {
        LOGE("%s: Failed to allocate memory", __func__);
        return -1;
    }

    STRSCPY(entry->radio_name, radio_name);
    entry->channel = channel;
    entry->ts = time(NULL);
    sm_stats_pub_survey_record_copy(&entry->stats, if_name, rec);

    ds_dlist_insert_tail(&g_radio_stats_list, entry);
    return 0;
}

int sm_stats_pub_survey_update(char *phy_name, char *if_name, int channel, dpp_survey_record_t *rec)
{
    sm_stats_pub_survey_t *entry;

    if ((phy_name == NULL) || (if_name == NULL) || (channel == 0) || (rec == NULL)) {
        LOGW("%s: Some function arguments are empty. Skipping the request", __func__);
        return -1;
    }

    LOGD("%s: Chan[%d] Active[%u] Busy[%u] Busy_ext[%u] Self[%u] Rx[%u] Tx[%u] Duration[%u]ms",
         __func__, channel, rec->chan_active, rec->chan_busy, rec->chan_busy_ext, rec->chan_self,
         rec->chan_rx, rec->chan_tx, rec->duration_ms);

    entry = sm_stats_pub_survey_entry_get(phy_name, channel);
    if (entry != NULL)
    {
        entry->ts = time(NULL);
        sm_stats_pub_survey_record_copy(&entry->stats, if_name, rec);
        return 0;
    }

    if (sm_stats_pub_survey_add(phy_name, if_name, channel, rec) != 0) {
        return -1;
    }

    return 0;
}

stats_pub_survey_t* sm_stats_pub_survey_get(char *phy_name, int channel)
{
    sm_stats_pub_survey_t *entry;

    entry = sm_stats_pub_survey_entry_get(phy_name, channel);
    if (entry == NULL) {
        LOGW("%s: phy_name[%s] channel[%d] stats are not found", __func__, phy_name, channel);
        return NULL;
    }

    return &entry->stats;
}

int sm_stats_pub_survey_init(void)
{
    ds_dlist_init(&g_radio_stats_list, sm_stats_pub_survey_t, next);
    ev_timer_init(&g_timer_cleanup, sm_stats_pub_survey_cleanup, DATA_EXPIRE_SEC, DATA_EXPIRE_SEC);

    return 0;
}

void sm_stats_pub_survey_uninit(void)
{
    ds_dlist_iter_t iter;
    void *item;

    for (   item = ds_dlist_ifirst(&iter, &g_radio_stats_list);
            item != NULL;
            item = ds_dlist_inext(&iter))
    {
        ds_dlist_iremove(&iter);
        free(item);
    }

    ev_timer_stop(EV_DEFAULT, &g_timer_cleanup);
}
