#include <inttypes.h>

#include "const.h"
#include "os_time.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"

#include "wbm_engine.h"
#include "wbm_report.h"
#include "wbm.h"

/* Log entries from this file will contain "OVSDB" */
#define MODULE_ID LOG_MODULE_ID_OVSDB

ovsdb_table_t table_Wifi_Blaster_Config;
ovsdb_table_t table_Wifi_Blaster_State;

static int wbm_ovsdb_state_table_lookup(
        char *plan_id,
        char *state,
        struct schema_Wifi_Blaster_State *schema)
{
    json_t *cond;
    json_t *where;
    json_t *jrow = NULL;
    pjs_errmsg_t perr;
    int res = -1;

    where = json_array();
    if (where == NULL) {
        LOGE("%s: Plan[%s] Failed to allocate json array", __func__, plan_id);
        return -1;
    }

    cond = ovsdb_tran_cond_single("plan_id", OFUNC_EQ, plan_id);
    json_array_append_new(where, cond);
    cond = ovsdb_tran_cond_single("state", OFUNC_EQ, state);
    json_array_append_new(where, cond);

    jrow = ovsdb_sync_select_where(SCHEMA_TABLE(Wifi_Blaster_State), where);
    if (json_array_size(jrow) == 0) {
        LOGD("%s: Plan[%s] Wifi_Blaster_State table doesn't exist", __func__, plan_id);
        goto Exit;
    }

    if (!schema_Wifi_Blaster_State_from_json(schema, json_array_get(jrow, 0), false, perr)) {
        LOGE("%s: Plan[%s] Failed to parse Wifi_Blaster_State: %s", __func__, plan_id, perr);
        goto Exit;
    }

    res = 0;
Exit:
    if (jrow != NULL) {
        json_decref(jrow);
    }
    json_decref(where);
    return res;
}

static int wbm_ovsdb_state_table_exists(char *plan_id, char *state)
{
    struct schema_Wifi_Blaster_State schema;

    return !wbm_ovsdb_state_table_lookup(plan_id, state, &schema);
}

static void wbm_ovsdb_state_table_create(char *plan_id, char *state)
{
    struct schema_Wifi_Blaster_State schema;

    if (wbm_ovsdb_state_table_exists(plan_id, state)) {
        return;
    }

    memset(&schema, 0, sizeof(schema));
    SCHEMA_SET_STR(schema.plan_id, plan_id);
    SCHEMA_SET_STR(schema.state, state);

    if (!ovsdb_table_insert(&table_Wifi_Blaster_State, &schema)) {
        LOGE("%s: Plan[%s] Failed to add Wifi_Blaster_State table with [%s] state", __func__,
             plan_id, state);
    }
}

static void wbm_ovsdb_plan_complete(char *plan_id)
{
    /* The table is created and immediately removed according to design */
    wbm_ovsdb_state_table_create(plan_id, "complete");
    LOGI("%s: Plan[%s] is completed", __func__, plan_id);

    ovsdb_table_delete_simple(
            &table_Wifi_Blaster_State,
            SCHEMA_COLUMN(Wifi_Blaster_State, plan_id),
            plan_id);
    ovsdb_table_delete_simple(
            &table_Wifi_Blaster_Config,
            SCHEMA_COLUMN(Wifi_Blaster_Config, plan_id),
            plan_id);
}

static void wbm_ovsdb_print_dbg(wbm_node_t *node)
{
    int i;
    c_item_t *item;
    char *chanwidth;
    char *hwmode;
    char *rtype;
    wbm_stats_radio_t *rstats;
    wbm_stats_health_t *hstats;
    wbm_stats_client_t *cstats;
    uint64_t retrans_sum = 0;
    double throughput_sum = 0.0;

    c_item_t map_ovsdb_chanwidth[] = {
        C_ITEM_STR( RADIO_CHAN_WIDTH_20MHZ,         "HT20" ),
        C_ITEM_STR( RADIO_CHAN_WIDTH_40MHZ,         "HT40" ),
        C_ITEM_STR( RADIO_CHAN_WIDTH_40MHZ_ABOVE,   "HT40+" ),
        C_ITEM_STR( RADIO_CHAN_WIDTH_40MHZ_BELOW,   "HT40-" ),
        C_ITEM_STR( RADIO_CHAN_WIDTH_80MHZ,         "HT80" ),
        C_ITEM_STR( RADIO_CHAN_WIDTH_160MHZ,        "HT160" ),
        C_ITEM_STR( RADIO_CHAN_WIDTH_80_PLUS_80MHZ, "HT80+80" ),
        C_ITEM_STR( RADIO_CHAN_WIDTH_NONE,          "HT2040" )
    };

    c_item_t map_ovsdb_hwmode[] = {
        C_ITEM_STR( RADIO_802_11_A,                 "11a" ),
        C_ITEM_STR( RADIO_802_11_BG,                "11b" ),
        C_ITEM_STR( RADIO_802_11_BG,                "11g" ),
        C_ITEM_STR( RADIO_802_11_NG,                "11n" ),
        C_ITEM_STR( RADIO_802_11_A,                 "11ab" ),
        C_ITEM_STR( RADIO_802_11_AC,                "11ac" ),
    };

    c_item_t map_ovsdb_radiotype[] = {
        C_ITEM_STR( RADIO_TYPE_2G,                  RADIO_TYPE_STR_2G ),
        C_ITEM_STR( RADIO_TYPE_5G,                  RADIO_TYPE_STR_5G ),
        C_ITEM_STR( RADIO_TYPE_5GL,                 RADIO_TYPE_STR_5GL ),
        C_ITEM_STR( RADIO_TYPE_5GU,                 RADIO_TYPE_STR_5GU ),
    };

    item = c_get_item_by_key(map_ovsdb_chanwidth, node->info.chanwidth);
    chanwidth = (char *)item->data;
    item = c_get_item_by_key(map_ovsdb_hwmode, node->info.radio_proto);
    hwmode = (char *)item->data;
    item = c_get_item_by_key(map_ovsdb_radiotype, node->info.radio_type);
    rtype = (char *)item->data;

    LOGD("********** WiFi Blaster Test Results **********");
    LOGD("Client's MAC address[%s]", node->dest_mac);
    LOGD("Info: radio_name[%s] if_name[%s] channel[%u] proto[%s] width[%s] type[%s]",
         node->info.radio_name, node->info.if_name, node->info.chan, hwmode, chanwidth, rtype);

    LOGD("Duration [ms]: request-reply[%"PRIu64"] start-end[%"PRIu64"] blasting[%"PRIu64"]",
         node->ts[WBM_TS_FINISHED] - node->ts[WBM_TS_RECEIVED],
         node->ts[WBM_TS_FINISHED] - node->ts[WBM_TS_STARTED],
         node->result.sample[node->result.sample_cnt - 1].ts - node->result.sample[0].ts);

    rstats = &node->result.radio;
    LOGD("Radio stats: util[%u]%% activity[%u]%% interf[%u]%% noise_floor[%d]db",
         rstats->util, rstats->activity, rstats->interf, rstats->noise_floor);

    hstats = &node->result.health;
    LOGD("Health stats: CPU_util[%u]%% CPU_util_avg_1/5/15[%0.2f/%0.2f/%0.2f] RAM_free[%u]KB",
         hstats->cpu_util, hstats->cpu_util_1m, hstats->cpu_util_5m, hstats->cpu_util_15m,
         hstats->mem_free);

    cstats = &node->result.client;
    LOGD("Client stats: RSSI[%d]db TxRate[%u]Mbps RxRate[%u]Mbps SNR[%d]db",
         cstats->rssi, cstats->rate_tx, cstats->rate_rx, cstats->rssi - rstats->noise_floor);

    for (i = 1; i < node->result.sample_cnt; i++)
    {
        LOGD("Results: sample[%d] throughput[%f]Mbps, TxRetrans[%"PRIu64"], ts[%"PRIu64"]ms",
             i,
             node->result.sample[i].throughput,
             node->result.sample[i].tx_retrans,
             node->result.sample[i].ts);
        retrans_sum += node->result.sample[i].tx_retrans;
        throughput_sum += node->result.sample[i].throughput;
    }
    LOGD("Average throughput[%f]Mbps. Summ of retransmissions[%"PRIu64"]",
         throughput_sum / (node->result.sample_cnt - 1), retrans_sum);
    LOGD("***********************************************");
}

static void wbm_ovsdb_request_cb(wbm_node_t *node)
{
    char *plan_id = node->plan->plan_id;
    wbm_report_status_t report_status;

    LOGI("%s: Test result is ready. Description: %s", __func__, node->status_desc);

    if (node->status == WBM_STATUS_SUCCEED) {
        wbm_ovsdb_print_dbg(node);
    }

    report_status = wbm_report_publish(node);
    if (report_status != WBM_REPORT_STATUS_SUCCESS) {
        LOGE("%s: Plan[%s] Step[%d] Mac[%s] Failed to publish, err: %d", __func__,
             plan_id, node->step_id, node->dest_mac, report_status);
    }

    if (!wbm_engine_plan_is_active(plan_id) && wbm_ovsdb_state_table_exists(plan_id, "new")) {
        wbm_ovsdb_plan_complete(plan_id);
    }
}

static int wbm_ovsdb_plan_add(struct schema_Wifi_Blaster_Config *conf)
{
    wbm_request_t req;
    uint64_t timestamp;
    int i;
    int j;
    int succeed_cnt = 0;

    if (conf->plan_id[0] == '\0') {
        LOGE("%s: Plan ID is not found in the request", __func__);
        ovsdb_table_delete(&table_Wifi_Blaster_Config, conf);
        return -1;
    }

    if (wbm_engine_plan_is_active(conf->plan_id)) {
        LOGE("%s: Plan[%s] already exists. Ignoring new request", __func__, conf->plan_id);
        return -1;
    }

    if (conf->step_id_and_dest_len == 0) {
        LOGW("%s: Plan[%s] doesn't have any Step ID", __func__, conf->plan_id);
    }

    timestamp = clock_real_ms();
    for (i = 0; i < conf->step_id_and_dest_len; i++)
    {
        memset(&req, 0, sizeof(req));

        req.plan_id = conf->plan_id;
        req.step_id = atoi(conf->step_id_and_dest_keys[i]);
        req.dest_mac = conf->step_id_and_dest[i];
        req.timestamp = timestamp;
        req.sample_count = conf->blast_sample_count;
        req.duration = conf->blast_duration;
        req.packet_size = conf->blast_packet_size;
        req.cb = wbm_ovsdb_request_cb;

        for (   j = 0;
                (j < conf->blast_config_len)
                && (j < (int)(sizeof(req.config)/sizeof(req.config[0])));
                j++)
        {
            req.config[j].key = conf->blast_config_keys[j];
            req.config[j].value = conf->blast_config[j];
            LOGD("%s: Plan[%s] Step[%d] Mac[%s] Add config: [%s]key [%s]value", __func__,
                 req.plan_id, req.step_id, req.dest_mac, req.config[j].key, req.config[j].value);
        }

        if (wbm_engine_request_add(&req) != 0)
        {
            LOGE("%s: Plan[%s] Step[%d] Mac[%s] Failed to add engine request", __func__,
                 req.plan_id, req.step_id, req.dest_mac);
            continue;
        }

        succeed_cnt++;
    }

    wbm_ovsdb_state_table_create(conf->plan_id, "new");
    if (succeed_cnt == 0) {
        wbm_ovsdb_plan_complete(conf->plan_id);
        return -1;
    }

    return 0;
}

static int wbm_ovsdb_plan_cancel(struct schema_Wifi_Blaster_Config *conf)
{
    if (conf->plan_id[0] == '\0') {
        LOGW("%s: Plan ID is empty", __func__);
        return -1;
    }

    if (wbm_engine_plan_is_active(conf->plan_id)) {
        wbm_engine_plan_cancel(conf->plan_id);
    }

    return 0;
}

void callback_Wifi_Blaster_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Blaster_Config *old_rec,
        struct schema_Wifi_Blaster_Config *conf)
{
    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            LOGI("%s: Plan[%s] Add WBM config entry", __func__, conf->plan_id);
            wbm_ovsdb_plan_add(conf);
            break;
        case OVSDB_UPDATE_DEL:
            LOGI("%s: Plan[%s] Delete WBM config entry", __func__, old_rec->plan_id);
            wbm_ovsdb_plan_cancel(old_rec);
            break;
        case OVSDB_UPDATE_MODIFY:
            LOGW("%s: modifying of WBM config entry is not supported", __func__);
            break;
        default:
            return;
    }
}

int wbm_ovsdb_init(void)
{
    LOGI("Initializing WBM tables");

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(Wifi_Blaster_Config);
    OVSDB_TABLE_INIT_NO_KEY(Wifi_Blaster_State);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(Wifi_Blaster_Config, false);

    return 0;
}

void wbm_ovsdb_uninit(void)
{
    ovsdb_table_delete(&table_Wifi_Blaster_State, NULL);
    ovsdb_table_delete(&table_Wifi_Blaster_Config, NULL);
}
