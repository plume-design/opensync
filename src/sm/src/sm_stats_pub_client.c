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

#include "sm_stats_pub.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

#define DATA_EXPIRE_SEC 60

typedef struct sm_stats_pub_client
{
    ds_dlist_node_t next;

    mac_address_t mac;
    time_t ts;
    stats_pub_client_t stats;
} sm_stats_pub_client_t;

static ds_dlist_t g_client_stats_list;
static struct ev_timer g_timer_cleanup;

static void sm_stats_pub_client_cleanup(struct ev_loop *loop, ev_timer *timer, int revents)
{
    sm_stats_pub_client_t *entry;
    ds_dlist_iter_t iter;
    void *item;
    time_t now;

    if (ds_dlist_is_empty(&g_client_stats_list)) {
        return;
    }

    now = time(NULL);
    for (   item = ds_dlist_ifirst(&iter, &g_client_stats_list);
            item != NULL;
            item = ds_dlist_inext(&iter))
    {
        entry = (sm_stats_pub_client_t *)item;
        if ((now - entry->ts) < DATA_EXPIRE_SEC) {
            continue;
        }

        ds_dlist_iremove(&iter);
        free(entry);
    }
}

static sm_stats_pub_client_t* sm_stats_pub_client_entry_get(mac_address_t mac)
{
    sm_stats_pub_client_t *entry;
    ds_dlist_iter_t iter;
    void *item;

    for (   item = ds_dlist_ifirst(&iter, &g_client_stats_list);
            item != NULL;
            item = ds_dlist_inext(&iter))
    {
        entry = (sm_stats_pub_client_t *)item;
        if (memcmp(entry->mac, mac, sizeof(entry->mac)) == 0) {
            return entry;
        }
    }

    return NULL;
}

static void sm_stats_pub_client_record_copy(stats_pub_client_t *stats, dpp_client_stats_t *rec)
{
    stats->timestamp = time(NULL);
    memcpy(&stats->rec, rec, sizeof(*rec));
}

static int sm_stats_pub_client_add(mac_address_t mac, dpp_client_stats_t *rec)
{
    sm_stats_pub_client_t *entry;

    entry = calloc(1, sizeof(*entry));
    if (entry == NULL) {
        LOGE("%s: Failed to allocate memory", __func__);
        return -1;
    }

    memcpy(entry->mac, mac, sizeof(entry->mac));
    entry->ts = time(NULL);
    sm_stats_pub_client_record_copy(&entry->stats, rec);

    ds_dlist_insert_tail(&g_client_stats_list, entry);
    return 0;
}

int sm_stats_pub_client_update(mac_address_t mac, dpp_client_stats_t *rec)
{
    sm_stats_pub_client_t *entry;

    if (rec == NULL) {
        LOGW("%s: rec argument is empty. Skipping the request", __func__);
        return -1;
    }

    LOGD("%s: Update  ["MAC_ADDRESS_FORMAT"] stats: RxRate[%f] TxRate[%f] RSSI[%d]", __func__,
         MAC_ADDRESS_PRINT(mac), rec->rate_rx, rec->rate_tx, rec->rssi);

    entry = sm_stats_pub_client_entry_get(mac);
    if (entry != NULL)
    {
        entry->ts = time(NULL);
        sm_stats_pub_client_record_copy(&entry->stats, rec);
        return 0;
    }

    if (sm_stats_pub_client_add(mac, rec) != 0) {
        return -1;
    }

    return 0;
}

stats_pub_client_t* sm_stats_pub_client_get(mac_address_t mac)
{
    sm_stats_pub_client_t *entry;

    entry = sm_stats_pub_client_entry_get(mac);
    if (entry == NULL) {
        LOGW("%s: stats are not found", __func__);
        return NULL;
    }

    return &entry->stats;
}

int sm_stats_pub_client_init(void)
{
    ds_dlist_init(&g_client_stats_list, sm_stats_pub_client_t, next);
    ev_timer_init(&g_timer_cleanup, sm_stats_pub_client_cleanup, DATA_EXPIRE_SEC, DATA_EXPIRE_SEC);

    return 0;
}

void sm_stats_pub_client_uninit(void)
{
    ds_dlist_iter_t iter;
    void *item;

    for (   item = ds_dlist_ifirst(&iter, &g_client_stats_list);
            item != NULL;
            item = ds_dlist_inext(&iter))
    {
        ds_dlist_iremove(&iter);
        free(item);
    }

    ev_timer_stop(EV_DEFAULT, &g_timer_cleanup);
}
