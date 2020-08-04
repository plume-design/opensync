#include <ev.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include "const.h"
#include "dpp_client.h"
#include "log.h"
#include "os_time.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "target.h"

#include "wbm.h"
#include "wbm_stats.h"
#include "wbm_engine.h"
#include "wbm_report.h"
#include "wbm_traffic_gen.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

/******************************************************************************
 *  PROTECTED definitions
 *****************************************************************************/

#define THRESHOLD_MEM               (8 * 1024)
#define THRESHOLD_CPU               90
#define STATS_UPDATE_INTERVAL       1
#define STATS_GET_DELAY_MS          (STATS_UPDATE_INTERVAL * 3 * 1000)
#define WIFI_TABLE_CACHE_LIVE_MS    (1 * 1000)

typedef struct wbm_entry
{
    ds_dlist_node_t     next_entry;

    wbm_node_t          *node;          /* The node includes all configs and results */
    uint8_t             args_valid;     /* Shows if the input parameters are verified and valid */
    uint8_t             stats_ready;    /* Shows if the result stats are collected */
    wbm_request_cb_t    cb;             /* Callback that is called once Blast result is ready */
} wbm_entry_t;

typedef struct tables
{
    void                *data;
    int                 cnt;
    uint64_t            ts;
} tables_t;

static ds_dlist_t g_entry_queue;
static ev_timer   g_timer_args_validate;
static ev_timer   g_timer_stats_get;
static ev_timer   g_timer_entry_run;
static ev_timer   g_timer_sample_run;
static tables_t   g_stats_tables;
static tables_t   g_vifs_tables;
static tables_t   g_radios_tables;

static c_item_t g_map_ovsdb_chanwidth[] = {
    C_ITEM_STR( RADIO_CHAN_WIDTH_20MHZ,         "HT20" ),
    C_ITEM_STR( RADIO_CHAN_WIDTH_40MHZ,         "HT40" ),
    C_ITEM_STR( RADIO_CHAN_WIDTH_40MHZ_ABOVE,   "HT40+" ),
    C_ITEM_STR( RADIO_CHAN_WIDTH_40MHZ_BELOW,   "HT40-" ),
    C_ITEM_STR( RADIO_CHAN_WIDTH_80MHZ,         "HT80" ),
    C_ITEM_STR( RADIO_CHAN_WIDTH_160MHZ,        "HT160" ),
    C_ITEM_STR( RADIO_CHAN_WIDTH_80_PLUS_80MHZ, "HT80+80" ),
    C_ITEM_STR( RADIO_CHAN_WIDTH_NONE,          "HT2040" )
};

static c_item_t g_map_ovsdb_hwmode[] = {
    C_ITEM_STR( RADIO_802_11_A,     "11a" ),
    C_ITEM_STR( RADIO_802_11_BG,    "11b" ),
    C_ITEM_STR( RADIO_802_11_BG,    "11g" ),
    C_ITEM_STR( RADIO_802_11_NG,    "11n" ),
    C_ITEM_STR( RADIO_802_11_A,     "11ab" ),
    C_ITEM_STR( RADIO_802_11_AC,    "11ac" ),
};

static c_item_t g_map_ovsdb_radiotype[] = {
    C_ITEM_STR( RADIO_TYPE_2G,      RADIO_TYPE_STR_2G ),
    C_ITEM_STR( RADIO_TYPE_5G,      RADIO_TYPE_STR_5G ),
    C_ITEM_STR( RADIO_TYPE_5GL,     RADIO_TYPE_STR_5GL ),
    C_ITEM_STR( RADIO_TYPE_5GU,     RADIO_TYPE_STR_5GU ),
};

static void wbm_engine_timer_set(struct ev_timer *timer, uint32_t timeout_ms, void *ctx)
{
    double delay;

    delay = (timeout_ms > 0) ? ((timeout_ms / 1000.0) + (ev_time() - ev_now(EV_DEFAULT))) : DBL_MIN;
    ev_timer_stop(EV_DEFAULT, timer);
    ev_timer_set(timer, delay, 0);
    timer->data = ctx;
    ev_timer_start(EV_DEFAULT, timer);
}

static char* wbm_engine_entry_desc_get(wbm_entry_t *entry)
{
    static char desc[128];
    int res;

    res = snprintf(desc, sizeof(desc), "Plan[%s] Step[%d] Mac[%s]",
        entry->node->plan->plan_id, entry->node->step_id, entry->node->dest_mac);

    return (res < 0) ? NULL : desc;
}

static void wbm_engine_err(wbm_entry_t *entry, const char *func, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsnprintf(entry->node->status_desc, sizeof(entry->node->status_desc), fmt, args);
    va_end(args);

    LOGE("%s: %s %s", func, wbm_engine_entry_desc_get(entry), entry->node->status_desc);
}

static void wbm_engine_info(wbm_entry_t *entry, const char *func, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsnprintf(entry->node->status_desc, sizeof(entry->node->status_desc), fmt, args);
    va_end(args);

    LOGI("%s: %s %s", func, wbm_engine_entry_desc_get(entry), entry->node->status_desc);
}

static void wbm_engine_tables_cache_free(tables_t *tables)
{
    if (tables->ts == 0) {
        return;
    }

    free(tables->data);
    tables->data = NULL;
    tables->cnt = 0;
    tables->ts = 0;
}

static void* wbm_engine_vifs_get(int *count)
{
    int i;
    int cnt;
    int vif_table_size;
    json_t *jrows;
    json_t *jrow;
    void *record;
    void *records_array = NULL;
    void *retval = NULL;
    pjs_errmsg_t perr;

    *count = 0;
    jrows = ovsdb_sync_select_where(SCHEMA_TABLE(Wifi_VIF_State), NULL);
    if (jrows == NULL) {
        return NULL;
    }

    cnt = json_array_size(jrows);
    if (cnt == 0) {
        goto Exit;
    }

    vif_table_size = sizeof(struct schema_Wifi_VIF_State);
    records_array = calloc(1, cnt * vif_table_size);
    if (records_array == NULL) {
        goto Exit;
    }

    for (i = 0; i < cnt; i++)
    {
        jrow = json_array_get(jrows, i);
        if (jrow == NULL) {
            goto Exit;
        }

        record = records_array + vif_table_size * i;
        if (!schema_Wifi_VIF_State_from_json(record, jrow, false, perr)) {
            goto Exit;
        }
    }
    retval = records_array;
    *count = cnt;

Exit:
    if (retval == NULL) {
        free(records_array);
    }
    json_decref(jrows);
    return retval;
}

static void* wbm_engine_vifs_cache_get(int *count)
{
    uint64_t ts = clock_real_ms();

    if ((g_vifs_tables.ts == 0) || ((g_vifs_tables.ts + WIFI_TABLE_CACHE_LIVE_MS) < ts))
    {
        LOGD("%s: updating the tables, ts[%"PRIu64"]", __func__, ts);
        wbm_engine_tables_cache_free(&g_vifs_tables);

        g_vifs_tables.data = wbm_engine_vifs_get(&g_vifs_tables.cnt);
        if (g_vifs_tables.data == NULL) {
            return NULL;
        }

        g_vifs_tables.ts = ts;
    }

    *count = g_vifs_tables.cnt;
    return g_vifs_tables.data;
}

static int wbm_engine_vif_state_ap_get(
        struct schema_Wifi_VIF_State *vstate,
        const struct schema_Wifi_Associated_Clients *client)
{
    struct schema_Wifi_VIF_State *vstates;
    int i;
    int n;

    vstates = wbm_engine_vifs_cache_get(&n);
    if (vstates == NULL) {
        return -1;
    }

    while (n--)
    {
        if (strcmp(vstates[n].mode, "ap") != 0) {
            continue;
        }

        for (i = 0; i < vstates[n].associated_clients_len; i++)
        {
            if (strcmp(vstates[n].associated_clients[i].uuid, client->_uuid.uuid) != 0) {
                continue;
            }

            memcpy(vstate, &vstates[n], sizeof(*vstate));
            return 0;
        }
    }

    return -1;
}

static int wbm_engine_vif_state_sta_get(struct schema_Wifi_VIF_State *vstate, char *str_mac)
{
    struct schema_Wifi_VIF_State *vstates;
    int n;

    vstates = wbm_engine_vifs_cache_get(&n);
    if (vstates == NULL) {
        return -1;
    }

    while (n--)
    {
        if (strcmp(vstates[n].mode, "sta") != 0) {
            continue;
        }

        if (strcmp(vstates[n].parent, str_mac) != 0) {
            continue;
        }

        memcpy(vstate, &vstates[n], sizeof(*vstate));
        return 0;
    }

    return -1;
}

static void* wbm_engine_radios_get(int *count)
{
    int i;
    int cnt;
    int vif_table_size;
    json_t *jrows;
    json_t *jrow;
    void *record;
    void *records_array = NULL;
    void *retval = NULL;
    pjs_errmsg_t perr;

    *count = 0;
    jrows = ovsdb_sync_select_where(SCHEMA_TABLE(Wifi_Radio_State), NULL);
    if (jrows == NULL) {
        return NULL;
    }

    cnt = json_array_size(jrows);
    if (cnt == 0) {
        goto Exit;
    }

    vif_table_size = sizeof(struct schema_Wifi_Radio_State);
    records_array = calloc(1, cnt * vif_table_size);
    if (records_array == NULL) {
        goto Exit;
    }

    for (i = 0; i < cnt; i++)
    {
        jrow = json_array_get(jrows, i);
        if (jrow == NULL) {
            goto Exit;
        }

        record = records_array + vif_table_size * i;
        if (!schema_Wifi_Radio_State_from_json(record, jrow, false, perr)) {
            goto Exit;
        }
    }
    retval = records_array;
    *count = cnt;

Exit:
    if (retval == NULL) {
        free(records_array);
    }
    json_decref(jrows);
    return retval;
}

static void* wbm_engine_radios_cache_get(int *count)
{
    uint64_t ts = clock_real_ms();

    if ((g_radios_tables.ts == 0) || ((g_radios_tables.ts + WIFI_TABLE_CACHE_LIVE_MS) < ts))
    {
        LOGD("%s: updating the tables, ts[%"PRIu64"]", __func__, ts);
        wbm_engine_tables_cache_free(&g_radios_tables);

        g_radios_tables.data = wbm_engine_radios_get(&g_radios_tables.cnt);
        if (g_radios_tables.data == NULL) {
            return NULL;
        }

        g_radios_tables.ts = ts;
    }

    *count = g_radios_tables.cnt;
    return g_radios_tables.data;
}

static int wbm_engine_radio_state_get(
        struct schema_Wifi_Radio_State *rstate,
        const struct schema_Wifi_VIF_State *vstate)
{
    struct schema_Wifi_Radio_State *rstates;
    int i;
    int n;

    rstates = wbm_engine_radios_cache_get(&n);
    if (rstates == NULL) {
        return -1;
    }

    while (n--)
    {
        for (i = 0; i < rstates[n].vif_states_len; i++)
        {
            if (strcmp(rstates[n].vif_states[i].uuid, vstate->_uuid.uuid) != 0) {
                continue;
            }

            memcpy(rstate, &rstates[n], sizeof(*rstate));
            return 0;
        }
    }

    return -1;
}

static int wbm_engine_assoc_get(struct schema_Wifi_Associated_Clients *sch, char *str_mac)
{
    json_t *jrow;
    json_t *jrows = NULL;
    json_t *where = NULL;
    pjs_errmsg_t perr;
    int retval = -1;

    where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Associated_Clients, mac), str_mac);
    if (where == NULL) {
        goto Exit;
    }

    jrows = ovsdb_sync_select_where(SCHEMA_TABLE(Wifi_Associated_Clients), where);
    if (jrows == NULL) {
        goto Exit;
    }

    if (json_array_size(jrows) != 1) {
        goto Exit;
    }

    jrow = json_array_get(jrows, 0);
    if (jrow == NULL) {
        goto Exit;
    }

    if (!schema_Wifi_Associated_Clients_from_json(sch, jrow, false, perr)) {
        goto Exit;
    }

    retval = 0;
Exit:
    if (jrows != NULL) {
        json_decref(jrows);
    }
    if (where != NULL) {
        json_decref(where);
    }
    return retval;
}

static int wbm_engine_is_bhaul_active(char *vif_name, char *str_mac)
{
    json_t *cond;
    json_t *jrow;
    json_t *where;
    int is_exist;

    where = json_array();
    if (where == NULL) {
        LOGE("%s: [%s] Failed to allocate json array", __func__, str_mac);
        return 0;
    }

    cond = ovsdb_tran_cond_single("if_name", OFUNC_EQ, vif_name);
    json_array_append_new(where, cond);
    cond = ovsdb_tran_cond_single("parent", OFUNC_EQ, str_mac);
    json_array_append_new(where, cond);

    jrow = ovsdb_sync_select_where(SCHEMA_TABLE(Wifi_VIF_State), where);
    is_exist = !!json_array_size(jrow);
    if (!is_exist) {
        LOGD("%s: [%s] Wifi_VIF_State table doesn't exist", __func__, str_mac);
    }

    json_decref(jrow);
    json_decref(where);
    return is_exist;
}

static int wbm_engine_mac_is_available(wbm_node_t *node)
{
    struct schema_Wifi_Associated_Clients client;

    return (wbm_engine_assoc_get(&client, node->dest_mac) == 0)
           || wbm_engine_is_bhaul_active(node->info.if_name, node->dest_mac);
}

static int wbm_engine_vif_state_get(struct schema_Wifi_VIF_State *vstate, char *str_mac)
{
    struct schema_Wifi_Associated_Clients client;

    if ((wbm_engine_assoc_get(&client, str_mac) == 0)) {
        return wbm_engine_vif_state_ap_get(vstate, &client);
    }

    return wbm_engine_vif_state_sta_get(vstate, str_mac);
}

static int wbm_engine_info_fill(wbm_entry_t *entry, char *str_mac)
{
    struct schema_Wifi_VIF_State vstate;
    struct schema_Wifi_Radio_State rstate;
    c_item_t *item;
    wbm_info_t *info;
    int res;

    res = wbm_engine_vif_state_get(&vstate, str_mac);
    if (res != 0)
    {
        wbm_engine_err(entry, __func__, "Failed to find MAC in the OVSDB tables");
        entry->node->status = WBM_STATUS_NO_CLIENT;
        return -1;
    }

    res = wbm_engine_radio_state_get(&rstate, &vstate);
    if (res != 0)
    {
        wbm_engine_err(entry, __func__, "Failed to get Wifi_Radio_State schema");
        entry->node->status = WBM_STATUS_FAILED;
        return -1;
    }

    info = &entry->node->info;
    STRSCPY(info->if_name, vstate.if_name);
    STRSCPY(info->radio_name, rstate.if_name);
    info->chan = rstate.channel;
    item = c_get_item_by_str(g_map_ovsdb_chanwidth, rstate.ht_mode);
    info->chanwidth = item->key;
    item = c_get_item_by_str(g_map_ovsdb_hwmode, rstate.hw_mode);
    info->radio_proto = item->key;
    item = c_get_item_by_str(g_map_ovsdb_radiotype, rstate.freq_band);
    info->radio_type = item->key;

    LOGD("%s: %s vif_name[%s] radio_name[%s]", __func__,
         wbm_engine_entry_desc_get(entry), info->if_name, info->radio_name);
    return 0;
}

static int wbm_engine_is_mac_valid(char *mac_str)
{
    osn_mac_addr_t mac;

    return (strlen(mac_str) == (OSN_MAC_ADDR_LEN - 1))
           && osn_mac_addr_from_str(&mac, mac_str);
}

static int wbm_engine_client_stats_get(wbm_entry_t *entry, dpp_client_stats_t *stats)
{
    osn_mac_addr_t mac;
    bool res;

    memset(stats, 0, sizeof(*stats));
    osn_mac_addr_from_str(&mac, entry->node->dest_mac);
    res = target_stats_client_get(
            entry->node->info.radio_type,
            entry->node->info.if_name,
            entry->node->info.radio_name,
            mac.ma_addr,
            stats);
    if (!res) {
        wbm_engine_err(entry, __func__, "Failed to get target client stats");
        return -1;
    }

    return 0;
}

static int wbm_engine_sample_stats_fill(wbm_entry_t *entry)
{
    dpp_client_stats_t stats;
    wbm_result_t *result = &entry->node->result;

    if (wbm_engine_client_stats_get(entry, &stats) != 0)
    {
        if (!wbm_engine_mac_is_available(entry->node)) {
            wbm_engine_err(entry, __func__, "The MAC is disconnected");
            entry->node->status = WBM_STATUS_NO_CLIENT;
        }
        return -1;
    }

    result->sample[result->sample_cnt].tx_bytes = stats.bytes_tx;
    result->sample[result->sample_cnt].tx_retries = stats.retries_tx;
    result->sample[result->sample_cnt].ts = clock_real_ms();
    result->sample_cnt++;

    return 0;
}

static void wbm_engine_plan_thresholds_fill(wbm_request_t *req, wbm_plan_t *plan)
{
    unsigned int i;

    plan->threshold_cpu = THRESHOLD_CPU;
    plan->threshold_mem = THRESHOLD_MEM;

    for (   i = 0;
            (i < sizeof(req->config)/sizeof(req->config[0])) && (req->config[i].key != NULL);
            i++)
    {
        if (strcmp("threshold_cpu", req->config[i].key) == 0) {
            plan->threshold_cpu = atoi(req->config[i].value);
            continue;
        }

        if (strcmp("threshold_mem", req->config[i].key) == 0) {
            plan->threshold_mem = atoi(req->config[i].value);
        }
    }

    LOGD("%s: Plan[%s] threshold_cpu[%d], threshold_mem[%d]", __func__,
         req->plan_id, plan->threshold_cpu, plan->threshold_mem);
}

static void wbm_engine_plan_init(wbm_request_t *req, wbm_plan_t *plan)
{
    STRSCPY(plan->plan_id, req->plan_id);
    plan->sample_count = req->sample_count;
    plan->duration = req->duration;
    plan->packet_size = req->packet_size;

    wbm_engine_plan_thresholds_fill(req, plan);
}

static wbm_plan_t* wbm_engine_plan_lookup(char *plan_id)
{
    wbm_entry_t *entry;
    ds_dlist_iter_t iter;

    for (   entry = ds_dlist_ifirst(&iter, &g_entry_queue);
            entry != NULL;
            entry = ds_dlist_inext(&iter))
    {
        if (strcmp(entry->node->plan->plan_id, plan_id) == 0) {
            return entry->node->plan;
        }
    }

    return NULL;
}

static wbm_plan_t* wbm_engine_plan_create(wbm_request_t *request)
{
    wbm_plan_t *plan;

    plan = wbm_engine_plan_lookup(request->plan_id);
    if (plan == NULL)
    {
        plan = calloc(sizeof(*plan), 1);
        if (plan == NULL)
        {
            LOGE("%s: Plan[%s] Step[%d] Mac[%s] Failed to allocate Plan", __func__,
                 request->plan_id, request->step_id, request->dest_mac);
            return NULL;
        }
        wbm_engine_plan_init(request, plan);
    }

    plan->ref_cnt++;
    return plan;
}

static void wbm_engine_plan_free(wbm_plan_t *plan)
{
    if (plan == NULL) {
        return;
    }

    plan->ref_cnt--;
    if (plan->ref_cnt == 0) {
        free(plan);
    }
}

static void wbm_engine_node_free(wbm_node_t *node)
{
    if (node == NULL) {
        return;
    }

    free(node->result.sample);
    node->result.sample = NULL;
    node->result.sample_cnt = 0;
    wbm_engine_plan_free(node->plan);
    node->plan = NULL;
    free(node);
}


static void wbm_engine_entry_free(wbm_entry_t *entry)
{
    if (entry == NULL) {
        return;
    }

    wbm_engine_node_free(entry->node);
    entry->node = NULL;
    free(entry);
}

static int wbm_engine_sample_is_first_run(wbm_entry_t *entry)
{
    return entry->node->result.sample_cnt == 0;
}

static int wbm_engine_sample_is_finished(wbm_entry_t *entry)
{
    return entry->node->result.sample_cnt == (entry->node->plan->sample_count + 1);
}

static char* wbm_engine_status_to_str(wbm_status_t status)
{
    switch (status)
    {
        case WBM_STATUS_SUCCEED:
            return "SUCCEED";
        case WBM_STATUS_CANCELED:
            return "CANCELED";
        case WBM_STATUS_FAILED:
            return "FAILED";
        case WBM_STATUS_BUSY:
            return "BUSY";
        case WBM_STATUS_NO_CLIENT:
            return "NO_CLIENT";
        case WBM_STATUS_WRONG_ARG:
            return "WRONG_ARG";
        default:
            return "UNDEFINED";
    }
}

static void wbm_engine_entry_finalize(wbm_entry_t *entry)
{
    int i;
    char message[128];
    uint32_t bits_diff;
    uint32_t timestamp_diff;
    wbm_result_t *res = &entry->node->result;

    if (entry->node->status == WBM_STATUS_UNDEFINED) {
        entry->node->status = wbm_engine_sample_is_finished(entry) ? WBM_STATUS_SUCCEED :
            WBM_STATUS_FAILED;
    }

    STRSCPY(message, entry->node->status_desc);
    snprintf(entry->node->status_desc, sizeof(entry->node->status_desc), "%s Status[%s] %s",
        wbm_engine_entry_desc_get(entry), wbm_engine_status_to_str(entry->node->status), message);

    entry->node->state = WBM_STATE_DONE;
    entry->node->ts[WBM_TS_FINISHED] = clock_real_ms();

    for (i = 1; i < res->sample_cnt; i++)
    {
        bits_diff = (res->sample[i].tx_bytes - res->sample[i - 1].tx_bytes) * 8;
        timestamp_diff = res->sample[i].ts - res->sample[i - 1].ts;
        res->sample[i].throughput = (bits_diff / 1000.0) / timestamp_diff;

        res->sample[i].tx_retrans = res->sample[i].tx_retries - res->sample[i - 1].tx_retries;
    }
}

static int wbm_engine_entry_is_forced_done(wbm_entry_t *entry)
{
    return entry->node->status != WBM_STATUS_UNDEFINED;
}

static void wbm_engine_sample_run_schedule(wbm_entry_t *entry)
{
    uint32_t duration_ms = entry->node->plan->duration / entry->node->plan->sample_count;

    wbm_engine_timer_set(&g_timer_sample_run, duration_ms, entry);
}

static void wbm_engine_entry_finish(wbm_entry_t *entry)
{
    wbm_engine_entry_finalize(entry);

    /* Report the test results */
    entry->cb(entry->node);
    wbm_engine_entry_free(entry);
}

static void wbm_engine_entry_done(wbm_entry_t *entry)
{
    ds_dlist_remove(&g_entry_queue, entry);
    wbm_engine_entry_finish(entry);

    wbm_engine_timer_set(&g_timer_entry_run, 0, NULL);
}

static void wbm_engine_sample_run(struct ev_loop *loop, ev_timer *timer, int revents)
{
    wbm_entry_t *entry = timer->data;

    LOGD("%s: %s Handle [%d] sample", __func__,
         wbm_engine_entry_desc_get(entry), entry->node->result.sample_cnt);

    if (wbm_engine_entry_is_forced_done(entry)) {
        goto Finish;
    }

    if (wbm_engine_sample_is_first_run(entry)) {
        entry->node->ts[WBM_TS_STARTED] = clock_real_ms();
    }

    if (wbm_engine_sample_stats_fill(entry) != 0) {
        goto Finish;
    }

    if (!wbm_engine_sample_is_finished(entry)) {
        wbm_engine_sample_run_schedule(entry);
        return;
    }

Finish:
    if (wbm_traffic_gen_get_ctx() != NULL) {
        wbm_traffic_gen_stop();
    }

    wbm_engine_entry_done(entry);
}

static void wbm_engine_traffic_gen_cb(int error, void *ctx)
{
    wbm_entry_t *entry = ctx;

    if (error)
    {
        wbm_engine_err(entry, __func__, "Failed to run Traffic generator");
        if (!wbm_engine_mac_is_available(entry->node)) {
            entry->node->status = WBM_STATUS_NO_CLIENT;
        } else {
            entry->node->status = WBM_STATUS_FAILED;
        }
        wbm_engine_entry_done(entry);
        return;
    }

    wbm_engine_timer_set(&g_timer_sample_run, 0, entry);
}

static int wbm_engine_entry_is_resources_enough(wbm_entry_t *entry)
{
    wbm_stats_health_t *health = &entry->node->result.health;
    wbm_plan_t *plan = entry->node->plan;

    if ((health->cpu_util == 0) && (health->mem_free == 0)) {
        LOGW("%s: %s Health stats are empty", __func__, wbm_engine_entry_desc_get(entry));
        return 1;
    }

    if ((plan->threshold_cpu == 0) && (plan->threshold_mem == 0)) {
        LOGI("%s: %s Health thresholds are empty", __func__, wbm_engine_entry_desc_get(entry));
        return 1;
    }

    if ((health->cpu_util != 0)
        && (plan->threshold_cpu != 0)
        && (health->cpu_util > (unsigned)plan->threshold_cpu))
    {
        wbm_engine_info(entry, __func__, "Skip because of high CPU usage [%u]%%. Threshold [%d]%%",
            health->cpu_util, plan->threshold_cpu);
        return 0;
    }

    if ((health->mem_free != 0)
        && (plan->threshold_mem != 0)
        && (health->mem_free < (unsigned)plan->threshold_mem))
    {
        wbm_engine_info(entry, __func__, "Skip because of low free RAM memory [%u]KB. "
            "Threshold [%d]KB", health->mem_free, plan->threshold_mem);
        return 0;
    }

    return 1;
}

static void wbm_engine_entry_run(struct ev_loop *loop, ev_timer *timer, int revents)
{
    wbm_entry_t *entry;

    if (ds_dlist_is_empty(&g_entry_queue)) {
        LOGD("%s: No more Entries exist", __func__);
        return;
    }

    entry = ds_dlist_head(&g_entry_queue);
    if (entry->node->state == WBM_STATE_IN_PROGRESS) {
        LOGD("%s: Running entry already exists. Waiting", __func__);
        return;
    }

    if (!entry->stats_ready) {
        LOGD("%s: Stats collecting is in progress. Waiting", __func__);
        return;
    }

    if (!wbm_engine_entry_is_resources_enough(entry))
    {
        entry->node->status = WBM_STATUS_BUSY;
        wbm_engine_entry_done(entry);
        return;
    }

    entry->node->state = WBM_STATE_IN_PROGRESS;

    wbm_traffic_gen_async_start(
        entry->node->info.radio_type,
        entry->node->info.if_name,
        entry->node->info.radio_name,
        entry->node->dest_mac,
        entry->node->plan->packet_size,
        wbm_engine_traffic_gen_cb,
        entry);
}

static void wbm_engine_stats_health_fill(void)
{
    wbm_entry_t *entry;
    ds_dlist_iter_t iter;
    wbm_stats_health_t hstats;
    int stats_error = 0;

    if (wbm_stats_health_get(&hstats) != 0) {
        LOGE("%s: Failed to get health stats", __func__);
        stats_error = 1;
    }

    for (   entry = ds_dlist_ifirst(&iter, &g_entry_queue);
            entry != NULL;
            entry = ds_dlist_inext(&iter))
    {
        if (entry->stats_ready) {
            continue;
        }

        if (!stats_error) {
            memcpy(&entry->node->result.health, &hstats, sizeof(entry->node->result.health));
            continue;
        }

        wbm_engine_err(entry, __func__, "Failed to fill in health stats");
        ds_dlist_iremove(&iter);
        entry->node->status = WBM_STATUS_FAILED;
        wbm_engine_entry_finish(entry);
    }
}

static int wbm_engine_stats_radio_get(wbm_stats_radio_t *stats, char *phy_name, uint32_t channel)
{
    wbm_entry_t *entry;
    ds_dlist_iter_t iter;

    for (   entry = ds_dlist_ifirst(&iter, &g_entry_queue);
            entry != NULL;
            entry = ds_dlist_inext(&iter))
    {
        if (!entry->stats_ready) {
            continue;
        }

        if ((entry->node->info.chan != channel)
            || (strcmp(entry->node->info.radio_name, phy_name) != 0))
        {
            continue;
        }

        memcpy(stats, &entry->node->result.radio, sizeof(*stats));
        return 0;
    }

    if (wbm_stats_radio_get(stats, phy_name, channel) != 0) {
        LOGE("%s: Failed to get radio stats [%s]phy_name [%d]chan", __func__, phy_name, channel);
        return -1;
    }

    return 0;
}

static void wbm_engine_stats_radio_fill(void)
{
    wbm_entry_t *entry;
    ds_dlist_iter_t iter;
    int res;

    for (   entry = ds_dlist_ifirst(&iter, &g_entry_queue);
            entry != NULL;
            entry = ds_dlist_inext(&iter))
    {
        if (entry->stats_ready) {
            continue;
        }

        res = wbm_engine_stats_radio_get(
                &entry->node->result.radio,
                entry->node->info.radio_name,
                entry->node->info.chan);
        if (res == 0) {
            continue;
        }

        wbm_engine_err(entry, __func__, "Failed to fill in radio stats");
        ds_dlist_iremove(&iter);
        entry->node->status = WBM_STATUS_FAILED;
        wbm_engine_entry_finish(entry);
    }
}

static void wbm_engine_stats_client_fill(void)
{
    wbm_entry_t *entry;
    ds_dlist_iter_t iter;

    for (   entry = ds_dlist_ifirst(&iter, &g_entry_queue);
            entry != NULL;
            entry = ds_dlist_inext(&iter))
    {
        if (entry->stats_ready) {
            continue;
        }

        if (wbm_stats_client_get(&entry->node->result.client, entry->node->dest_mac) == 0) {
            continue;
        }

        wbm_engine_err(entry, __func__, "Failed to fill in client stats");
        ds_dlist_iremove(&iter);
        entry->node->status = WBM_STATUS_FAILED;
        wbm_engine_entry_finish(entry);
    }
}

static json_t* wbm_engine_wifi_stats_where_create(char *stats_type, char *radio_type)
{
    json_t *cond;
    json_t *where;

    if ((stats_type == NULL) || (*stats_type == '\0')) {
        LOGE("%s: stats_type argument is invalid", __func__);
        return NULL;
    }

    where = json_array();
    if (where == NULL) {
        LOGE("%s: Failed to allocate json_array: %s-%s", __func__, stats_type, radio_type);
        return NULL;
    }

    cond = ovsdb_tran_cond_single("stats_type", OFUNC_EQ, stats_type);
    json_array_append_new(where, cond);
    if (radio_type != NULL) {
        cond = ovsdb_tran_cond_single("radio_type", OFUNC_EQ, radio_type);
        json_array_append_new(where, cond);
    }

    if (strcmp("survey", stats_type) == 0) {
        cond = ovsdb_tran_cond_single("survey_type", OFUNC_EQ, "on-chan");
        json_array_append_new(where, cond);
    }

    return where;
}

static void* wbm_engine_wifi_stats_configs_get(int *count)
{
    int i;
    int cnt;
    int stats_table_size;
    json_t *jrows;
    json_t *jrow;
    void *record;
    void *records_array = NULL;
    void *retval = NULL;
    pjs_errmsg_t perr;

    *count = 0;
    jrows = ovsdb_sync_select_where(SCHEMA_TABLE(Wifi_Stats_Config), NULL);
    if (jrows == NULL) {
        return NULL;
    }

    cnt = json_array_size(jrows);
    if (cnt == 0) {
        goto Exit;
    }

    stats_table_size = sizeof(struct schema_Wifi_Stats_Config);
    records_array = calloc(1, cnt * stats_table_size);
    if (records_array == NULL) {
        goto Exit;
    }

    for (i = 0; i < cnt; i++)
    {
        jrow = json_array_get(jrows, i);
        if (jrow == NULL) {
            goto Exit;
        }

        record = records_array + stats_table_size * i;
        if (!schema_Wifi_Stats_Config_from_json(record, jrow, false, perr)) {
            goto Exit;
        }
    }
    retval = records_array;
    *count = cnt;

Exit:
    if (retval == NULL) {
        free(records_array);
    }
    json_decref(jrows);
    return retval;
}

static int wbm_engine_wifi_stats_config_get(
        struct schema_Wifi_Stats_Config *stats,
        char *stats_type,
        char *radio_type)
{
    struct schema_Wifi_Stats_Config *stats_table;
    int n;

    if (g_stats_tables.data == NULL) {
        return -1;
    }

    stats_table = g_stats_tables.data;
    n = g_stats_tables.cnt;
    while (n--)
    {
        if (strcmp(stats_table[n].stats_type, stats_type) != 0) {
            continue;
        }

        if ((radio_type != NULL) && (strcmp(stats_table[n].radio_type, radio_type) != 0)) {
            continue;
        }

        if ((strcmp("survey", stats_type) == 0)
            && (strcmp(stats_table[n].survey_type, "on-chan") != 0))
        {
            continue;
        }

        memcpy(stats, &stats_table[n], sizeof(*stats));
        return 0;
    }

    return -1;
}

static int wbm_engine_wifi_stats_config_update(
        struct schema_Wifi_Stats_Config *config,
        char *stats_type,
        char *radio_type)
{
    json_t *where;
    json_t *jrow = NULL;
    pjs_errmsg_t perr;
    int ret = -1;

    where = wbm_engine_wifi_stats_where_create(stats_type, radio_type);
    if (where == NULL) {
        return -1;
    }

    jrow = schema_Wifi_Stats_Config_to_json(config, perr);
    if (jrow == NULL) {
        LOGE("%s: Failed to get Wifi_Stats_Config json: %s-%s", __func__, stats_type, radio_type);
        goto Exit;
    }

    if (ovsdb_sync_update_where(SCHEMA_TABLE(Wifi_Stats_Config), where, jrow) <= 0) {
        LOGE("%s: Failed to update Wifi_Stats_Config: %s-%s", __func__, stats_type, radio_type);
        goto Exit;
    }

    ret = 0;
Exit:
    if (jrow != NULL) {
        json_decref(jrow);
    }
    json_decref(where);
    return ret;
}

static void wbm_engine_stats_sampling_intervals_set(char *stats_type)
{
    struct schema_Wifi_Stats_Config config;
    char *radio_type;
    int backup;
    uint32_t i;

    for (i = 0; i < (int)ARRAY_SIZE(g_map_ovsdb_radiotype); i++)
    {
        radio_type = (char *)g_map_ovsdb_radiotype[i].data;

        if (wbm_engine_wifi_stats_config_get(&config, stats_type, radio_type) != 0) {
            continue;
        }

        if (config.sampling_interval <= STATS_UPDATE_INTERVAL) {
            continue;
        }

        backup = config.sampling_interval;
        config.sampling_interval = STATS_UPDATE_INTERVAL;
        wbm_engine_wifi_stats_config_update(&config, stats_type, radio_type);
        config.sampling_interval = backup;

        LOGD("%s: stats_type[%s] radio_type[%s] old_int[%d] new_int[%d]", __func__,
             stats_type, radio_type, backup, STATS_UPDATE_INTERVAL);
    }
}

static void wbm_engine_stats_sampling_intervals_restore(char *stats_type)
{
    struct schema_Wifi_Stats_Config config;
    char *radio_type;
    uint32_t i;

    for (i = 0; i < ARRAY_SIZE(g_map_ovsdb_radiotype); i++)
    {
        radio_type = (char *)g_map_ovsdb_radiotype[i].data;

        if (wbm_engine_wifi_stats_config_get(&config, stats_type, radio_type) != 0) {
            continue;
        }

        if (config.sampling_interval <= STATS_UPDATE_INTERVAL) {
            continue;
        }

        wbm_engine_wifi_stats_config_update(&config, stats_type, radio_type);
        LOGD("%s: stats_type[%s] radio_type[%s] int[%d]", __func__,
             stats_type, radio_type, config.reporting_interval);
    }
}

static void wbm_engine_stats_reporting_interval_set(char *stats_type)
{
    struct schema_Wifi_Stats_Config config;
    int backup;

    if (wbm_engine_wifi_stats_config_get(&config, stats_type, NULL) != 0) {
        return;
    }

    if (config.reporting_interval <= STATS_UPDATE_INTERVAL) {
        return;
    }

    backup = config.reporting_interval;
    config.reporting_interval = STATS_UPDATE_INTERVAL;
    wbm_engine_wifi_stats_config_update(&config, stats_type, NULL);
    config.reporting_interval = backup;

    LOGD("%s: stats_type[%s] old_int[%d] new_int[%d]", __func__,
         stats_type, backup, STATS_UPDATE_INTERVAL);
}

static void wbm_engine_stats_reporting_interval_restore(char *stats_type)
{
    struct schema_Wifi_Stats_Config config;

    if (wbm_engine_wifi_stats_config_get(&config, stats_type, NULL) != 0) {
        return;
    }

    if (config.reporting_interval <= STATS_UPDATE_INTERVAL) {
        return;
    }

    wbm_engine_wifi_stats_config_update(&config, stats_type, NULL);
    LOGD("%s: stats_type[%s] int[%d]", __func__, stats_type, config.reporting_interval);
}

static int wbm_engine_stats_fresh_request(void)
{
    if (g_stats_tables.ts != 0) {
        return 0;
    }

    g_stats_tables.data = wbm_engine_wifi_stats_configs_get(&g_stats_tables.cnt);
    if (g_stats_tables.data == NULL) {
        return -1;
    }

    LOGD("%s: Change SM intervals", __func__);

    wbm_engine_stats_sampling_intervals_set("survey");
    wbm_engine_stats_sampling_intervals_set("client");
    wbm_engine_stats_reporting_interval_set("device");

    g_stats_tables.ts = clock_real_ms();
    return 0;
}

static void wbm_engine_stats_fresh_reset(void)
{
    if (g_stats_tables.ts == 0) {
        return;
    }

    LOGD("%s: Restore SM intervals", __func__);

    wbm_engine_stats_reporting_interval_restore("device");
    wbm_engine_stats_sampling_intervals_restore("client");
    wbm_engine_stats_sampling_intervals_restore("survey");
    wbm_engine_tables_cache_free(&g_stats_tables);
}

static int wbm_engine_stats_fresh_are_ready(void)
{
    int64_t measure_delay;

    if (g_stats_tables.ts == 0) {
        return 0;
    }

    measure_delay = g_stats_tables.ts + STATS_GET_DELAY_MS;
    return (clock_real_ms() >= measure_delay);
}

static uint32_t wbm_engine_stats_fresh_delay_get(void)
{
    int64_t measure_delay = g_stats_tables.ts + STATS_GET_DELAY_MS;

    return (measure_delay - clock_real_ms());
}

static int wbm_engine_entry_args_validate(wbm_entry_t *entry)
{
    wbm_plan_t *plan = entry->node->plan;
    int len;

    len = strlen(plan->plan_id);
    if ((len < 1) || (len > 36))
    {
        wbm_engine_err(entry, __func__, "Invalid length of PlanID [%d]. Expected in range [1..36]",
            len);
        return -1;
    }

    if ((plan->sample_count < 1) || (plan->sample_count > 100))
    {
        wbm_engine_err(entry, __func__, "Invalid Sample count [%d]. Expected in range [1..100]",
            plan->sample_count);
        return -1;
    }

    if ((plan->duration < 1000) || (plan->duration > 10000))
    {
        wbm_engine_err(entry, __func__, "Invalid Duration [%d]ms. Expected in range [1000..10000]",
            plan->duration);
        return -1;
    }

    if ((plan->duration / plan->sample_count) < 100)
    {
        wbm_engine_err(entry, __func__, "Invalid Duration/Sample_count ratio [%d/%d = %d]ms. "
            "Expected >= 100", plan->duration, plan->sample_count,
            plan->duration / plan->sample_count);
        return -1;
    }

    if ((plan->packet_size < 64) || (plan->packet_size > 1470))
    {
        wbm_engine_err(entry, __func__, "Invalid Packet size [%d]bytes. Expected in range "
            "[64..1470]", plan->packet_size);
        return -1;
    }

    if ((plan->threshold_cpu < 0) || (plan->threshold_cpu > 100))
    {
        wbm_engine_err(entry, __func__, "Invalid CPU threshold [%d]%%. Expected in range [0..100]",
            plan->threshold_cpu);
        return -1;
    }

    if ((plan->threshold_mem < (1 * 1024)) || (plan->threshold_mem > INT_MAX))
    {
        wbm_engine_err(entry, __func__, "Invalid RAM memory threshold [%d]KB. Expected in range "
            "[1024..INT_MAX]", plan->threshold_mem);
        return -1;
    }

    if ((entry->node->step_id < 0) || (entry->node->step_id > INT_MAX))
    {
        wbm_engine_err(entry, __func__, "Invalid StepID [%d]. Expected in range [0..INT_MAX]",
            entry->node->step_id);
        return -1;
    }

    if (!wbm_engine_is_mac_valid(entry->node->dest_mac))
    {
        wbm_engine_err(entry, __func__, "Invalid MAC address [%s]", entry->node->dest_mac);
        return -1;
    }

    return 0;
}

static void wbm_engine_args_validate(struct ev_loop *loop, ev_timer *timer, int revents)
{
    wbm_entry_t *entry;
    ds_dlist_iter_t iter;

    for (   entry = ds_dlist_ifirst(&iter, &g_entry_queue);
            entry != NULL;
            entry = ds_dlist_inext(&iter))
    {
        if (entry->args_valid) {
            continue;
        }

        if (wbm_engine_entry_args_validate(entry) != 0) {
            entry->node->status = WBM_STATUS_WRONG_ARG;
            goto Error;
        }

        if (wbm_engine_info_fill(entry, entry->node->dest_mac) != 0) {
            goto Error;
        }

        entry->args_valid = 1;
        continue;

Error:
        ds_dlist_iremove(&iter);
        wbm_engine_entry_finish(entry);
    }

    if (!ds_dlist_is_empty(&g_entry_queue)) {
        wbm_engine_timer_set(&g_timer_stats_get, 0, NULL);
    }
}

static void wbm_engine_stats_get(struct ev_loop *loop, ev_timer *timer, int revents)
{
    wbm_entry_t *entry;
    ds_dlist_iter_t iter;
    int res = 0;

    if (!wbm_engine_stats_fresh_are_ready())
    {
        res = wbm_engine_stats_fresh_request();
        if (res != 0) {
            goto Exit;
        }

        wbm_engine_timer_set(&g_timer_stats_get, wbm_engine_stats_fresh_delay_get(), NULL);
        return;
    }

    wbm_engine_stats_fresh_reset();

    wbm_engine_stats_health_fill();
    wbm_engine_stats_radio_fill();
    wbm_engine_stats_client_fill();

Exit:
    for (   entry = ds_dlist_ifirst(&iter, &g_entry_queue);
            entry != NULL;
            entry = ds_dlist_inext(&iter))
    {
        if (entry->stats_ready) {
            continue;
        }

        if (res == 0) {
            entry->stats_ready = 1;
            continue;
        }

        wbm_engine_err(entry, __func__, "Failed to update stats");
        ds_dlist_iremove(&iter);
        entry->node->status = WBM_STATUS_FAILED;
        wbm_engine_entry_finish(entry);
    }

    wbm_engine_timer_set(&g_timer_entry_run, 0, NULL);
}

static int wbm_request_is_valid(wbm_request_t *req)
{
    return ((req->plan_id != NULL)
           && (req->dest_mac != NULL)
           && (req->cb != NULL));
}

static char* wbm_engine_format_mac(char *str_mac)
{
    static char mac_addr[OSN_MAC_ADDR_LEN];

    if (strlen(str_mac) == 12)
    {
        snprintf(mac_addr, sizeof(mac_addr), "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
            str_mac[0], str_mac[1], str_mac[2], str_mac[3], str_mac[4], str_mac[5],
            str_mac[6], str_mac[7], str_mac[8], str_mac[9], str_mac[10], str_mac[11]);
    }
    else
    {
        snprintf(mac_addr, sizeof(mac_addr), "%s", str_mac);
    }
    str_tolower(mac_addr);

    return mac_addr;
}

static wbm_node_t* wbm_engine_node_create(wbm_request_t *request)
{
    wbm_node_t *node;

    node = calloc(sizeof(*node), 1);
    if (node == NULL)
    {
        LOGE("%s: Plan[%s] Step[%d] Mac[%s] Failed to allocate Node", __func__,
             request->plan_id, request->step_id, request->dest_mac);
        return NULL;
    }
    node->step_id = request->step_id;
    STRSCPY(node->dest_mac, wbm_engine_format_mac(request->dest_mac));
    node->dest_mac_orig_raw = strlen(request->dest_mac) == 12;
    node->ts[WBM_TS_RECEIVED] = request->timestamp;
    node->state = WBM_STATE_WAITING;
    node->status = WBM_STATUS_UNDEFINED;

    node->plan = wbm_engine_plan_create(request);
    if (node->plan == NULL) {
        goto Error;
    }

    if (request->sample_count > 0)
    {
        node->result.sample = calloc(sizeof(*node->result.sample), request->sample_count + 1);
        if (node->result.sample == NULL)
        {
            LOGE("%s: Plan[%s] Step[%d] Mac[%s] Failed to allocate Sample", __func__,
                 request->plan_id, request->step_id, request->dest_mac);
            goto Error;
        }
    }

    return node;

Error:
    wbm_engine_plan_free(node->plan);
    free(node);
    return NULL;
}

static wbm_entry_t* wbm_engine_entry_create(wbm_request_t *request)
{
    wbm_entry_t *entry;

    entry = calloc(sizeof(*entry), 1);
    if (entry == NULL)
    {
        LOGE("%s: Plan[%s] Step[%d] Mac[%s] Failed to allocate Entry", __func__,
             request->plan_id, request->step_id, request->dest_mac);
        return NULL;
    }
    entry->cb = request->cb;

    entry->node = wbm_engine_node_create(request);
    if (entry->node == NULL) {
        free(entry);
        return NULL;
    }

    return entry;
}

/******************************************************************************
 * PUBLIC API definitions
 *****************************************************************************/

int wbm_engine_request_add(wbm_request_t *request)
{
    wbm_entry_t *entry;
    LOGI("%s: Add Plan[%s] Step[%d] Mac[%s] request", __func__,
         request->plan_id, request->step_id, request->dest_mac);

    if (!wbm_request_is_valid(request))
    {
        LOGE("%s: Plan[%s] Step[%d] Mac[%s] Invalid request argument", __func__,
             request->plan_id, request->step_id, request->dest_mac);
        return -1;
    }

    entry = wbm_engine_entry_create(request);
    if (entry == NULL) {
        return -1;
    }

    ds_dlist_insert_tail(&g_entry_queue, entry);
    wbm_engine_timer_set(&g_timer_args_validate, 0, NULL);
    return 0;
}

int wbm_engine_plan_is_active(char *plan_id)
{
    return !!wbm_engine_plan_lookup(plan_id);
}

void wbm_engine_plan_cancel(char *plan_id)
{
    wbm_entry_t *entry;
    ds_dlist_iter_t iter;

    entry = wbm_traffic_gen_get_ctx();
    if ((entry != NULL) && (strcmp(entry->node->plan->plan_id, plan_id) == 0))
    {
        wbm_traffic_gen_stop();
        LOGI("%s: %s Canceling in-progress request", __func__, wbm_engine_entry_desc_get(entry));
        ev_timer_stop(EV_DEFAULT, &g_timer_sample_run);
        wbm_engine_timer_set(&g_timer_entry_run, 0, NULL);
    }

    for (   entry = ds_dlist_ifirst(&iter, &g_entry_queue);
            entry != NULL;
            entry = ds_dlist_inext(&iter))
    {
        if (strcmp(entry->node->plan->plan_id, plan_id) != 0) {
            continue;
        }

        wbm_engine_info(entry, __func__, "The request is canceled");
        ds_dlist_iremove(&iter);
        entry->node->status = WBM_STATUS_CANCELED;
        wbm_engine_entry_finish(entry);
    }
}

int wbm_engine_init(void)
{
    LOGI("Initializing WBM engine");

    if (wbm_traffic_gen_init() != 0) {
        return -1;
    }

    if (wbm_report_init() != 0) {
        return -1;
    }

    ds_dlist_init(&g_entry_queue, wbm_entry_t, next_entry);
    ev_init(&g_timer_args_validate, wbm_engine_args_validate);
    ev_init(&g_timer_stats_get, wbm_engine_stats_get);
    ev_init(&g_timer_entry_run, wbm_engine_entry_run);
    ev_init(&g_timer_sample_run, wbm_engine_sample_run);

    return 0;
}

void wbm_engine_uninit(void)
{
    ds_dlist_iter_t iter;
    wbm_entry_t *entry;

    ev_timer_stop(EV_DEFAULT, &g_timer_args_validate);
    ev_timer_stop(EV_DEFAULT, &g_timer_stats_get);
    ev_timer_stop(EV_DEFAULT, &g_timer_entry_run);
    ev_timer_stop(EV_DEFAULT, &g_timer_sample_run);

    if (wbm_traffic_gen_get_ctx() != NULL) {
        wbm_traffic_gen_stop();
    }

    wbm_engine_stats_fresh_reset();
    wbm_traffic_gen_uninit();

    for (   entry = ds_dlist_ifirst(&iter, &g_entry_queue);
            entry != NULL;
            entry = ds_dlist_inext(&iter))
    {
        ds_dlist_iremove(&iter);
        wbm_engine_entry_free(entry);
    }

    wbm_engine_tables_cache_free(&g_stats_tables);
    wbm_engine_tables_cache_free(&g_vifs_tables);
    wbm_engine_tables_cache_free(&g_radios_tables);
}
