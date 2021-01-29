#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "const.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "osn_types.h"
#include "qm_conn.h"
#include "log.h"

#include "wbm.pb-c.h"
#include "wbm_report.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

#define DBG_PRINT_MSG 0

typedef struct wbm_report_pb {
    size_t  len;   /* Length of the serialized protobuf */
    void    *buf;  /* Allocated pointer for serialized data */
} wbm_report_pb_t;

static ovsdb_table_t table_AWLAN_Node;
static char g_mqtt_topic[256];

static bool wbm_report_publish_mqtt(void *pb_report, size_t pb_len)
{
    qm_response_t res;
    bool ret;

    LOGI("%s: Publishing message with msg len: %zu, to topic: %s", __func__, pb_len, g_mqtt_topic);
    ret = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, g_mqtt_topic, pb_report, pb_len, &res);
    if (!ret) {
        LOGE("%s: Error sending message", __func__);
    }

    return ret;
}

static void wbm_report_device_struct_free(Wbm__WifiBlastResult__DeviceMetrics *wb_dm_pb)
{
    if (wb_dm_pb == NULL) {
        return;
    }

    free(wb_dm_pb->client_mac);
    free(wb_dm_pb->throughput_samples);
    free(wb_dm_pb->tx_packet_retransmissions);
    free(wb_dm_pb);
}

static void wbm_report_radio_struct_free(Wbm__WifiBlastResult__RadioMetrics *wb_rm_pb)
{
    free(wb_rm_pb);
}

static void wbm_report_health_struct_free(Wbm__WifiBlastResult__HealthMetrics *wb_hm_pb)
{
    if (wb_hm_pb == NULL) {
        return;
    }

    free(wb_hm_pb->load_avg);
    free(wb_hm_pb);
}

static wbm_report_status_t wbm_report_pb_send(wbm_report_pb_t *proto_buff)
{
    if (!qm_conn_get_status(NULL)) {
        LOGE("%s: Cannot connect to QM (QM not running?)", __func__);
        return WBM_REPORT_STATUS_ERR_CONN;
    }

    if (!wbm_report_publish_mqtt(proto_buff->buf, proto_buff->len)) {
        return WBM_REPORT_STATUS_ERR_SEND;
    }

    return WBM_REPORT_STATUS_SUCCESS;
}

static Wbm__WifiBlastResult__HealthMetrics__LoadAvg* wbm_report_load_avg_struct_create(
        wbm_node_t *node)
{
    Wbm__WifiBlastResult__HealthMetrics__LoadAvg *wb_hm_la_pb;

    wb_hm_la_pb = calloc(1, sizeof(*wb_hm_la_pb));
    if (wb_hm_la_pb == NULL) {
        LOGE("%s: Failed to allocate HealthMetrics__LoadAvg memory", __func__);
        return NULL;
    }

    wbm__wifi_blast_result__health_metrics__load_avg__init(wb_hm_la_pb);

    wb_hm_la_pb->one = node->result.health.cpu_util_1m;
    wb_hm_la_pb->has_one = true;
    wb_hm_la_pb->five = node->result.health.cpu_util_5m;
    wb_hm_la_pb->has_five = true;
    wb_hm_la_pb->fifteen = node->result.health.cpu_util_15m;
    wb_hm_la_pb->has_fifteen = true;

    return wb_hm_la_pb;
}

static Wbm__WifiBlastResult__HealthMetrics* wbm_report_health_struct_create(wbm_node_t *node)
{
    Wbm__WifiBlastResult__HealthMetrics *wb_hm_pb;

    wb_hm_pb = calloc(1, sizeof(*wb_hm_pb));
    if (wb_hm_pb == NULL) {
        LOGE("%s: Failed to allocate HealthMetrics memory", __func__);
        return NULL;
    }

    wbm__wifi_blast_result__health_metrics__init(wb_hm_pb);

    wb_hm_pb->cpu_util = node->result.health.cpu_util;
    wb_hm_pb->has_cpu_util = true;
    wb_hm_pb->mem_util = node->result.health.mem_free;
    wb_hm_pb->has_mem_util = true;
    wb_hm_pb->load_avg = wbm_report_load_avg_struct_create(node);
    if (wb_hm_pb->load_avg == NULL) {
        free(wb_hm_pb);
        return NULL;
    }

    return wb_hm_pb;
}

static Wbm__WiFiStandard wbm_report_wifi_standard_get(radio_protocols_t radio_proto)
{
    Wbm__WiFiStandard standard;

    switch (radio_proto)
    {
        case RADIO_802_11_A:
            standard = WBM__WI_FI_STANDARD__WIFI_STD_80211_A;
            break;
        case RADIO_802_11_BG:
            standard = WBM__WI_FI_STANDARD__WIFI_STD_80211_B;
            break;
        case RADIO_802_11_NG:
            standard = WBM__WI_FI_STANDARD__WIFI_STD_80211_N;
            break;
        case RADIO_802_11_AC:
            standard = WBM__WI_FI_STANDARD__WIFI_STD_80211_AC;
            break;
        default:
            standard = WBM__WI_FI_STANDARD__WIFI_STD_UNKNOWN;
            break;
    }

    return standard;
}

static Wbm__WifiBlastResult__RadioMetrics* wbm_report_radio_struct_create(wbm_node_t *node)
{
    Wbm__WifiBlastResult__RadioMetrics *wb_rm_pb;

    wb_rm_pb = calloc(1, sizeof(*wb_rm_pb));
    if (wb_rm_pb == NULL) {
        LOGE("%s: Failed to allocate RadioMetrics memory", __func__);
        return NULL;
    }

    wbm__wifi_blast_result__radio_metrics__init(wb_rm_pb);

    wb_rm_pb->activity_factor = node->result.radio.activity;
    wb_rm_pb->has_activity_factor = true;
    wb_rm_pb->carriersense_threshold_exceeded = node->result.radio.interf;
    wb_rm_pb->has_carriersense_threshold_exceeded = true;
    wb_rm_pb->noise_floor = node->result.radio.noise_floor;
    wb_rm_pb->has_noise_floor = true;
    wb_rm_pb->channel_utilization = node->result.radio.util;
    wb_rm_pb->has_channel_utilization = true;
    wb_rm_pb->channel = node->info.chan;
    wb_rm_pb->has_channel = true;
    wb_rm_pb->wifi_standard = wbm_report_wifi_standard_get(node->info.radio_proto);
    wb_rm_pb->has_wifi_standard = true;
    wb_rm_pb->chan_width = node->info.chanwidth;
    wb_rm_pb->has_chan_width = true;
    wb_rm_pb->radio_band = node->info.radio_type;
    wb_rm_pb->has_radio_band = true;

    return wb_rm_pb;
}

static int wbm_device_metrics_blast_sample_fill(
        Wbm__WifiBlastResult__DeviceMetrics *wm_dm_pb,
        wbm_node_t *node)
{
    int count;
    int num_of_samples = node->result.sample_cnt - 1;

    if (num_of_samples <= 0) {
        LOGE("%s: Invalid number of samples is received [%d]", __func__, num_of_samples);
        return -1;
    }

    wm_dm_pb->throughput_samples = calloc(num_of_samples, sizeof(*wm_dm_pb->throughput_samples));
    if (wm_dm_pb->throughput_samples == NULL) {
        LOGE("%s: Failed to allocate throughput_samples memory", __func__);
        return -1;
    }

    wm_dm_pb->tx_packet_retransmissions = calloc(num_of_samples,
        sizeof(*wm_dm_pb->tx_packet_retransmissions));
    if (wm_dm_pb->tx_packet_retransmissions == NULL)
    {
        LOGE("%s: Failed to allocate tx_packet_retransmissions memory", __func__);
        free(wm_dm_pb->throughput_samples);
        wm_dm_pb->throughput_samples = NULL;
        return -1;
    }

    wm_dm_pb->n_throughput_samples = num_of_samples;
    wm_dm_pb->n_tx_packet_retransmissions = num_of_samples;
    for (count = 0; count < num_of_samples; count++)
    {
        wm_dm_pb->throughput_samples[count] = node->result.sample[count + 1].throughput;
        wm_dm_pb->tx_packet_retransmissions[count] = node->result.sample[count + 1].tx_retrans;
    }

    return 0;
}

static char* wbm_report_raw_mac(char *str_mac)
{
    static char mac_addr[OSN_MAC_ADDR_LEN];

    snprintf(mac_addr, sizeof(mac_addr), "%c%c%c%c%c%c%c%c%c%c%c%c",
        str_mac[0], str_mac[1], str_mac[3], str_mac[4], str_mac[6], str_mac[7],
        str_mac[9], str_mac[10], str_mac[12], str_mac[13], str_mac[15], str_mac[16]);

    return mac_addr;
}

static Wbm__WifiBlastResult__DeviceMetrics* wbm_report_device_struct_create(wbm_node_t *node)
{
    Wbm__WifiBlastResult__DeviceMetrics *wm_dm_pb;
    char *mac_addr;

    wm_dm_pb = calloc(1, sizeof(*wm_dm_pb));
    if (wm_dm_pb == NULL) {
        LOGE("%s: Failed to allocate DeviceMetrics memory", __func__);
        return NULL;
    }

    wbm__wifi_blast_result__device_metrics__init(wm_dm_pb);

    mac_addr = node->dest_mac_orig_raw ? wbm_report_raw_mac(node->dest_mac) : node->dest_mac;
    wm_dm_pb->client_mac = strdup(mac_addr);
    if (wm_dm_pb->client_mac == NULL)
    {
        LOGE("%s: Failed to allocate Client MAC memory", __func__);
        free(wm_dm_pb);
        return NULL;
    }

    wm_dm_pb->rssi = node->result.client.rssi;
    wm_dm_pb->has_rssi = true;
    wm_dm_pb->rx_phyrate = node->result.client.rate_rx;
    wm_dm_pb->has_rx_phyrate = true;
    wm_dm_pb->tx_phyrate = node->result.client.rate_tx;
    wm_dm_pb->has_tx_phyrate = true;
    wm_dm_pb->snr = node->result.client.rssi - node->result.radio.noise_floor;
    wm_dm_pb->has_snr = true;

    // fill blast device details
    if (wbm_device_metrics_blast_sample_fill(wm_dm_pb, node) != 0)
    {
        free(wm_dm_pb);
        free(wm_dm_pb->client_mac);
        return NULL;
    }

    return wm_dm_pb;
}

static int wbm_report_metrics_struct_create(Wbm__WifiBlastResult *wb_res_pb, wbm_node_t *node)
{
    Wbm__WifiBlastResult__HealthMetrics *wb_hm_pb;
    Wbm__WifiBlastResult__RadioMetrics *wb_rm_pb;
    Wbm__WifiBlastResult__DeviceMetrics *wm_dm_pb;

    wb_hm_pb = wbm_report_health_struct_create(node);
    if (wb_hm_pb == NULL) {
        return -1;
    }

    wb_rm_pb = wbm_report_radio_struct_create(node);
    if (wb_rm_pb == NULL) {
        wbm_report_health_struct_free(wb_hm_pb);
        return -1;
    }

    wm_dm_pb = wbm_report_device_struct_create(node);
    if (wm_dm_pb == NULL)
    {
        wbm_report_health_struct_free(wb_hm_pb);
        wbm_report_radio_struct_free(wb_rm_pb);
        return -1;
    }

    wb_res_pb->health_metrics = wb_hm_pb;
    wb_res_pb->device_metrics = wm_dm_pb;
    wb_res_pb->radio_metrics = wb_rm_pb;

    return 0;
}

static Wbm__WifiBlastResult__Status* wbm_report_status_struct_create(wbm_node_t *node)
{
    Wbm__WifiBlastResult__Status *res_status;

    res_status = calloc(1, sizeof(*res_status));
    if (res_status == NULL) {
        LOGE("%s: Failed to allocate Status memory", __func__);
        return NULL;
    }

    wbm__wifi_blast_result__status__init(res_status);

    switch (node->status)
    {
        case WBM_STATUS_UNDEFINED:
            res_status->code = WBM__RESULT_CODE__RESULT_CODE_UNDEFINED;
            break;
        case WBM_STATUS_SUCCEED:
            res_status->code = WBM__RESULT_CODE__RESULT_CODE_SUCCEED;
            break;
        default:
            res_status->code = WBM__RESULT_CODE__RESULT_CODE_ERROR;
            break;
    }

    res_status->description = strdup(node->status_desc);
    if (res_status->description == NULL)
    {
        LOGE("%s: Failed to allocate Description memory", __func__);
        free(res_status);
        return NULL;
    }

    return res_status;
}

static void wbm_report_status_struct_free(Wbm__WifiBlastResult__Status *status)
{
    if (status == NULL) {
        return;
    }

    free(status->description);
    free(status);
}

static void wbm_report_pb_struct_free(Wbm__WifiBlastResult *wb_res_pb)
{
    if (wb_res_pb == NULL)
        return;

    wbm_report_device_struct_free(wb_res_pb->device_metrics);
    wbm_report_health_struct_free(wb_res_pb->health_metrics);
    wbm_report_radio_struct_free(wb_res_pb->radio_metrics);
    wbm_report_status_struct_free(wb_res_pb->status);
    free(wb_res_pb->plan_id);
    free(wb_res_pb);
}

static Wbm__WifiBlastResult* wbm_report_pb_struct_create(wbm_node_t *node)
{
    Wbm__WifiBlastResult *wb_res_pb;

    wb_res_pb = calloc(1, sizeof(*wb_res_pb));
    if (wb_res_pb == NULL) {
        LOGE("%s: Failed to allocate WifiBlastResult memory", __func__);
        return NULL;
    }

    wbm__wifi_blast_result__init(wb_res_pb);
    wb_res_pb->time_stamp = node->ts[WBM_TS_FINISHED];
    wb_res_pb->has_time_stamp = true;
    wb_res_pb->step_id = node->step_id;
    wb_res_pb->has_step_id = true;
    wb_res_pb->plan_id = strdup(node->plan->plan_id);
    if (wb_res_pb->plan_id == NULL)
    {
        LOGE("%s: Failed to strdup PlanID", __func__);
        free(wb_res_pb);
        return NULL;
    }

    if (node->status == WBM_STATUS_SUCCEED)
    {
        if (wbm_report_metrics_struct_create(wb_res_pb, node) != 0) {
            wbm_report_pb_struct_free(wb_res_pb);
            return NULL;
        }
    }

    wb_res_pb->status = wbm_report_status_struct_create(node);
    if (wb_res_pb->status == NULL) {
        wbm_report_pb_struct_free(wb_res_pb);
        return NULL;
    }

    return wb_res_pb;
}

static wbm_report_pb_t* wbm_report_pb_create(wbm_node_t *node)
{
    Wbm__WifiBlastResult *wb_res_pb;
    wbm_report_pb_t *report_pb;
    void *buf = NULL;

    report_pb = calloc(1, sizeof(*report_pb));
    if (report_pb == NULL) {
        LOGE("%s: Failed to allocate wbm_report_pb_t memory", __func__);
        return NULL;
    }

    wb_res_pb = wbm_report_pb_struct_create(node);
    if (wb_res_pb == NULL) {
        goto Error;
    }

    report_pb->len = wbm__wifi_blast_result__get_packed_size(wb_res_pb);
    if (report_pb->len == 0) {
        LOGE("%s: Invalid packed size for result buff", __func__);
        goto Error;
    }

    buf = calloc(1, report_pb->len);
    if (buf == NULL) {
        LOGE("%s: Failed to allocate buf memory", __func__);
        goto Error;
    }

    report_pb->len = wbm__wifi_blast_result__pack(wb_res_pb, buf);
    if (report_pb->len <= 0) {
        LOGE("%s: Failed to pack result protobuf! Length [%zu]", __func__, report_pb->len);
        goto Error;
    }
    report_pb->buf = buf;

    wbm_report_pb_struct_free(wb_res_pb);
    return report_pb;

Error:
    wbm_report_pb_struct_free(wb_res_pb);
    free(report_pb);
    free(buf);
    return NULL;
}

static void wbm_report_pb_free(wbm_report_pb_t *report_pb)
{
    if (report_pb == NULL)
        return;

    free(report_pb->buf);
    free(report_pb);
}

static void wbm_report_pb_print_dbg(wbm_report_pb_t* serialized_buff)
{
#if DBG_PRINT_MSG
    Wbm__WifiBlastResult *blast_res;
    Wbm__WifiBlastResult__HealthMetrics *h_metrics;
    Wbm__WifiBlastResult__HealthMetrics__LoadAvg *h_metrics_load_avg;
    Wbm__WifiBlastResult__RadioMetrics *r_metrics;
    Wbm__WifiBlastResult__DeviceMetrics *d_metrics;
    void *blast_res_buf = serialized_buff->buf;
    uint32_t count;
    uint64_t retrans_sum = 0;
    double throughput_sum = 0.0;
    c_item_t *item;
    char *chan_width;
    char *wifi_standard;
    char *radio_band;

    c_item_t map_wbm_chanwidth[] = {
        C_ITEM_STR( WBM__CHAN_WIDTH__CHAN_WIDTH_20MHZ,          "HT20" ),
        C_ITEM_STR( WBM__CHAN_WIDTH__CHAN_WIDTH_40MHZ,          "HT40" ),
        C_ITEM_STR( WBM__CHAN_WIDTH__CHAN_WIDTH_40MHZ_ABOVE,    "HT40+" ),
        C_ITEM_STR( WBM__CHAN_WIDTH__CHAN_WIDTH_40MHZ_BELOW,    "HT40-" ),
        C_ITEM_STR( WBM__CHAN_WIDTH__CHAN_WIDTH_80MHZ,          "HT80" ),
        C_ITEM_STR( WBM__CHAN_WIDTH__CHAN_WIDTH_160MHZ,         "HT160" ),
        C_ITEM_STR( WBM__CHAN_WIDTH__CHAN_WIDTH_80_PLUS_80MHZ,  "HT80+80" ),
        C_ITEM_STR( WBM__CHAN_WIDTH__CHAN_WIDTH_UNKNOWN,        "Unknown" )
    };

    c_item_t map_wbm_hwmode[] = {
        C_ITEM_STR( WBM__WI_FI_STANDARD__WIFI_STD_80211_A,      "11a" ),
        C_ITEM_STR( WBM__WI_FI_STANDARD__WIFI_STD_80211_B,      "11b" ),
        C_ITEM_STR( WBM__WI_FI_STANDARD__WIFI_STD_80211_G,      "11g" ),
        C_ITEM_STR( WBM__WI_FI_STANDARD__WIFI_STD_80211_N,      "11n" ),
        C_ITEM_STR( WBM__WI_FI_STANDARD__WIFI_STD_80211_AC,     "11ac"),
        C_ITEM_STR( WBM__WI_FI_STANDARD__WIFI_STD_UNKNOWN,      "unknown" ),
    };

    c_item_t map_wbm_radiotype[] = {
        C_ITEM_STR( WBM__RADIO_BAND_TYPE__BAND2G,               RADIO_TYPE_STR_2G ),
        C_ITEM_STR( WBM__RADIO_BAND_TYPE__BAND5G,               RADIO_TYPE_STR_5G ),
        C_ITEM_STR( WBM__RADIO_BAND_TYPE__BAND5GL,              RADIO_TYPE_STR_5GL ),
        C_ITEM_STR( WBM__RADIO_BAND_TYPE__BAND5GU,              RADIO_TYPE_STR_5GU ),
        C_ITEM_STR( WBM__RADIO_BAND_TYPE__BAND_UNKNOWN,         "Unknown" ),
    };

    blast_res = wbm__wifi_blast_result__unpack(NULL, serialized_buff->len,
        (const uint8_t *)blast_res_buf);
    if (blast_res == NULL) {
        LOGE("%s: Failed to unpack blast result", __func__);
        return;
    }

    h_metrics = blast_res->health_metrics;
    r_metrics = blast_res->radio_metrics;
    d_metrics = blast_res->device_metrics;

    LOGI("********** WiFi Blaster Test Protobuf Results **********");
    LOGI("Plan[%s] Step[%d] Finished_Time_Stamp[%"PRIu64"] Status[%d]",
         blast_res->plan_id, blast_res->step_id, blast_res->time_stamp, blast_res->status->code);
    LOGI("Desc: %s", blast_res->status->description);

    if (blast_res->status->code != WBM__RESULT_CODE__RESULT_CODE_SUCCEED) {
        goto Error;
    }

    if ((h_metrics != NULL) && (h_metrics->load_avg != NULL))
    {
        h_metrics_load_avg = h_metrics->load_avg;
        LOGI("Health: CPU_util[%u]%% Mem_free[%u]KB CPU_load_avg(1/5/15)[%0.2f/%0.2f/%0.2f]",
             h_metrics->cpu_util, h_metrics->mem_util,
             h_metrics_load_avg->one, h_metrics_load_avg->five, h_metrics_load_avg->fifteen);
    }

    if (r_metrics != NULL)
    {
        item = c_get_item_by_key(map_wbm_chanwidth, r_metrics->chan_width);
        chan_width = (char *)item->data;
        item = c_get_item_by_key(map_wbm_hwmode, r_metrics->wifi_standard);
        wifi_standard = (char *)item->data;
        item = c_get_item_by_key(map_wbm_radiotype, r_metrics->radio_band);
        radio_band = (char *)item->data;

        LOGI("Radio: Noise_floor[%d]db Channel_Util[%u]%% Activity_factor[%u]%% "
             "Carriersense_Threshold_Exceeded[%u]%%",
             r_metrics->noise_floor, r_metrics->channel_utilization, r_metrics->activity_factor,
             r_metrics->carriersense_threshold_exceeded);
        LOGI("   Channel[%u] Channel_Width[%s] Radio_band[%s] Wifi_Standard[%s]",
             r_metrics->channel, chan_width, radio_band, wifi_standard);
    }

    if (d_metrics != NULL)
    {
        LOGI("Device: Client_Mac[%s] RSSI[%d]db Tx_Phyrate[%u] Rx_Phyrate[%u] SNR[%d]",
             d_metrics->client_mac, d_metrics->rssi, d_metrics->tx_phyrate, d_metrics->rx_phyrate,
             d_metrics->snr);

        for (count = 0; count < d_metrics->n_throughput_samples; count++)
        {
            LOGI("Sample[%d] Throughput[%f]Mbps, TxRetrans[%"PRIu64"]",
                 count + 1, d_metrics->throughput_samples[count],
                 d_metrics->tx_packet_retransmissions[count]);
            retrans_sum += d_metrics->tx_packet_retransmissions[count];
            throughput_sum += d_metrics->throughput_samples[count];
        }
        LOGI("Average throughput[%f]Mbps. Summ of retransmissions[%"PRIu64"]",
            throughput_sum / d_metrics->n_throughput_samples, retrans_sum);
    }

Error:
    LOGI("***********************************************");

    wbm__wifi_blast_result__free_unpacked(blast_res, NULL);
#endif /* DBG_PRINT_MSG */
}

static char* wbm_report_mqtt_topic_get(struct schema_AWLAN_Node *awlan)
{
    int map_cnt;

    for (map_cnt = 0; map_cnt < awlan->mqtt_topics_len; map_cnt++)
    {
        if (strcmp(awlan->mqtt_topics_keys[map_cnt], "WifiBlaster.Results") == 0) {
            return awlan->mqtt_topics[map_cnt];
        }
    }

    return NULL;
}

static int wbm_report_mqtt_topic_update(void)
{
    json_t *jrows;
    struct schema_AWLAN_Node awlan;
    pjs_errmsg_t perr;
    char *topic_name;
    int res = -1;

    memset(g_mqtt_topic, 0, sizeof(g_mqtt_topic));

    jrows = ovsdb_sync_select_where(SCHEMA_TABLE(AWLAN_Node), NULL);
    if ((jrows == NULL) || (json_array_size(jrows) == 0)) {
        LOGE("%s: Failed to get AWLAN_Node json",__func__);
        goto Exit;
    }

    if (!schema_AWLAN_Node_from_json(&awlan, json_array_get(jrows, 0), false, perr)) {
        LOGE("%s: Failed to get AWLAN_Node schema",__func__);
        goto Exit;
    }

    topic_name = wbm_report_mqtt_topic_get(&awlan);
    if ((topic_name == NULL) || (strlen(topic_name) == 0))
    {
        LOGW("%s: MQTT topic name is empty", __func__);
        res = 0;
        goto Exit;
    }

    STRSCPY(g_mqtt_topic, topic_name);
    LOGD("%s: MQTT topic name is [%s]", __func__, g_mqtt_topic);

    res = 0;
Exit:
    if (jrows != NULL) {
        json_decref(jrows);
    }
    return res;
}

static void callback_AWLAN_Node(
        ovsdb_update_monitor_t *mon,
        struct schema_AWLAN_Node *old_rec,
        struct schema_AWLAN_Node *awlan)
{
    char *topic_name;

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        LOGD("%s: MQTT topic has been removed", __func__);
        memset(g_mqtt_topic, 0, sizeof(g_mqtt_topic));
        return;
    }

    topic_name = wbm_report_mqtt_topic_get(awlan);
    if ((topic_name == NULL) || (strlen(topic_name) == 0))
    {
        LOGD("%s: MQTT topic name is empty", __func__);
        if (g_mqtt_topic[0] != '\0') {
            memset(g_mqtt_topic, 0, sizeof(g_mqtt_topic));
        }
        return;
    }

    STRSCPY(g_mqtt_topic, topic_name);
    LOGD("%s: MQTT topic name is [%s]", __func__, g_mqtt_topic);
}

int wbm_report_init(void)
{
    char *filter[] = { "+", SCHEMA_COLUMN(AWLAN_Node, mqtt_topics), NULL };

    if (wbm_report_mqtt_topic_update() != 0) {
        return -1;
    }

    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
    OVSDB_TABLE_MONITOR_F(AWLAN_Node, filter);
    return 0;
}

wbm_report_status_t wbm_report_publish(wbm_node_t *node)
{
    wbm_report_status_t status;
    wbm_report_pb_t *report_pb;

    if (g_mqtt_topic[0] == '\0') {
        LOGE("%s: MQTT topic name is empty. Failed to send the report", __func__);
        return WBM_REPORT_STATUS_ERR_INTERNAL;
    }

    if (node == NULL) {
        LOGE("%s: Invalid input parameters", __func__);
        return WBM_REPORT_STATUS_ERR_INPUT;
    }

    report_pb = wbm_report_pb_create(node);
    if (report_pb == NULL) {
        return WBM_REPORT_STATUS_ERR_INTERNAL;
    }

    status = wbm_report_pb_send(report_pb);
    if (status == WBM_REPORT_STATUS_SUCCESS) {
        wbm_report_pb_print_dbg(report_pb);
    }

    wbm_report_pb_free(report_pb);
    return status;
}
