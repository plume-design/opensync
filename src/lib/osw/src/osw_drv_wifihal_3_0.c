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

/* libc */
#include <pthread.h>
#include <string.h>
#include <inttypes.h>
#include <dlfcn.h>

/* opensync */
#include <const.h>
#include <ds_tree.h>
#include <log.h>
#include <os.h>
#include <util.h>
#include <memutil.h>
#include <os_time.h>
#include <osw_drv.h>
#include <osw_drv_common.h>
#include <osw_conf.h>
#include <osw_etc.h>

/* 3rd party */
#include <ev.h>

#ifndef __WIFI_HAL_H__
#include "ccsp/wifi_hal.h"
#endif

#define MAC_LEN 6
#define DRV_NAME "wifihal_3_0"

/* FIXME: Measure all WifiHAL call timings and report if they take too long. */
#define MEASURE_MAX 500
#define MEASURE(fn, args) \
({ \
    int64_t a = clock_mono_ms(); \
    typeof(fn args) __x = fn args; \
    int64_t b = clock_mono_ms(); \
    int64_t c = b - a; \
    if (c >= MEASURE_MAX) \
        LOGW("%sL%d@%s: [%s] took too long, %"PRIu64" > %d", __FILE__, __LINE__, __func__, #fn, c, MEASURE_MAX); \
    __x; \
})

#define STEERING_EVENT_QUEUE_CAPACITY 256
#define MAX_IES_LEN 4096

INT wifihal_stub() /* () means any argument list is valid */
{
    return RETURN_ERR;
}

struct wifihal_3_0_sta_id {
    UINT ap_index;
    struct osw_hwaddr addr;
};

struct wifihal_3_0_sta {
    struct ds_tree_node node;
    struct wifihal_3_0_sta_id id;
    uint8_t ies[MAX_IES_LEN];
    size_t ies_len;
    struct {
        bool state;
        bool ies;
    } changed;
};

struct wifihal_3_0_steering_event {
    struct ds_dlist_node node;
    wifi_steering_event_t hal_event;
};

struct wifihal_3_0_priv {
    struct ev_loop *loop;
    struct osw_drv *drv;
    struct osw_conf_mutator conf_mut;
    pthread_mutex_t lock;
    pthread_mutex_t lock_steering;
    pthread_cond_t cond_steering;
    ev_async async;
    ev_async async_steering;
    struct ds_tree sta_tree;
    struct ds_dlist steering_events_queue;
    size_t steering_events_queue_len;
    int n_events;

    INT (*wifi_getRadioIfName)(INT radioIndex, CHAR *output_string);
    INT (*wifi_getApAssociatedDeviceDiagnosticResult3)(INT apIndex, wifi_associated_dev3_t **associated_dev_array, UINT *output_array_size);
    INT (*wifi_getRadioDfsSupport)(INT radioIndex, BOOL *output_bool);
    INT (*wifi_getRadioDfsEnable)(INT radioIndex, BOOL *output_bool);
    INT (*wifi_getRadioChannels)(INT radioIndex, wifi_channelMap_t *output_map, INT output_map_size);
    INT (*wifi_getApAclDeviceNum)(INT apIndex, UINT *output_uint);
    INT (*wifi_getApAclDevices)(INT apIndex, CHAR *macArray, UINT buf_size);
    INT (*wifi_getApAclDevices2)(INT apIndex, mac_address_t *macArray, UINT maxArraySize, UINT* output_numEntries);
    INT (*wifi_addApAclDevice)(INT apIndex, CHAR *DeviceMacAddress);
    INT (*wifi_addApAclDevice2)(INT apIndex, mac_address_t DeviceMacAddress);
    INT (*wifi_delApAclDevices)(INT apINdex);
    INT (*wifi_steering_clientDisconnect)(UINT steeringgroupIndex, INT apIndex, mac_address_t client_mac, wifi_disconnectType_t type, UINT reason);
    INT (*wifi_pushRadioChannel2)(INT radioIndex, UINT channel, UINT channel_width_MHz, UINT csa_beacon_count);
    INT (*wifi_createVAP)(wifi_radio_index_t index, wifi_vap_info_map_t *map);
    INT (*wifi_setRadioOperatingParameters)(wifi_radio_index_t index, wifi_radio_operationParam_t *operationParam);
    INT (*wifi_setRadioDfsEnable)(INT radioIndex, BOOL enabled);
    INT (*wifi_getRadioOperatingParameters)(wifi_radio_index_t index, wifi_radio_operationParam_t *operationParam);
    INT (*wifi_getRadioVapInfoMap)(wifi_radio_index_t index, wifi_vap_info_map_t *map);
    INT (*wifi_pushMultiPskKeys)(INT apIndex, wifi_key_multi_psk_t *keys, INT keysNumber);
    INT (*wifi_getMultiPskKeys)(INT apIndex, wifi_key_multi_psk_t *keys, INT keysNumber);
    INT (*wifi_getMultiPskClientKey)(INT apIndex, mac_address_t mac, wifi_key_multi_psk_t *key);
    INT (*wifi_getHalVersion)(CHAR *output_string);
    INT (*wifi_getRadioChannelStats)(INT radioIndex, wifi_channelStats_t *input_output_channelStats_array, INT array_size);
    INT (*wifi_getNeighboringWiFiStatus)(INT radioIndex, wifi_neighbor_ap2_t **neighbor_ap_array, UINT *output_array_size);
    // INT wifi_getNeighboringWiFiStatus(INT radioIndex, BOOL scan, wifi_neighbor_ap2_t **neighbor_ap_array, UINT *output_array_size);
    INT (*wifi_steering_eventRegister)(wifi_steering_eventCB_t event_cb);
    INT (*wifi_sendActionFrame)(INT apIndex, mac_address_t sta, UINT frequency, UCHAR *frame, UINT len);
    INT (*wifi_mgmt_frame_callbacks_register)(wifi_receivedMgmtFrame_callback mgmtRxCallback);
    void (*wifi_newApAssociatedDevice_callback_register)(wifi_newApAssociatedDevice_callback callback_proc);
    void (*wifi_apDisassociatedDevice_callback_register)(wifi_apDisassociatedDevice_callback callback_proc);
    void (*wifi_apDeAuthEvent_callback_register)(wifi_apDeAuthEvent_callback callback_proc);

    /* FIXME: This structure could be used to keep a local
     * working copy of vap map for easier lookups as well as
     * more robust and async reconfig arch.
     */
};

#define DLSYM(fn, stub) \
    do { \
        priv->wifi_##fn = (void *)dlsym(NULL, "wifi_hal_"#fn) ?: ((void *)dlsym(NULL, "wifi_"#fn) ?: (void *)stub); \
        LOGI("%s: %s = %p (%s)", __func__, \
             #fn, \
             priv->wifi_##fn, \
             (priv->wifi_##fn == NULL \
              ? "null" \
              : ((void *)priv->wifi_##fn == (void *)stub ? "stub" : "real"))); \
    } while (0)

#define DLSYM2(fn, stub) \
    do { \
        priv->wifi_##fn##2 = (void *)dlsym(NULL, "wifi_hal_"#fn) ?: ((void *)dlsym(NULL, "wifi_"#fn) ?: (void *)stub); \
        LOGI("%s: %s2 = %p (%s)", __func__, \
             #fn, \
             priv->wifi_##fn, \
             (priv->wifi_##fn == NULL \
              ? "null" \
              : ((void *)priv->wifi_##fn == (void *)stub ? "stub" : "real"))); \
    } while (0)

typedef void sta_walk_fn_t(struct wifihal_3_0_sta *sta, void *fn_priv);
typedef bool vap_walk_fn_t(wifi_vap_info_t *vap, void *fn_priv);

static int
sta_id_cmp(const void *a, const void *b)
{
    const struct wifihal_3_0_sta_id *x = a;
    const struct wifihal_3_0_sta_id *y = b;
    return memcmp(x, y, sizeof(*x));
}

static bool
drv_is_enabled(void)
{
    if (osw_etc_get("OSW_DRV_WIFIHAL_3_0_DISABLED") == NULL)
        return true;
    else
        return false;
}

static bool
is_phase2(void)
{
    /* FIXME: This should be runtime detected. There doesn't
     * seem to be a way to do that now though, so fingers
     * crossed this is enough for now.
     */
#ifdef WIFI_HAL_VERSION_3_PHASE2
    return true;
#else
    return false;
#endif
}

static bool
phy_name_to_radio_index(const char *phy_name,
                        wifi_radio_index_t *radio_index);

static void
osw_conf_mutate_errata_beacon_interval(struct wifihal_3_0_priv *priv,
                                       struct ds_tree *phy_tree)
{
    /* FIXME: This should only be applied conditionally on
     * platforms where Wifi HAL implementation isn't
     * properly respecting setting beacon interval.
     */

    struct osw_conf_phy *phy;
    ds_tree_foreach(phy_tree, phy) {
        wifi_radio_index_t rix;

        if (phy_name_to_radio_index(phy->phy_name, &rix) == false)
            continue;

        wifi_radio_operationParam_t params = {0};
        if (MEASURE(priv->wifi_getRadioOperatingParameters, (rix, &params)) != RETURN_OK)
            continue;

        struct osw_conf_vif *vif;
        ds_tree_foreach(&phy->vif_tree, vif) {
            if (vif->vif_type != OSW_VIF_AP)
                continue;
            if (vif->u.ap.beacon_interval_tu == (int)params.beaconInterval)
                continue;

            LOGI("osw: drv: wifihal: %s/%s: overriding beacon interval %d -> %u, errata",
                    phy->phy_name, vif->vif_name,
                    vif->u.ap.beacon_interval_tu,
                    params.beaconInterval);

            vif->u.ap.beacon_interval_tu = params.beaconInterval;
        }
    }
}

static void
osw_conf_mutate_errata_2ghz_vht(struct wifihal_3_0_priv *priv,
                                struct ds_tree *phy_tree)
{
    struct osw_conf_phy *phy;
    ds_tree_foreach(phy_tree, phy) {
        wifi_radio_index_t rix;

        if (phy_name_to_radio_index(phy->phy_name, &rix) == false)
            continue;

        /* rix isn't used. The check is only to verify
         * the phy in question belongs to Wifi HAL.
         */

        struct osw_conf_vif *vif;
        ds_tree_foreach(&phy->vif_tree, vif) {
            if (vif->vif_type != OSW_VIF_AP)
                continue;

            struct osw_conf_vif_ap *ap = &vif->u.ap;
            const int freq = ap->channel.control_freq_mhz;
            const enum osw_band band = osw_freq_to_band(freq);

            /* FIXME: 11ac is not allowed on 2.4G by the
             * vanilla spec but vendors have extensions to do that
             * anyway. Setting 11ac alone (without 11ax) variant to
             * Wifi HAL works as expected, but setting both 11ax and
             * 11ac flags ends up with 11ax being reported back, but
             * not the 11ac one. OSW provides setting of these
             * independently and currently, eg. ow_ovsdb will set 11ac
             * always if 11ax is set (since it doesn't know any
             * better, and assumes 11ac on 2.4G implies vendor
             * specific 11ac mode).
             *
             * This should be addressed by allowing OSW drivers to
             * advertise capabilities so that osw_confsync can make an
             * educated decision on what to do with a setup like this.
             *
             * This override makes sure that only a valid config will
             * be generated as far as Wifi HAL goes.
             */

            if (ap->mode.vht_enabled == true &&
                ap->mode.he_enabled == true &&
                band == OSW_BAND_2GHZ) {
                LOGI("osw: drv: wifihal: %s/%s: overriding vht_enabled to false, errata",
                     phy->phy_name, vif->vif_name);
                ap->mode.vht_enabled = false;
            }
        }
    }
}

static void
osw_conf_mutate_cb(struct osw_conf_mutator *mut,
                   struct ds_tree *phy_tree)
{
    struct wifihal_3_0_priv *priv;
    priv = container_of(mut, struct wifihal_3_0_priv, conf_mut);

    osw_conf_mutate_errata_beacon_interval(priv, phy_tree);
    osw_conf_mutate_errata_2ghz_vht(priv, phy_tree);
}

static struct wifihal_3_0_priv g_priv = {
    .sta_tree = DS_TREE_INIT(sta_id_cmp, struct wifihal_3_0_sta, node),
    .steering_events_queue = DS_DLIST_INIT(struct wifihal_3_0_steering_event, node),
    .conf_mut = {
        .name = DRV_NAME,
        .mutate_fn = osw_conf_mutate_cb,
    },
};

static struct wifihal_3_0_sta *
sta_get(struct ds_tree *tree, UINT ap_index, const struct osw_hwaddr *addr)
{
    struct wifihal_3_0_sta_id id = {0};
    id.ap_index = ap_index;
    id.addr = *addr;

    struct wifihal_3_0_sta *sta = ds_tree_find(tree, &id);
    if (sta == NULL) {
        sta = CALLOC(1, sizeof(*sta));
        sta->id = id;
        ds_tree_insert(tree, sta, &sta->id);
    }

    return sta;
}

static bool
sta_touch(struct wifihal_3_0_priv *priv,
          UINT ap_index,
          const struct osw_hwaddr *addr)
{
    struct ds_tree *tree = &priv->sta_tree;
    struct ev_loop *loop = priv->loop;
    ev_async *async = &priv->async;
    pthread_mutex_t *lock = &priv->lock;

    pthread_mutex_lock(lock);
    struct wifihal_3_0_sta *sta = sta_get(tree, ap_index, addr);
    if (sta != NULL) sta->changed.state = true;
    pthread_mutex_unlock(lock);

    if (sta == NULL)
        return false;

    ev_async_send(loop, async);
    return true;
}

static void
sta_walk(struct wifihal_3_0_priv *priv, sta_walk_fn_t *fn, void *fn_priv)
{
    struct ds_tree *tree = &priv->sta_tree;
    pthread_mutex_t *lock = &priv->lock;

    for (;;) {
        pthread_mutex_lock(lock);
        struct wifihal_3_0_sta *sta = ds_tree_head(tree);
        if (sta != NULL)
            ds_tree_remove(tree, sta);
        pthread_mutex_unlock(lock);

        if (sta != NULL)
            fn(sta, fn_priv);

        FREE(sta);
        if (sta == NULL)
            break;
    }
}

static void
vap_walk_buf(wifi_vap_info_map_t *map,
             vap_walk_fn_t *fn,
             void *fn_priv)
{
    if (WARN_ON(map->num_vaps > ARRAY_SIZE(map->vap_array))) return;

    unsigned int i;
    for (i = 0; i < map->num_vaps; i++) {
        wifi_vap_info_t *vap = &map->vap_array[i];
        size_t max = sizeof(vap->vap_name);

        if (WARN_ON(strnlen(vap->vap_name, max) >= max)) continue;
        if (WARN_ON(strnlen(vap->vap_name, max) == 0)) continue;

        bool done = fn(vap, fn_priv);
        if (done == true)
            return;
    }
}

static void
vap_walk_radio(const struct wifihal_3_0_priv *priv,
               int radio_index,
               vap_walk_fn_t *fn,
               void *fn_priv)
{
    wifi_vap_info_map_t map = {0};
    if (MEASURE(priv->wifi_getRadioVapInfoMap, (radio_index, &map)) != RETURN_OK) return;
    vap_walk_buf(&map, fn, fn_priv);
}

static void
vap_walk(const struct wifihal_3_0_priv *priv,
         vap_walk_fn_t *fn,
         void *fn_priv)
{
    unsigned int i;
    for (i = 0; i < MAX_NUM_RADIOS; i++)
        vap_walk_radio(priv, i, fn, fn_priv);
}

static bool
vap_walk_index_to_info_cb(wifi_vap_info_t *vap, void *fn_priv)
{
    void **p = fn_priv;
    const wifi_vap_index_t *ap_index = p[0];
    if (vap->vap_index != *ap_index) return false;

    memcpy(p[1], vap, sizeof(wifi_vap_info_t));
    return true;
}

static bool
vap_walk_name_to_info_cb(wifi_vap_info_t *vap, void *fn_priv)
{
    void **p = fn_priv;
    const char *vif_name = p[0];
    if (strcmp(vap->vap_name, vif_name) != 0) return false;

    memcpy(p[1], vap, sizeof(wifi_vap_info_t));
    return true;
}

static char *
ap_index_to_phy_name(const struct wifihal_3_0_priv *priv,
                     UINT ap_index)
{
    wifi_vap_info_t vap_arg;
    vap_arg.radio_index = MAX_NUM_RADIOS;

    void *p[2] = { &ap_index, &vap_arg };
    vap_walk(priv, vap_walk_index_to_info_cb, p);

    const wifi_vap_info_t *vap = p[1];
    if (vap->radio_index == MAX_NUM_RADIOS) return NULL;

    char buf[MAXIFACENAMESIZE];
    if (MEASURE(priv->wifi_getRadioIfName, (vap->radio_index, buf)) != RETURN_OK)
        return NULL;

    buf[MAXIFACENAMESIZE - 1] = 0;
    if (WARN_ON(strlen(buf) == 0))
        return NULL;

    return STRDUP(buf);
}

static char *
ap_index_to_vif_name(const struct wifihal_3_0_priv *priv,
                     UINT ap_index)
{
    unsigned int i;
    for (i = 0; i < MAX_NUM_RADIOS; i++) {
        wifi_vap_info_map_t map = {0};
        if (MEASURE(priv->wifi_getRadioVapInfoMap, (i, &map)) != RETURN_OK) continue;
        unsigned int j;
        for (j = 0; j < map.num_vaps; j++) {
            const wifi_vap_info_t *vap = &map.vap_array[j];
            if (vap->vap_index == ap_index) return STRDUP(vap->vap_name);
        }
    }
    return NULL;
}

int
vif_name_to_ap_index(const struct wifihal_3_0_priv *priv,
                     const char *vif_name)
{
    wifi_vap_info_t vap_arg;
    vap_arg.radio_index = MAX_NUM_RADIOS;

    const void *p[2] = { vif_name, &vap_arg };
    vap_walk(priv, vap_walk_name_to_info_cb, p);

    const wifi_vap_info_t *vap = p[1];
    if (vap->radio_index == MAX_NUM_RADIOS) return -1;
    return vap->vap_index;
}

static bool
vap_walk_rix_name_to_info(const struct wifihal_3_0_priv *priv,
                          int rix,
                          const char *vif_name,
                          wifi_vap_info_t *vap)
{
    vap->radio_index = MAX_NUM_RADIOS;

    const void *p[2] = { vif_name, vap };
    vap_walk_radio(priv, rix, vap_walk_name_to_info_cb, p);

    if ( vap->radio_index == MAX_NUM_RADIOS) return false;

    return true;
}

static void
sta_report_cb(struct wifihal_3_0_sta *sta, void *fn_priv)
{
    struct wifihal_3_0_priv *priv = fn_priv;
    struct osw_drv *drv = priv->drv;
    const int ap_index = sta->id.ap_index;
    char *phy_name = ap_index_to_phy_name(priv, ap_index);
    char *vif_name = ap_index_to_vif_name(priv, ap_index);

    if (phy_name == NULL) {
        LOGW("%s: failed to get phy_name for ap_index = %d",
             __func__, ap_index);
    }

    if (vif_name == NULL) {
        LOGW("%s: failed to get vif_name for ap_index = %d",
             __func__, ap_index);
    }

    if (phy_name != NULL && vif_name != NULL) {
        if (sta->changed.state == true) {
            sta->changed.state = false;
            osw_drv_report_sta_changed(drv, phy_name, vif_name, &sta->id.addr);
        }

        if (sta->changed.ies == true) {
            sta->changed.ies = false;
            osw_drv_report_sta_assoc_ies(drv,
                                         phy_name,
                                         vif_name,
                                         &sta->id.addr,
                                         sta->ies,
                                         sta->ies_len);
        }
    }

    FREE(phy_name);
    FREE(vif_name);
}


static bool
radio_index_is_phy_name(int radio_index, const char *phy_name)
{
    const struct wifihal_3_0_priv *priv = &g_priv;
    char buf[MAXIFACENAMESIZE] = {0};
    if (MEASURE(priv->wifi_getRadioIfName, (radio_index, buf)) != RETURN_OK)
        return false;
    if (strncmp(phy_name, buf, sizeof(buf)) != 0)
        return false;
    return true;
}

static bool
phy_name_to_radio_index(const char *phy_name,
                        wifi_radio_index_t *radio_index)
{
    const struct wifihal_3_0_priv *priv = &g_priv;
    int i;

    for (i = 0; i < MAX_NUM_RADIOS; i++) {
        char radio_ifname[MAXIFACENAMESIZE] = {0};
        const size_t max = sizeof(radio_ifname);

        if (MEASURE(priv->wifi_getRadioIfName, (i, radio_ifname)) != RETURN_OK)
            return false;

        if (strncmp(phy_name, radio_ifname, max) == 0) {
            *radio_index = (wifi_radio_index_t)i;
            return true;
        }
    }

    return false;
}

static INT
sta_connect_cb(INT ap_index, wifi_associated_dev_t *dev)
{
    const struct osw_hwaddr *addr = (const void *)dev->cli_MACAddress;
    char *vif_name = ap_index_to_vif_name(&g_priv, ap_index);
    LOGI("osw: drv: wifihal: %s: " OSW_HWADDR_FMT ": connected: rssi=%d snr=%d",
         vif_name, OSW_HWADDR_ARG(addr), dev->cli_RSSI, dev->cli_SNR);
    FREE(vif_name);
    return sta_touch(&g_priv, ap_index, addr);
}

static INT
sta_disconnect_cb(INT ap_index, char *mac, INT event_type)
{
    struct osw_hwaddr addr = {0};
    char *vif_name = ap_index_to_vif_name(&g_priv, ap_index);
    sscanf(mac, OSW_HWADDR_FMT, OSW_HWADDR_SARG(&addr));
    /* FIXME: Store event_type */
    LOGI("osw: drv: wifihal: %s: " OSW_HWADDR_FMT ": disconnected: type=%d",
         vif_name, OSW_HWADDR_ARG(&addr), event_type);
    FREE(vif_name);
    return sta_touch(&g_priv, ap_index, &addr);
}

static INT
sta_deauth_cb(int ap_index, char *mac, int reason)
{
    struct osw_hwaddr addr = {0};
    char *vif_name = ap_index_to_vif_name(&g_priv, ap_index);
    sscanf(mac, OSW_HWADDR_FMT, OSW_HWADDR_SARG(&addr));
    LOGI("osw: drv: wifihal: %s: " OSW_HWADDR_FMT ": deauthenticated: reason=%d",
         vif_name, OSW_HWADDR_ARG(&addr), reason);
    FREE(vif_name);
    /* FIXME: Store reason */
    return sta_touch(&g_priv, ap_index, &addr);
}

static void steering_event_cb(UINT steeringgroupIndex, wifi_steering_event_t *wifi_hal_event)
{
    struct wifihal_3_0_steering_event *event = NULL;
    struct wifihal_3_0_priv *priv = &g_priv;
    struct ds_dlist *queue = &priv->steering_events_queue;
    pthread_mutex_t *lock = &priv->lock_steering;
    pthread_cond_t *cond = &priv->cond_steering;

    pthread_mutex_lock(lock);
    while (priv->steering_events_queue_len >= STEERING_EVENT_QUEUE_CAPACITY)
    {
        LOGN("%s: queue is full. Waiting (event: %d)", __func__, wifi_hal_event->type);
        pthread_cond_wait(cond, lock);
    }

    event = CALLOC(1, sizeof(*event));
    memcpy(&event->hal_event, wifi_hal_event, sizeof(event->hal_event));

    ds_dlist_insert_tail(queue, event);
    priv->steering_events_queue_len++;
    pthread_mutex_unlock(lock);

    ev_async_send(priv->loop, &priv->async_steering);
}

static void
steering_report_event(struct wifihal_3_0_priv *priv)
{
    struct wifihal_3_0_steering_event *event = NULL;
    struct osw_drv_report_vif_probe_req probe_req;
    MEMZERO(probe_req);
    struct ds_dlist *queue = &priv->steering_events_queue;
    pthread_mutex_t *lock = &priv->lock_steering;
    pthread_cond_t *cond = &priv->cond_steering;
    char *phy_name;
    char *vif_name;
    unsigned int events_counter = 0;

    pthread_mutex_lock(lock);
    while (ds_dlist_is_empty(queue) == false)
    {
        event = ds_dlist_remove_head(queue);
        priv->steering_events_queue_len--;
        events_counter++;

        phy_name = ap_index_to_phy_name(priv, event->hal_event.apIndex);
        vif_name = ap_index_to_vif_name(priv, event->hal_event.apIndex);

        if (WARN_ON(phy_name == NULL)) goto free;
        if (WARN_ON(vif_name == NULL)) goto free;

        switch (event->hal_event.type)
        {
            case WIFI_STEERING_EVENT_PROBE_REQ:
                memcpy(&probe_req.sta_addr.octet, event->hal_event.data.probeReq.client_mac,
                        sizeof(probe_req.sta_addr.octet));
                probe_req.snr = event->hal_event.data.probeReq.rssi;
                osw_drv_report_vif_probe_req(priv->drv, phy_name, vif_name, &probe_req);
                break;
            default:
                LOGN("Unsupported steering event: %d", event->hal_event.type);
                break;
        }

free:
        FREE(event);
    }
    pthread_cond_signal(cond);
    pthread_mutex_unlock(lock);

    LOGT("%s: processed %u events", __func__, events_counter);
}

static void
sta_async_cb(EV_P_ ev_async *arg, int events)
{
    struct wifihal_3_0_priv *priv;
    priv = container_of(arg, struct wifihal_3_0_priv, async);
    sta_walk(priv, sta_report_cb, priv);
}

static void
steering_async_cb(EV_P_ ev_async *arg, int events)
{
    struct wifihal_3_0_priv *priv;
    priv = container_of(arg, struct wifihal_3_0_priv, async_steering);
    steering_report_event(priv);
}

static INT
received_mgmt_frame_cb(INT apIndex,
                       mac_address_t sta_mac,
                       UCHAR *frame,
                       UINT len,
                       wifi_mgmtFrameType_t type,
                       wifi_direction_t dir)
{
    struct wifihal_3_0_priv *priv = &g_priv;
    struct ds_tree *tree = &priv->sta_tree;
    struct ev_loop *loop = priv->loop;
    ev_async *async = &priv->async;
    pthread_mutex_t *lock = &priv->lock;
    INT ret = RETURN_OK;

    pthread_mutex_lock(lock);
    struct wifihal_3_0_sta *sta = sta_get(tree, apIndex, (const struct osw_hwaddr *)sta_mac);
    if (sta == NULL)
        goto unlock;

    if (type == WIFI_MGMT_FRAME_TYPE_ASSOC_REQ ||
        type == WIFI_MGMT_FRAME_TYPE_REASSOC_REQ) {
        if (len > MAX_IES_LEN) {
            LOGE("%s: received mgmt frame size too big (%u vs %u)", __func__,
                 len, MAX_IES_LEN);
            ret = RETURN_ERR;
            goto unlock;
        }

        /* FIXME: It's unclear, from the Wifi HAL
         * API documentation, whether the frame
         * pointer points to 802.11 header, or the
         * fixed payload, or the variable payload
         * of the frame.
         *
         * This assumes it points to 802.11 frame
         * for now. If that is proven to be
         * incorrect, it can be fixed. This needs
         * testing.
         */
        const size_t hdr_len = (2 + 2 + 6 + 6 + 6 + 2);
        const size_t assoc_len = 2 + 2;
        const size_t reassoc_len = 2 + 2 + 6;
        const size_t fixed_len = (type == WIFI_MGMT_FRAME_TYPE_ASSOC_REQ)
                               ? assoc_len
                               : reassoc_len;
        const size_t ies_off = hdr_len + fixed_len;
        const size_t ies_len = len - ies_off;
        const void *ies = (frame + ies_off);

        memcpy(&sta->ies, ies, ies_len);
        sta->ies_len = ies_len;
        sta->changed.ies = true;
        ev_async_send(loop, async);
    }
unlock:
    pthread_mutex_unlock(lock);

    return ret;
}

static void
osw_drv_init_cb(struct osw_drv *drv)
{
    struct wifihal_3_0_priv *priv = &g_priv;

    if (drv_is_enabled() == false) return;

    priv->loop = EV_DEFAULT;
    priv->drv = drv;
    pthread_mutex_init(&priv->lock, NULL);
    osw_drv_set_priv(drv, priv);

    ev_async_init(&priv->async, sta_async_cb);
    ev_async_start(priv->loop, &priv->async);
    priv->steering_events_queue_len = 0;
    ev_async_init(&priv->async_steering, steering_async_cb);
    ev_async_start(priv->loop, &priv->async_steering);
    ev_unref(EV_DEFAULT);

    DLSYM(getRadioIfName, wifihal_stub);
    DLSYM(getApAssociatedDeviceDiagnosticResult3, wifihal_stub);
    DLSYM(getRadioDfsSupport, wifihal_stub);
    DLSYM(getRadioDfsEnable, wifihal_stub);
    DLSYM(getRadioChannels, wifihal_stub);
    DLSYM(getApAclDeviceNum, wifihal_stub);
    DLSYM(getApAclDevices, wifihal_stub);
    DLSYM2(getApAclDevices, wifihal_stub);
    DLSYM(addApAclDevice, wifihal_stub);
    DLSYM2(addApAclDevice, wifihal_stub);
    DLSYM(delApAclDevices, wifihal_stub);
    DLSYM(steering_clientDisconnect, wifihal_stub);
    DLSYM(pushRadioChannel2, wifihal_stub);
    DLSYM(createVAP, wifihal_stub);
    DLSYM(setRadioOperatingParameters, wifihal_stub);
    DLSYM(setRadioDfsEnable, wifihal_stub);
    DLSYM(getRadioOperatingParameters, wifihal_stub);
    DLSYM(getRadioVapInfoMap, wifihal_stub);
    DLSYM(pushMultiPskKeys, NULL);
    DLSYM(getMultiPskKeys, NULL);
    DLSYM(getMultiPskClientKey, NULL);
    DLSYM(newApAssociatedDevice_callback_register, wifihal_stub);
    DLSYM(apDisassociatedDevice_callback_register, wifihal_stub);
    DLSYM(apDeAuthEvent_callback_register, wifihal_stub);
    DLSYM(getRadioChannelStats, wifihal_stub);
    DLSYM(getNeighboringWiFiStatus, wifihal_stub);
    DLSYM(getHalVersion, wifihal_stub);
    DLSYM(steering_eventRegister, wifihal_stub);
    DLSYM(sendActionFrame, wifihal_stub);
    DLSYM(mgmt_frame_callbacks_register, wifihal_stub);

    priv->wifi_newApAssociatedDevice_callback_register(sta_connect_cb);
    priv->wifi_apDisassociatedDevice_callback_register(sta_disconnect_cb);
    priv->wifi_apDeAuthEvent_callback_register(sta_deauth_cb);
    if (priv->wifi_steering_eventRegister(steering_event_cb) != RETURN_OK)
    {
        LOGI("%s: failed to register steering callback", __func__);
    }
    if (priv->wifi_mgmt_frame_callbacks_register(received_mgmt_frame_cb) != RETURN_OK)
    {
        LOGI("%s: failed to register mgmt frame callback", __func__);
    }

    char ver[64] = {0};
    priv->wifi_getHalVersion(ver);
    LOGI("osw: drv: wifihal: version '%s'%s advertised", ver, is_phase2() ? " phase2" : "");
    if (strstr(ver, "3.0") != ver)
        LOGI("osw: drv: wifihal: only API 3.0.x is supported");

    osw_conf_register_mutator(&priv->conf_mut);
}

static void
osw_drv_get_phy_list_cb(struct osw_drv *drv,
                        osw_drv_report_phy_fn_t *report_phy_fn,
                        void *fn_priv)
{
    if (drv_is_enabled() == false) return;

    const struct wifihal_3_0_priv *priv = osw_drv_get_priv(drv);
    int i;
    char radio_ifname[MAXIFACENAMESIZE] = {0};

    for (i = 0; i < MAX_NUM_RADIOS; i++) {
        if (MEASURE(priv->wifi_getRadioIfName, (i, radio_ifname)) != RETURN_OK)
            continue;

        report_phy_fn(radio_ifname, fn_priv);
    }
}

static void
osw_drv_get_vif_list_cb(struct osw_drv *drv,
                        const char *phy_name,
                        osw_drv_report_vif_fn_t *report_vif_fn,
                        void *fn_priv)
{
    if (drv_is_enabled() == false) return;

    const struct wifihal_3_0_priv *priv = osw_drv_get_priv(drv);
    unsigned int i;

    for (i = 0; i < MAX_NUM_RADIOS; i++) {
        wifi_vap_info_map_t map = {0};
        if (MEASURE(priv->wifi_getRadioVapInfoMap, (i, &map)) != RETURN_OK) continue;
        if (WARN_ON(map.num_vaps > ARRAY_SIZE(map.vap_array))) continue;

        unsigned int j;
        for (j = 0; j < map.num_vaps; j++) {
            const wifi_vap_info_t *vap = &map.vap_array[j];

            if (WARN_ON(i != vap->radio_index)) continue;
            if (radio_index_is_phy_name(i, phy_name) == false) continue;

            const char *vif_name = vap->vap_name;
            size_t max = sizeof(vap->vap_name);
            if (WARN_ON(strnlen(vif_name, max) >= max)) continue;

            report_vif_fn(vif_name, fn_priv);
        }
    }
}

static void
osw_drv_get_sta_list_cb(struct osw_drv *drv,
                        const char *phy_name,
                        const char *vif_name,
                        osw_drv_report_sta_fn_t *report_sta_fn,
                        void *fn_priv)
{
    if (drv_is_enabled() == false) return;

    const struct wifihal_3_0_priv *priv = osw_drv_get_priv(drv);
    int ap_index = vif_name_to_ap_index(priv, vif_name);
    wifi_associated_dev3_t *sta = NULL;
    UINT n_sta = 0;
    UINT i;

    if (WARN_ON(ap_index < 0)) return;

    if (MEASURE(priv->wifi_getApAssociatedDeviceDiagnosticResult3, (ap_index, &sta, &n_sta)) != RETURN_OK) {
        LOGE("%s: cannot get associated device diagnostic result for vap_index: %d",
             __func__, ap_index);
        return;
    }

    for (i = 0; i < n_sta; i++) {
        struct osw_hwaddr addr;
        memcpy(addr.octet, sta[i].cli_MACAddress, MAC_LEN);
        report_sta_fn(&addr, fn_priv);
    }

    free(sta);
    return;
}

static bool
num_bands(wifi_freq_bands_t band)
{
    int bands = 0;
    while (band > 0) {
        if (band & 1) bands++;
        band >>= 1;
    }

    return bands;
}

static int
chan_to_mhz(const wifi_freq_bands_t band, const int c)
{
    if (WARN_ON(num_bands(band) != 1))
        return 0;

    switch (band) {
        case WIFI_FREQUENCY_2_4_BAND:
            return 2407 + (c * 5);
        case WIFI_FREQUENCY_5_BAND:
        case WIFI_FREQUENCY_5L_BAND:
        case WIFI_FREQUENCY_5H_BAND:
            return 5000 + (c * 5);
        case WIFI_FREQUENCY_6_BAND:
            if (c == 2) return 5935;
            else return 5950 + (c * 5);
        case WIFI_FREQUENCY_60_BAND:
            LOGW("%s: 60ghz not supported", __func__);
            return 0;
    }

    LOGW("%s: called with band: 0x%02x for channel %d",
         __func__, band, c);
    return 0;
}

static enum osw_channel_state_dfs
chan_state_to_osw(const wifi_channelState_t s)
{
    switch (s) {
        case CHAN_STATE_AVAILABLE: return OSW_CHANNEL_NON_DFS;
        case CHAN_STATE_DFS_NOP_FINISHED: return OSW_CHANNEL_DFS_CAC_POSSIBLE;
        case CHAN_STATE_DFS_NOP_START: return OSW_CHANNEL_DFS_NOL;
        case CHAN_STATE_DFS_CAC_START: return OSW_CHANNEL_DFS_CAC_IN_PROGRESS;
        case CHAN_STATE_DFS_CAC_COMPLETED: return OSW_CHANNEL_DFS_CAC_COMPLETED;
    }
    LOGW("%s: called with state: %d, assuming non-dfs", __func__, s);
    return OSW_CHANNEL_NON_DFS;
}

static bool
mhz_to_chan_band(const int mhz, int *chan, int *band)
{
    const int b2ch1 = 2412;
    const int b2ch13 = 2472;
    const int b2ch14 = 2484;
    const int b5ch36 = 5180;
    const int b5ch177 = 5885;
    const int b6ch1 = 5955;
    const int b6ch2 = 5935;
    const int b6ch233 = 7115;

    if (mhz == b2ch14) {
        *band = 2;
        *chan = 14;
        return true;
    }
    if (mhz == b6ch2) {
        *band = 6;
        *chan = 2;
        return true;
    }

    if (mhz >= b2ch1 && mhz <= b2ch13) {
        *band = 2;
        *chan = (mhz - 2407) / 5;
        return true;
    }

    if (mhz >= b5ch36 && mhz <= b5ch177) {
        *band = 5;
        *chan = (mhz - 5000) / 5;
        return true;
    }

    if (mhz >= b6ch1 && mhz <= b6ch233) {
        *band = 6;
        *chan = (mhz - 5950) / 5;
        return true;
    }


    LOGW("%s: unknown frequency: %d", __func__, mhz);
    return false;
}

static enum osw_channel_width
bw_to_osw(const wifi_channelBandwidth_t w)
{
    switch (w) {
        case WIFI_CHANNELBANDWIDTH_20MHZ: return OSW_CHANNEL_20MHZ;
        case WIFI_CHANNELBANDWIDTH_40MHZ: return OSW_CHANNEL_40MHZ;
        case WIFI_CHANNELBANDWIDTH_80MHZ: return OSW_CHANNEL_80MHZ;
        case WIFI_CHANNELBANDWIDTH_160MHZ: return OSW_CHANNEL_160MHZ;
        case WIFI_CHANNELBANDWIDTH_80_80MHZ: return OSW_CHANNEL_80P80MHZ;
    }

    LOGW("%s: unknown bandwidth: %d", __func__, w);
    return OSW_CHANNEL_20MHZ;
}

static wifi_channelBandwidth_t
osw_to_bw(enum osw_channel_width w)
{
    switch (w) {
        case OSW_CHANNEL_20MHZ: return WIFI_CHANNELBANDWIDTH_20MHZ;
        case OSW_CHANNEL_40MHZ: return WIFI_CHANNELBANDWIDTH_40MHZ;
        case OSW_CHANNEL_80MHZ: return WIFI_CHANNELBANDWIDTH_80MHZ;
        case OSW_CHANNEL_160MHZ: return WIFI_CHANNELBANDWIDTH_160MHZ;
        case OSW_CHANNEL_80P80MHZ: return WIFI_CHANNELBANDWIDTH_80_80MHZ;
        case OSW_CHANNEL_320MHZ:
            LOGW("%s: Unsupported bandwidth: %d", __func__, w);
            break;
    }
    LOGW("%s: unknown bandwidth: %d", __func__, w);
    return WIFI_CHANNELBANDWIDTH_20MHZ;
}

static int
chan_avg(const int *c)
{
    int sum = 0;
    int n = 0;
    if (c == NULL || *c < 1)
        return 0;
    while (*c) {
        sum += *c;
        n++;
        c++;
    }
    return sum / n;
}

static int
chan_to_center_2ghz(const wifi_radio_operationParam_t *p)
{
    int c[3] = { p->channel, 0, 0 };
    switch (p->channelWidth) {
        case WIFI_CHANNELBANDWIDTH_20MHZ:
            break;
        case WIFI_CHANNELBANDWIDTH_40MHZ:
            if (p->numSecondaryChannels >= 1) {
                c[1] = p->channelSecondary[0];
            }
            break;
        case WIFI_CHANNELBANDWIDTH_80MHZ:
        case WIFI_CHANNELBANDWIDTH_160MHZ:
        case WIFI_CHANNELBANDWIDTH_80_80MHZ:
            /* N/A on 2.4GHz band */
            break;
    }
    return chan_to_mhz(p->band, chan_avg(c));
}

static int
chan_to_center_5ghz(const UINT c, const int w)
{
    wifi_freq_bands_t b = WIFI_FREQUENCY_5_BAND;
    return chan_to_mhz(b, chan_avg(unii_5g_chan2list(c, w)));
}

static int
chan_to_center_6ghz(const UINT c, const int w)
{
    wifi_freq_bands_t b = WIFI_FREQUENCY_6_BAND;
    return chan_to_mhz(b, chan_avg(unii_6g_chan2list(c, w)));
}

static int
bw_to_mhz(const wifi_channelBandwidth_t w)
{
    switch (w) {
        case WIFI_CHANNELBANDWIDTH_20MHZ: return 20;
        case WIFI_CHANNELBANDWIDTH_40MHZ: return 40;
        case WIFI_CHANNELBANDWIDTH_80MHZ: return 80;
        case WIFI_CHANNELBANDWIDTH_160MHZ: return 160;
        case WIFI_CHANNELBANDWIDTH_80_80MHZ: return 0; /* N/A */
    }
    return 0;
}

static int
chan_to_center(const wifi_radio_operationParam_t *p)
{
    UINT c = p->channel;
    int w = bw_to_mhz(p->channelWidth);

    switch (p->band) {
        case WIFI_FREQUENCY_2_4_BAND:
            return chan_to_center_2ghz(p);
        case WIFI_FREQUENCY_5_BAND:
        case WIFI_FREQUENCY_5L_BAND:
        case WIFI_FREQUENCY_5H_BAND:
            return chan_to_center_5ghz(c, w);
        case WIFI_FREQUENCY_6_BAND:
            return chan_to_center_6ghz(c, w);
        case WIFI_FREQUENCY_60_BAND:
            break;
    }

    LOGW("%s: failed to decode channel center freq", __func__);
    return 0;
}

static struct osw_channel
radio_build_chan(const wifi_radio_operationParam_t *p)
{
    struct osw_channel c = {
        .control_freq_mhz = chan_to_mhz(p->band, p->channel),
        .center_freq0_mhz = chan_to_center(p),
        .width = bw_to_osw(p->channelWidth),
    };
    return c;
}

static enum osw_radar_detect
get_radar(wifi_freq_bands_t band, int rix)
{
    const struct wifihal_3_0_priv *priv = &g_priv;
    static const int band5 = WIFI_FREQUENCY_5_BAND
                           | WIFI_FREQUENCY_5L_BAND
                           | WIFI_FREQUENCY_5H_BAND;
    BOOL b;

    if ((band & band5) == 0)
        return OSW_RADAR_UNSUPPORTED;

    if (MEASURE(priv->wifi_getRadioDfsSupport, (rix, &b)) != RETURN_OK)
        return OSW_RADAR_UNSUPPORTED;

    if (b == false)
        return OSW_RADAR_UNSUPPORTED;

    if (MEASURE(priv->wifi_getRadioDfsEnable, (rix, &b)) != RETURN_OK)
        return OSW_RADAR_UNSUPPORTED;

    if (b == true)
        return OSW_RADAR_DETECT_ENABLED;
    else
        return OSW_RADAR_DETECT_DISABLED;
}

static void
fill_chan_list(const struct wifihal_3_0_priv *priv,
               int rix,
               const wifi_freq_bands_t band,
               struct osw_drv_phy_state *phy)
{
    wifi_channelMap_t ch_map[128] = {0};
    const size_t ch_size = ARRAY_SIZE(ch_map);

    if (WARN_ON(MEASURE(priv->wifi_getRadioChannels, (rix, ch_map, ch_size)) != RETURN_OK))
        return;

    size_t i;
    size_t n = 0;

    for (i = 0; i < ch_size; i++)
        if (ch_map[i].ch_number != 0)
            n++;

    struct osw_channel_state *cs = CALLOC(n, sizeof(*cs));
    phy->n_channel_states = n;
    phy->channel_states = cs;

    for (i = 0; i < ch_size; i++) {
        const wifi_channelState_t s = ch_map[i].ch_state;
        const int c = ch_map[i].ch_number;

        if (c == 0) continue;

        cs->channel.control_freq_mhz = chan_to_mhz(band, c);
        cs->dfs_state = chan_state_to_osw(s);
        cs++;
    }
}

static void
osw_drv_request_phy_state_cb(struct osw_drv *drv,
                             const char *phy_name)
{
    const struct wifihal_3_0_priv *priv = osw_drv_get_priv(drv);
    struct osw_drv_phy_state phy = {0};
    wifi_radio_operationParam_t radio_params = {0};
    wifi_radio_index_t rix = 0;

    if (phy_name_to_radio_index(phy_name, &rix) == false)
        goto report;

    if (MEASURE(priv->wifi_getRadioOperatingParameters, (rix, &radio_params)) != WIFI_HAL_SUCCESS) {
        LOGE("cannot get radio operating parameters for index: %d", rix);
        goto report;
    }

    phy.exists = true;
    phy.enabled = radio_params.enable;
    phy.radar = get_radar(radio_params.band, rix);
    fill_chan_list(priv, rix, radio_params.band, &phy);
    // phy_state.tx_chainmask = ... not supported in RDK

report:
    osw_drv_report_phy_state(drv, phy_name, &phy);
    osw_drv_phy_state_report_free(&phy);
}

static enum osw_acl_policy
bss_get_acl_policy(const wifi_front_haul_bss_t *bss)
{
    if (bss->mac_filter_enable == false)
        return OSW_ACL_NONE;

    switch (bss->mac_filter_mode) {
        case wifi_mac_filter_mode_black_list:
            return OSW_ACL_DENY_LIST;
        case wifi_mac_filter_mode_white_list:
            return OSW_ACL_ALLOW_LIST;
    }

    /* Shouldn't be reached */
    return OSW_ACL_NONE;
}

static void
bss_fill_acl(const wifi_vap_info_t *vap,
             struct osw_drv_vif_state_ap *ap)
{
    const struct wifihal_3_0_priv *priv = &g_priv;
    int ap_index = vap->vap_index;
    UINT size;

    if (WARN_ON(MEASURE(priv->wifi_getApAclDeviceNum, (ap_index, &size)) != RETURN_OK)) return;
    if (size == 0) return; /* nothing to do */

    if (is_phase2() == true) {
        mac_address_t *macs = CALLOC(size, sizeof(*macs));
        UINT len = 0;

        if (WARN_ON(MEASURE(priv->wifi_getApAclDevices2, (ap_index, macs, size, &len)) != RETURN_OK)) goto end;
        if (WARN_ON(len > size)) goto end;

        ap->acl.count = len;
        ap->acl.list = CALLOC(len, sizeof(*ap->acl.list));

        UINT i;
        for (i = 0; i < len; i++)
            memcpy(&ap->acl.list[i].octet, macs[i], MAC_LEN);

end:
        FREE(macs);
    }
    else {
        char buf[64*1024]; /* should be big enough */
        char *bufp = buf;
        char *p;

        size = sizeof(buf);
        if (WARN_ON(MEASURE(priv->wifi_getApAclDevices, (ap_index, buf, size)) != RETURN_OK)) return;
        while ((p = strsep(&bufp, "\n")) != NULL) {
            struct osw_hwaddr addr;

            if (osw_hwaddr_from_cstr(p, &addr) == false) continue;
            if (osw_hwaddr_is_zero(&addr)) continue;

            int n = ++ap->acl.count;
            ap->acl.list = REALLOC(ap->acl.list, n * sizeof(*ap->acl.list));
            memcpy(&ap->acl.list[n - 1], &addr, sizeof(addr));
        }
    }
}

#define MAX_PSK 64

static void
bss_add_psk(struct osw_drv_vif_state_ap *ap,
            int key_id,
            const char *str)
{
    size_t n = ++ap->psk_list.count;
    struct osw_ap_psk **arr = &ap->psk_list.list;
    size_t size = n * sizeof(**arr);
    (*arr) = REALLOC(*arr, size);
    (*arr)[n - 1].key_id = key_id;
    STRSCPY_WARN((*arr)[n - 1].psk.str, str);
}

static void
bss_fill_psk(const wifi_vap_info_t *vap,
             struct osw_drv_vif_state_ap *ap)
{
    struct wifihal_3_0_priv *priv = &g_priv;
    INT ap_index = vap->vap_index;
    wifi_key_multi_psk_t keys[MAX_PSK];
    INT size = ARRAY_SIZE(keys);

    if (priv->wifi_getMultiPskKeys == NULL)
        return;

    if (WARN_ON(MEASURE(priv->wifi_getMultiPskKeys, (ap_index, keys, size)) != RETURN_OK))
        return;

    INT i;
    for (i = 0; i < size; i++) {
        const char *str = keys[i].wifi_psk;
        int key_id = atoi(keys[i].wifi_keyId);

        if (strlen(str) < 8) continue;
        bss_add_psk(ap, key_id, str);
    }
}

static enum osw_pmf
mfp_to_osw_pmf(const wifi_mfp_cfg_t mfp)
{
    switch (mfp) {
        case wifi_mfp_cfg_disabled: return OSW_PMF_DISABLED;
        case wifi_mfp_cfg_optional: return OSW_PMF_OPTIONAL;
        case wifi_mfp_cfg_required: return OSW_PMF_REQUIRED;
    }
    LOGW("%s: unknown mfp: %d, default to disabled", __func__, mfp);
    return OSW_PMF_DISABLED;
}

static void
bss_fill_wpa(const wifi_vap_info_t *vap,
             struct osw_drv_vif_state_ap *ap)
{
    const wifi_front_haul_bss_t *bss = &vap->u.bss_info;
    const wifi_vap_security_t *sec = &bss->security;
    struct osw_wpa *wpa = &ap->wpa;
    bool is_key = false;

    switch (sec->mode) {
        case wifi_security_mode_none:
            return;
        case wifi_security_mode_wep_64:
            LOGW("%s: wep 64 not supported", vap->vap_name);
            return;
        case wifi_security_mode_wep_128:
            LOGW("%s: wep 128 not supported", vap->vap_name);
            return;
        case wifi_security_mode_wpa_personal:
            wpa->wpa = true;
            wpa->akm_psk = true;
            is_key = true;
            break;
        case wifi_security_mode_wpa2_personal:
            wpa->rsn = true;
            wpa->akm_psk = true;
            is_key = true;
            break;
        case wifi_security_mode_wpa_wpa2_personal:
            wpa->wpa = true;
            wpa->rsn = true;
            wpa->akm_psk = true;
            is_key = true;
            break;
        case wifi_security_mode_wpa_enterprise:
            LOGI("%s: wpa enterprise not supported", vap->vap_name);
            break;
        case wifi_security_mode_wpa2_enterprise:
            LOGI("%s: wpa2 enterprise not supported", vap->vap_name);
            break;
        case wifi_security_mode_wpa_wpa2_enterprise:
            LOGI("%s: wpa1+2 enterprise not supported", vap->vap_name);
            break;
        case wifi_security_mode_wpa3_personal:
            wpa->rsn = true;
            wpa->akm_sae = true;
            is_key = true;
            break;
        case wifi_security_mode_wpa3_transition:
            wpa->rsn = true;
            wpa->akm_psk = true;
            wpa->akm_sae = true;
            is_key = true;
            break;
        case wifi_security_mode_wpa3_enterprise:
            LOGI("%s: wpa3 enterprise not supported", vap->vap_name);
            break;
    }

    switch (sec->encr) {
        case wifi_encryption_none:
            break;
        case wifi_encryption_tkip:
            wpa->pairwise_tkip = true;
            break;
        case wifi_encryption_aes:
            wpa->pairwise_ccmp = true;
            break;
        case wifi_encryption_aes_tkip:
            wpa->pairwise_tkip = true;
            wpa->pairwise_ccmp = true;
            break;
    }

    wpa->pmf = mfp_to_osw_pmf(sec->mfp);

    /* FIXME: Is this expected? This isn't documented. */
    if (sec->wpa3_transition_disable &&
        wpa->akm_sae == true &&
        wpa->akm_psk == true) {
        wpa->akm_psk = false;
        LOGI("%s: overriding: disabling wpa3 transition", vap->vap_name);
    }

    wpa->group_rekey_seconds = sec->rekey_interval;

    if (is_key == true) {
        size_t max = sizeof(sec->u.key.key);

        switch (sec->u.key.type) {
            case wifi_security_key_type_psk:
            case wifi_security_key_type_pass:
            case wifi_security_key_type_sae:
            case wifi_security_key_type_psk_sae:
                /* FIXME: What's the difference between PSK
                 * and PASS? PSK and SAE? Is one of these an
                 * actual PMK instead? Assume everything as
                 * ASCII for now. */

                if (WARN_ON(strnlen(sec->u.key.key, max) >= max) == false)
                    bss_add_psk(ap, -1, sec->u.key.key);
                break;
        }
    }

    /* FIXME: ft_mobility_domain and akm_ft_* aren't
     * implemented in Wifi HAL at this time.
     */
}

static void
osw_drv_request_vif_state_cb(struct osw_drv *drv,
                             const char *phy_name,
                             const char *vif_name)
{
    const struct wifihal_3_0_priv *priv = osw_drv_get_priv(drv);
    struct osw_drv_vif_state vif = {0};
    struct osw_drv_vif_state_ap *ap = &vif.u.ap;
    wifi_vap_info_t vap = {0};
    wifi_radio_index_t rix;

    if (phy_name_to_radio_index(phy_name, &rix) == false)
        goto report;

    if (vap_walk_rix_name_to_info(priv, rix, vif_name, &vap) == false)
        goto report;

    /* FIXME: No clue if accessing vap->u.bss_info is legal.
     * There's no vap_type.
     */

    const wifi_front_haul_bss_t *bss = &vap.u.bss_info;

    vif.exists = true;
    osw_vif_status_set(&vif.status, bss->enabled ? OSW_VIF_ENABLED : OSW_VIF_DISABLED);
    vif.vif_type = OSW_VIF_AP;
    memcpy(&vif.mac_addr.octet, bss->bssid, MAC_LEN);

    STRSCPY_WARN(vif.u.ap.bridge_if_name.buf, vap.bridge_name);
    ap->isolated = bss->isolation;
    ap->ssid_hidden = bss->showSsid ? false : true;

    size_t max_ssid = ARRAY_SIZE(bss->ssid);
    STRSCPY_WARN(ap->ssid.buf, bss->ssid);
    ap->ssid.len = strnlen(bss->ssid, max_ssid);

    ap->mode.wmm_enabled = bss->wmm_enabled;
    ap->mode.wmm_uapsd_enabled = bss->UAPSDEnabled;
    ap->mode.wnm_bss_trans = bss->bssTransitionActivated;
    ap->mode.rrm_neighbor_report = bss->nbrReportActivated;
    ap->acl_policy = bss_get_acl_policy(bss);
    bss_fill_acl(&vap, ap);
    bss_fill_wpa(&vap, ap);
    bss_fill_psk(&vap, ap);

    wifi_radio_operationParam_t radio_params = {0};
    if (MEASURE(priv->wifi_getRadioOperatingParameters, (rix, &radio_params)) != RETURN_OK)
        goto report;

    wifi_ieee80211Variant_t v = radio_params.variant;
    if (v & WIFI_80211_VARIANT_AX) ap->mode.he_enabled = true;
    if (v & WIFI_80211_VARIANT_AC) ap->mode.vht_enabled = true;
    if (v & WIFI_80211_VARIANT_N) ap->mode.ht_enabled = true;

    // WIFI_80211_VARIANT_A = 0x01,
    // WIFI_80211_VARIANT_B = 0x02,
    // WIFI_80211_VARIANT_G = 0x04,
    // WIFI_80211_VARIANT_H = 0x10,
    // WIFI_80211_VARIANT_AD = 0x40,
    // mode.wps

    /* FIXME: Is the radio_params using Transmission Units or Milliseconds? */
    ap->beacon_interval_tu = radio_params.beaconInterval;
    ap->channel = radio_build_chan(&radio_params);

report:
    osw_drv_report_vif_state(drv, phy_name, vif_name, &vif);
    osw_drv_vif_state_report_free(&vif);
}

static void
osw_drv_request_sta_state_cb(struct osw_drv *drv,
                             const char *phy_name,
                             const char *vif_name,
                             const struct osw_hwaddr *mac_addr)
{
    const struct wifihal_3_0_priv *priv = osw_drv_get_priv(drv);
    struct osw_drv_sta_state sta = {0};
    wifi_associated_dev3_t *arr = NULL;
    wifi_vap_info_t vap = {0};
    wifi_radio_index_t rix;
    UINT n = 0;
    UINT i;

    if (phy_name_to_radio_index(phy_name, &rix) == false)
        goto report;

    if (vap_walk_rix_name_to_info(priv, rix, vif_name, &vap) == false)
        goto report;

    INT vix = vap.vap_index;
    if (MEASURE(priv->wifi_getApAssociatedDeviceDiagnosticResult3, (vix, &arr, &n)) != RETURN_OK)
        goto report;

    for (i = 0; i < n; i++) {
        const size_t n = sizeof(mac_addr->octet);
        const void *mac = mac_addr->octet;

        if (memcmp(mac, arr[i].cli_MACAddress, n) == 0) {
            if (arr[i].cli_Active)
                sta.connected = true;

            /* FIXME: key_id */
        }
    }

report:
    osw_drv_report_sta_state(drv, phy_name, vif_name, mac_addr, &sta);
    osw_drv_sta_state_report_free(&sta);
    free(arr);
    return;
}

static void
vap_configure_ap_psk(wifi_vap_info_t *vap,
                     const struct osw_drv_vif_config_ap *ap)
{
    struct wifihal_3_0_priv *priv = &g_priv;
    INT vix = vap->vap_index;

    if (ap->psk_list.count > 0) {
        const char *cstr = ap->psk_list.list[0].psk.str;
        STRSCPY_WARN(vap->u.bss_info.security.u.key.key, cstr);
    }

    if (priv->wifi_pushMultiPskKeys == NULL)
        return;

    if (ap->psk_list.count <= 1)
        WARN_ON(MEASURE(priv->wifi_pushMultiPskKeys, (vix, NULL, 0)) != RETURN_OK);

    if (ap->psk_list.count > 1) {
        wifi_key_multi_psk_t keys[MAX_PSK];
        size_t max = ARRAY_SIZE(keys);
        size_t n = 0;

        for (n = 1; n < ap->psk_list.count; n++) {
            const char *psk_str = ap->psk_list.list[n].psk.str;
            int key_id = ap->psk_list.list[n].key_id;
            char key_id_str[32];

            if (n >= max) break;

            snprintf(key_id_str, sizeof(key_id_str), "%d", key_id);
            STRSCPY_WARN(keys[n].wifi_keyId, key_id_str);
            STRSCPY_WARN(keys[n].wifi_psk, psk_str);
        }

        WARN_ON(ap->psk_list.count > max);
        WARN_ON(MEASURE(priv->wifi_pushMultiPskKeys, (vix, keys, n)) != RETURN_OK);
    }
}

static wifi_security_modes_t
get_sec_mode(const struct osw_wpa *wpa)
{
    if (wpa->wpa == false && wpa->rsn == false)
        return wifi_security_mode_none;
    else if (wpa->akm_psk == true && wpa->akm_sae == false) {
        if (wpa->wpa == true && wpa->rsn == false)
            return wifi_security_mode_wpa_personal;
        else if (wpa->wpa == false && wpa->rsn == true)
            return wifi_security_mode_wpa2_personal;
        else if (wpa->wpa == true && wpa->rsn == true)
            return wifi_security_mode_wpa_wpa2_personal;
    }
    else if (wpa->akm_psk == false && wpa->akm_sae == true) {
        if (wpa->wpa == false && wpa->rsn == true)
            return wifi_security_mode_wpa3_personal;
    }
    else if (wpa->akm_psk == true && wpa->akm_sae == true) {
        if (wpa->rsn == true) /* wpa->wpa can be whatever */
            return wifi_security_mode_wpa3_transition;
    }

    /* FIXME: Some unhandled cases for now: */
    //wifi_security_mode_wep_64
    //wifi_security_mode_wep_128
    //wifi_security_mode_wpa_enterprise
    //wifi_security_mode_wpa2_enterprise
    //wifi_security_mode_wpa_wpa2_enterprise
    //wifi_security_mode_wpa3_enterprise

    return wifi_security_mode_none;
}

static wifi_encryption_method_t
get_sec_encr(const struct osw_wpa *wpa)
{
    if (wpa->pairwise_tkip == false && wpa->pairwise_ccmp == false)
        return wifi_encryption_none;
    else if (wpa->pairwise_tkip == true && wpa->pairwise_ccmp == false)
        return wifi_encryption_tkip;
    else if (wpa->pairwise_tkip == false && wpa->pairwise_ccmp == true)
        return wifi_encryption_aes;
    else if (wpa->pairwise_tkip == true && wpa->pairwise_ccmp == true)
        return wifi_encryption_aes_tkip;

    char buf[256] = {0};
    osw_wpa_to_str(buf, sizeof(buf), wpa);
    LOGW("%s: unhandled encr: wpa=%s", __func__, buf);
    return wifi_encryption_none;
}

static wifi_mfp_cfg_t
get_sec_mfp(const enum osw_pmf pmf)
{
    switch (pmf) {
        case OSW_PMF_DISABLED: return wifi_mfp_cfg_disabled;
        case OSW_PMF_OPTIONAL: return wifi_mfp_cfg_optional;
        case OSW_PMF_REQUIRED: return wifi_mfp_cfg_required;
    }

    LOGW("%s: unhandled mfp: pmf=%d", __func__, pmf);
    return wifi_mfp_cfg_disabled;
}

static void
vap_configure_ap(const struct wifihal_3_0_priv *priv,
                 wifi_vap_info_t *vap,
                 const struct osw_drv_phy_config *pconf,
                 const struct osw_drv_vif_config *vconf,
                 bool *changed)
{
    const struct osw_drv_vif_config_ap *ap = &vconf->u.ap;
    const int vix = vap->vap_index;
    wifi_front_haul_bss_t *bss = &vap->u.bss_info;

    /* FIXME: How to verify that vap->u.bss_info is legal?
     * There's no vap_type in wifi_vap_info_t.
     */

    if (vconf->changed == false) {
        return;
    }

    if (vconf->enabled_changed == true) {
        bss->enabled = vconf->enabled;
        *changed = true;
    }

    if (vconf->vif_type_changed == true) {
        LOGW("%s: cannot change vif_type to %d, unsupported",
             vconf->vif_name, vconf->vif_type);
    }

    if (ap->bridge_if_name_changed == true) {
        STRSCPY_WARN(vap->bridge_name, ap->bridge_if_name.buf);
        *changed = true;
    }

    if (ap->ssid_changed == true) {
        const size_t len = ap->ssid.len + sizeof('\0');
        const size_t max = ARRAY_SIZE(bss->ssid);
        if (len <= max) {
            memcpy(bss->ssid, ap->ssid.buf, len);
        }
        else {
            LOGW("%s: cannot fit ssid into buffer, '%s' is too long (%zu > %zu)",
                 vap->vap_name, ap->ssid.buf, len, max);
        }
        *changed = true;
    }

    if (ap->isolated == true) {
        bss->isolation = ap->isolated;
        *changed = true;
    }

    if (ap->psk_list_changed == true) {
        vap_configure_ap_psk(vap, ap);
        *changed = true;
    }

    if (ap->ssid_hidden_changed == true) {
        bss->showSsid = ap->ssid_hidden == true ? false : true;
        *changed = true;
    }

    if (ap->wpa_changed == true) {
        bss->security.rekey_interval = ap->wpa.group_rekey_seconds;
        bss->security.mode = get_sec_mode(&ap->wpa);
        bss->security.encr = get_sec_encr(&ap->wpa);
        bss->security.mfp = get_sec_mfp(ap->wpa.pmf);
        *changed = true;

        /* FIXME:
           BOOL  wpa3_transition_disable;
           UINT  rekey_interval;
           BOOL  strict_rekey;  // must be set for enterprise VAPs
           UINT  eapol_key_timeout;
           UINT  eapol_key_retries;
           UINT  eap_identity_req_timeout;
           UINT  eap_identity_req_retries;
           UINT  eap_req_timeout;
           UINT  eap_req_retries;
           BOOL  disable_pmksa_caching;
         */
    }

    if (ap->mode_changed == true) {
        bss->wmm_enabled = ap->mode.wmm_enabled;
        bss->UAPSDEnabled = ap->mode.wmm_uapsd_enabled;
        bss->bssTransitionActivated = ap->mode.wnm_bss_trans;
        bss->nbrReportActivated = ap->mode.rrm_neighbor_report;
        *changed = true;

        /* The following are applied per radio in another
         * place in the code:
           bool ht_enabled;
           bool vht_enabled;
           bool he_enabled;
         */

        /* FIXME:
           struct osw_rateset_legacy supported_rates;
           struct osw_rateset_legacy basic_rates;
           struct osw_beacon_rate beacon_rate;
           bool ht_required;
           bool vht_required;
           bool he_required;
           bool wps;
         */
    }

    if (ap->acl_policy_changed == true) {
        switch (ap->acl_policy) {
            case OSW_ACL_NONE:
                bss->mac_filter_enable = false;
                break;
            case OSW_ACL_ALLOW_LIST:
                bss->mac_filter_enable = true;
                bss->mac_filter_mode = wifi_mac_filter_mode_white_list;
                break;
            case OSW_ACL_DENY_LIST:
                bss->mac_filter_enable = true;
                bss->mac_filter_mode = wifi_mac_filter_mode_black_list;
                break;
        }

        *changed = true;
    }

    if (ap->acl_changed == true) {
        /* FIXME: This is not atomic. In some cases will
         * need (not always!) to kick out clients that
         * inadvertently made it into the network. Eg.
         * opensync onboard and backhaul networks are strict
         * lists.
         */
        WARN_ON(priv->wifi_delApAclDevices(vix) != RETURN_OK);

        size_t i;
        for (i = 0; i < ap->acl.count; i++) {
            if (is_phase2() == true) {
                const struct osw_hwaddr *addr = &ap->acl.list[i];
                mac_address_t mac_addr;
                memcpy(mac_addr, addr->octet, sizeof(addr->octet));
                if (WARN_ON(priv->wifi_addApAclDevice2 == NULL)) continue;
                WARN_ON(priv->wifi_addApAclDevice2(vix, mac_addr) != RETURN_OK);
            }
            else {
                const struct osw_hwaddr *addr = &ap->acl.list[i];
                struct osw_hwaddr_str str;
                osw_hwaddr2str(addr, &str);
                if (WARN_ON(priv->wifi_addApAclDevice == NULL)) continue;
                WARN_ON(priv->wifi_addApAclDevice(vix, str.buf) != RETURN_OK);
            }
        }

        *changed = true;
    }

    /* FIXME: add handling of wifi_vap_info_t:
    INT     mgmtPowerControl;
    UINT    bssMaxSta;
    BOOL    rapidReconnectEnable;       //should not be implemented in the hal
    UINT    rapidReconnThreshold;       //should not be implemented in the hal
    BOOL    vapStatsEnable;             //should not be implemented in the hal
    wifi_interworking_t interworking;
    BOOL    sec_changed;                //should not be implemented in the hal
    wifi_wps_t   wps;
    wifi_bitrate_t beaconRate;
    UINT   wmmNoAck;
    UINT   wepKeyLength;
    BOOL   bssHotspot;
    UINT   wpsPushButton;
    char   beaconRateCtl[32];
    */

    //bool mcast2ucast;
    /* ap->channel_changed is handled per phy */
}

struct vap_configure_arg {
    const struct wifihal_3_0_priv *priv;
    const struct osw_drv_phy_config *pconf;
    const struct osw_drv_vif_config *vconf;
    bool *changed;
    wifi_vap_info_t *vap;
};

static bool
vap_configure_cb(wifi_vap_info_t *vap, void *fn_priv)
{
    struct vap_configure_arg *arg = fn_priv;

    if (strcmp(vap->vap_name, arg->vconf->vif_name) != 0)
        return false;

    switch (arg->vconf->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            vap_configure_ap(arg->priv, vap, arg->pconf, arg->vconf, arg->changed);
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            break;
    }

    if (arg->changed)
        arg->vap = vap;

    return true;
}

static void
chan_fill_sec_chan(wifi_radio_operationParam_t *p, const int *chans)
{
    while (chans != NULL && *chans != 0) {
        if (*chans != (int)p->channel) {
            p->channelSecondary[p->numSecondaryChannels++] = *chans;
        }
        chans++;
    }
}

static void
chan_fill_sec_chan_5ghz(wifi_radio_operationParam_t *p)
{
    const int w = bw_to_mhz(p->channelWidth);
    const int *chans = unii_5g_chan2list(p->channel, w);
    chan_fill_sec_chan(p, chans);
}

static void
chan_fill_sec_chan_6ghz(wifi_radio_operationParam_t *p)
{
    const int w = bw_to_mhz(p->channelWidth);
    const int *chans = unii_6g_chan2list(p->channel, w);
    chan_fill_sec_chan(p, chans);
}

static void
chan_fill_rparam(const struct osw_channel *c,
                 wifi_radio_operationParam_t *p)
{
    int band;
    int cn;

    if (mhz_to_chan_band(c->control_freq_mhz, &cn, &band) == false)
        return;

    p->channel = cn;
    p->channelWidth = osw_to_bw(c->width);
    p->numSecondaryChannels = 0;

    switch (band) {
        case 2:
            if (c->width == OSW_CHANNEL_40MHZ) {
                p->numSecondaryChannels = 1;
                if (c->control_freq_mhz < c->center_freq0_mhz)
                    p->channelSecondary[0] = p->channel + 4;
                else
                    p->channelSecondary[0] = p->channel - 4;

                WARN_ON(p->channelSecondary[0] < 1);
                WARN_ON(p->channelSecondary[0] > 14);
            }
            break;
        case 5:
            chan_fill_sec_chan_5ghz(p);
            break;
        case 6:
            chan_fill_sec_chan_6ghz(p);
            break;
    }

    /* FIXME: p->operatingClass */
}

static bool
try_csa(int rix, const char *phy_name, const struct osw_channel *c)
{
    const struct wifihal_3_0_priv *priv = &g_priv;
    const wifi_channelBandwidth_t w = osw_to_bw(c->width);
    const int bw = bw_to_mhz(w);
    const int cs_count = 15;
    int cn;
    int band;

    if (osw_etc_get("OSW_DRV_WIFIHAL_DISABLE_PUSH_CHANNEL") != NULL)
        return false;

    if (WARN_ON(mhz_to_chan_band(c->control_freq_mhz, &cn, &band) == false))
        return false;

    LOGI("osw: drv: wifihal: %s: changing channel to %d @ %dMHz",
         phy_name, cn, bw);

    if (WARN_ON(MEASURE(priv->wifi_pushRadioChannel2, (rix, cn, bw, cs_count)) != RETURN_OK))
        return false;

    return true;
}

static wifi_ieee80211Variant_t
get_hal_variant(const wifi_freq_bands_t bands, const struct osw_ap_mode *m)
{
    wifi_ieee80211Variant_t v = 0;

    if (bands & WIFI_FREQUENCY_2_4_BAND) {
        /* FIXME: supported_rates/basic_rates/beacon_rate should
         * be used to infer if 11b is enabled. OSW could provide a
         * helper. */
        // FIXME: v |= WIFI_80211_VARIANT_B;
        v |= WIFI_80211_VARIANT_G;
    }

    if (bands & WIFI_FREQUENCY_5_BAND ||
        bands & WIFI_FREQUENCY_5L_BAND ||
        bands & WIFI_FREQUENCY_5H_BAND ||
        bands & WIFI_FREQUENCY_6_BAND) {
        v |= WIFI_80211_VARIANT_A;
    }

    if (m->ht_enabled == true)
        v |= WIFI_80211_VARIANT_N;
    if (m->vht_enabled == true)
        v |= WIFI_80211_VARIANT_AC;
    if (m->he_enabled == true)
        v |= WIFI_80211_VARIANT_AX;

    /* FIXME: Why is 11h a mode variant? I saw it enabled on
     * 2.4G phy as well, hmm..
     */
    v |= WIFI_80211_VARIANT_H;

    /* FIXME: WIFI_80211_VARIANT_AD */

    return v;
}

static void
osw_drv_request_config_cb(struct osw_drv *drv,
                          struct osw_drv_conf *conf)
{
    struct wifihal_3_0_priv *priv = osw_drv_get_priv(drv);
    unsigned int i;

    if (drv_is_enabled() == false) return;

    for (i = 0; i < conf->n_phy_list; i++) {
        struct osw_drv_phy_config *pconf = &conf->phy_list[i];
        const char *phy_name = pconf->phy_name;
        bool phy_changed = false;
        wifi_radio_operationParam_t radio_params = {0};
        wifi_radio_index_t rix;

        if (phy_name_to_radio_index(phy_name, &rix) == false)
            continue;

        if (MEASURE(priv->wifi_getRadioOperatingParameters, (rix, &radio_params)) != RETURN_OK) {
            LOGW("Cannot get radio operating params for radio index: %d", rix);
            continue;
        }

        if (pconf->enabled_changed) {
            radio_params.enable = pconf->enabled;
            phy_changed = true;
        }

        if (pconf->tx_chainmask_changed) {
            // FIXME: Wifi HAL doesn't seem to support this.
        }

        if (pconf->radar_next_channel_changed) {
            // FIXME: Wifi HAL doesn't seem to support this either.
        }

        if (pconf->radar_changed) {
            BOOL b = 0;
            switch (pconf->radar) {
                case OSW_RADAR_UNSUPPORTED: break;
                case OSW_RADAR_DETECT_ENABLED: b = 1; break;
                case OSW_RADAR_DETECT_DISABLED: b = 0; break;
            }
            WARN_ON(MEASURE(priv->wifi_setRadioDfsEnable, (rix, b)) != RETURN_OK);
        }

        bool vap_changed = false;
        wifi_vap_info_map_t vaps = {0};
        wifi_vap_info_map_t vaps2 = {0};
        if (WARN_ON(MEASURE(priv->wifi_getRadioVapInfoMap, (rix, &vaps)) != RETURN_OK))
            continue;

        const struct osw_channel *c = NULL;
        const struct osw_ap_mode *m = NULL;
        bool csa_required = false;
        unsigned int j;
        for (j = 0; j < pconf->vif_list.count; j++) {
            const struct osw_drv_vif_config *vconf = &pconf->vif_list.list[j];
            const char *vif_name = vconf->vif_name;
            bool changed = false;
            struct vap_configure_arg arg = {
                .priv = priv,
                .pconf = pconf,
                .vconf = vconf,
                .changed = &changed,
                .vap = NULL,
            };
            /* FIXME: Some of the actions taken by vap_configure_cb()
             * call wifi hal api - this might not be a good idea
             * because some parameters may only be changeable when
             * interface is already UP, ie. bringing interface up
             * would require 2 rounds of reconfigs (with 30s timeout
             * in between by osw_confsync). This is fine for initial
             * bring up.
             */
            vap_walk_buf(&vaps, vap_configure_cb, &arg);
            if (changed) {
                memcpy(&vaps2.vap_array[vaps2.num_vaps], arg.vap, sizeof(*arg.vap));
                vaps2.num_vaps++;
                vap_changed = true;
                LOGI("osw: drv: wifihal: %s: will use createVAP()", arg.vap->vap_name);
            }
            osw_drv_report_vif_changed(drv, phy_name, vif_name);

            /* FIXME: This assumes all vifs have the same channel and
             * mode. OSW may need to provide a combined values for
             * easier implementation of OSW drivers.
             */
            if (vconf->vif_type == OSW_VIF_AP) {
                const struct osw_drv_vif_config_ap *ap = &vconf->u.ap;

                if (ap->channel_changed == true) {
                    c = &ap->channel;
                    csa_required |= ap->csa_required;
                }

                if (ap->mode_changed == true) {
                    m = &ap->mode;
                }

                if (ap->beacon_interval_tu_changed == true) {
                    radio_params.beaconInterval = vconf->u.ap.beacon_interval_tu;
                    phy_changed = true;
                }
            }
        }

        if (m != NULL) {
            radio_params.variant = get_hal_variant(radio_params.band, m);
            phy_changed = true;
        }

        if (c != NULL) {
            if (csa_required == true && phy_changed == true) {
                LOGI("osw: drv: wifihal: %s: skipping csa, radio reconfig required anyway", phy_name);
                csa_required = false;
            }

            if (csa_required == false || try_csa(rix, phy_name, c) == false) {
                chan_fill_rparam(c, &radio_params);
                radio_params.autoChannelEnabled = 0;
                phy_changed = true;
            }
        }

        if (vap_changed) {
            LOGI("osw: drv: wifihal: %s: configuring vaps", pconf->phy_name);
            if (MEASURE(priv->wifi_createVAP, (rix, &vaps2)) != RETURN_OK) {
                LOGE("Failed to update VAP map for radio index: %d", rix);
            }
        }

        if (phy_changed) {
            LOGI("osw: drv: wifihal: %s: configuring radio", pconf->phy_name);
            if (MEASURE(priv->wifi_setRadioOperatingParameters, (rix, &radio_params)) != RETURN_OK) {
                LOGE("Failed to set radio operating params for radio index: %d", rix);
            }
        }

        osw_drv_report_phy_changed(drv, phy_name);
    }

    osw_drv_conf_free(conf);
}

static void
osw_drv_request_sta_deauth_cb(struct osw_drv *drv,
                              const char *phy_name,
                              const char *vif_name,
                              const struct osw_hwaddr *mac_addr,
                              int dot11_reason_code)
{
    const struct wifihal_3_0_priv *priv = osw_drv_get_priv(drv);
    int steer_index = 0; /* FIXME: ??? */
    int ap_index = vif_name_to_ap_index(priv, vif_name);
    wifi_disconnectType_t type = DISCONNECT_TYPE_DEAUTH;
    mac_address_t mac = {0};
    INT ret;

    memcpy(&mac, mac_addr->octet, MAC_LEN);
    ret = MEASURE(priv->wifi_steering_clientDisconnect, (steer_index, ap_index, mac,
                                                         type, dot11_reason_code));
    WARN_ON(ret != RETURN_OK);
}

static void
osw_drv_fill_tlv_chan_stats(const struct wifihal_3_0_priv *priv,
                            struct osw_tlv *t,
                            int rix)
{
    wifi_radio_operationParam_t params = {0};
    wifi_channelMap_t chans[128] = {0};
    wifi_channelStats_t stats[128] = {0};
    char phy_name[MAXIFACENAMESIZE] = {0};
    const size_t n_chans = ARRAY_SIZE(chans);
    const size_t n_stats = ARRAY_SIZE(stats);
    size_t i;
    size_t j;

    if (MEASURE(priv->wifi_getRadioOperatingParameters, (rix, &params)) != RETURN_OK)
        return;

    if (WARN_ON(MEASURE(priv->wifi_getRadioIfName, (rix, phy_name)) != RETURN_OK))
        return;

    /* FIXME: This fails when radio is down. This should
     * generate WARN_ON() only if the radio is expected to
     * be up?
     */
    if (MEASURE(priv->wifi_getRadioChannels, (rix, chans, n_chans)) != RETURN_OK)
        return;

    for (i = 0, j = 0; i < n_chans; i++) {
        if (chans[i].ch_number == 0) continue;

        stats[j].ch_number = chans[i].ch_number;
        j++;

        if (WARN_ON(j == n_stats)) /* shouldn't happen */
            break;
    }

    if (WARN_ON(MEASURE(priv->wifi_getRadioChannelStats, (rix, stats, n_stats)) != RETURN_OK))
        return;

    for (i = 0; i < n_stats; i++) {
        if (stats[i].ch_number == 0) continue;

        const int chan = stats[i].ch_number;
        const int freq_mhz = chan_to_mhz(params.band, chan);

        size_t start_chan = osw_tlv_put_nested(t, OSW_STATS_CHAN);
        {
            const unsigned long active = stats[i].ch_utilization_total / 1000;
            const float noise = stats[i].ch_noise;

            osw_tlv_put_string(t, OSW_STATS_CHAN_PHY_NAME, phy_name);
            osw_tlv_put_u32(t, OSW_STATS_CHAN_FREQ_MHZ, freq_mhz);
            osw_tlv_put_u32(t, OSW_STATS_CHAN_ACTIVE_MSEC, active);
            osw_tlv_put_float(t, OSW_STATS_CHAN_NOISE_FLOOR_DBM, noise);

            size_t start_cnt = osw_tlv_put_nested(t, OSW_STATS_CHAN_CNT_MSEC);
            {
                const unsigned long busy = stats[i].ch_utilization_busy / 1000;
                const unsigned long tx = stats[i].ch_utilization_busy_tx / 1000;
                const unsigned long rx = stats[i].ch_utilization_busy_rx / 1000;
                const unsigned long inbss = stats[i].ch_utilization_busy_self / 1000;

                osw_tlv_put_u32(t, OSW_STATS_CHAN_CNT_TX, tx);
                osw_tlv_put_u32(t, OSW_STATS_CHAN_CNT_RX, rx);
                osw_tlv_put_u32(t, OSW_STATS_CHAN_CNT_RX_INBSS, inbss);
                osw_tlv_put_u32(t, OSW_STATS_CHAN_CNT_BUSY, busy);
            }
            osw_tlv_end_nested(t, start_cnt);
        }
        osw_tlv_end_nested(t, start_chan);
    }
}

static wifi_freq_bands_t
band_str_to_enum(const char *str)
{
    /* This is crude, but the string is farily free-form as
     * far as documentation goes. This should be good
     * enough.
     */
    const int b = atoi(str);
    if (b == 2) return WIFI_FREQUENCY_2_4_BAND;
    if (b == 5) return WIFI_FREQUENCY_5_BAND;
    if (b == 6) return WIFI_FREQUENCY_6_BAND;
    WARN_ON(1);
    return WIFI_FREQUENCY_2_4_BAND;
}

static uint32_t
rssi_to_snr_20mhz(const INT rssi, const INT nf)
{
    /* This shouldn't happen, but in case SNR is handed over
     * on accident, try to guess it and return a sane value.
     * Chances of RSSI being > 10dBm are really slim and
     * would require devices to sit next to each other,
     * antennas touching, on 2.4GHz. Not a real world case
     * to worry about. */
    if (rssi > 10) return rssi;

    const int fixed_nf = osw_channel_nf_20mhz_fixup(nf);

    /* This normally shouldn't happen, but
     * sometimes driver report incoherent data, or
     * sometimes the data source of RSSI is
     * different from NF causing them to naturally
     * be out of sync and in worst case underflow.
     */
    if (fixed_nf > rssi) {
        /* The difference can be a few dB so let
         * it go. But if the difference is greater
         * then there's probably a bigger
         * underlying issue to be addressed.
         */
        WARN_ON((fixed_nf - rssi) > 10);
        return 0;
    }

    return rssi - fixed_nf;
}

static void
osw_drv_fill_tlv_bss_scan_entry(struct osw_tlv *t,
                                const char *phy_name,
                                const wifi_neighbor_ap2_t *bss)
{
    struct osw_hwaddr bssid = {0};
    const int n = sscanf(bss->ap_BSSID,
                         "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                         &bssid.octet[0], &bssid.octet[1], &bssid.octet[2],
                         &bssid.octet[3], &bssid.octet[4], &bssid.octet[5]);
    if (n != 6) return;

    const wifi_freq_bands_t band = band_str_to_enum(bss->ap_OperatingFrequencyBand);
    const int freq_mhz = chan_to_mhz(band, bss->ap_Channel);
    const int width_mhz = atoi(bss->ap_OperatingChannelBandwidth); /* FIXME: 80+80? */
    const char *ssid = bss->ap_SSID;
    const size_t ssid_len = strnlen(bss->ap_SSID, sizeof(bss->ap_SSID));
    const uint32_t snr_db = rssi_to_snr_20mhz(bss->ap_SignalStrength,
                                              bss->ap_Noise);

    LOGT("%s: %s: bssid=%s ssid=%.*s(%zu) freq=%d width=%d "
         "signal=%d noise=%d snr=%u",
         __func__,
         phy_name,
         bss->ap_BSSID,
         (int)ssid_len, ssid, ssid_len,
         freq_mhz,
         width_mhz,
         bss->ap_SignalStrength,
         bss->ap_Noise,
         snr_db);

    size_t s = osw_tlv_put_nested(t, OSW_STATS_BSS_SCAN);
    osw_tlv_put_string(t, OSW_STATS_BSS_SCAN_PHY_NAME, phy_name);
    osw_tlv_put_hwaddr(t, OSW_STATS_BSS_SCAN_MAC_ADDRESS, &bssid);
    osw_tlv_put_u32(t, OSW_STATS_BSS_SCAN_FREQ_MHZ, freq_mhz);
    osw_tlv_put_u32(t, OSW_STATS_BSS_SCAN_WIDTH_MHZ, width_mhz);
    osw_tlv_put_u32(t, OSW_STATS_BSS_SCAN_SNR_DB, snr_db);
    osw_tlv_put_buf(t, OSW_STATS_BSS_SCAN_SSID, ssid, ssid_len);
    // TODO: OSW_STATS_BSS_SCAN_IES
    osw_tlv_end_nested(t, s);
}

static void
osw_drv_fill_tlv_bss_scan_stats(const struct wifihal_3_0_priv *priv,
                                struct osw_tlv *t,
                                int rix)
{
    char phy_name[MAXIFACENAMESIZE] = {0};

    if (MEASURE(priv->wifi_getRadioIfName, (rix, phy_name)) != RETURN_OK)
        return;

    LOGD("%s: rix=%d phy_name=%s", __func__, rix, phy_name);

    wifi_neighbor_ap2_t *bss = NULL;
    UINT n_bss = 0;
    if (MEASURE(priv->wifi_getNeighboringWiFiStatus, (rix, &bss, &n_bss)) != RETURN_OK)
        return;

    UINT i;
    for (i = 0; i < n_bss; i++) {
        osw_drv_fill_tlv_bss_scan_entry(t, phy_name, &bss[i]);
    }

    free(bss);
}

static void
osw_drv_fill_tlv_sta_stats(const struct wifihal_3_0_priv *priv,
                           struct osw_tlv *t,
                           int rix)
{
    wifi_vap_info_map_t map = {0};
    char phy_name[MAXIFACENAMESIZE] = {0};

    if (MEASURE(priv->wifi_getRadioIfName, (rix, phy_name)) != RETURN_OK) return;
    if (MEASURE(priv->wifi_getRadioVapInfoMap, (rix, &map)) != RETURN_OK) return;
    if (WARN_ON(map.num_vaps > ARRAY_SIZE(map.vap_array))) return;

    unsigned int i;
    for (i = 0; i < map.num_vaps; i++) {
        const wifi_vap_info_t *vap = &map.vap_array[i];
        const char *vif_name = vap->vap_name;
        const wifi_vap_index_t vix = vap->vap_index;
        wifi_associated_dev3_t *stas = NULL;
        UINT n_sta = 0;

        if (MEASURE(priv->wifi_getApAssociatedDeviceDiagnosticResult3, (vix, &stas, &n_sta)) != RETURN_OK) {
            LOGE("%s: cannot get associated device diagnostic result for vap_index: %d",
                    __func__, vix);
            continue;
        }

        UINT j;
        for (j = 0; j < n_sta; j++) {
            const wifi_associated_dev3_t *sta = &stas[j];

            if (sta->cli_Active == false) continue;

            struct osw_hwaddr addr;
            memcpy(addr.octet, sta->cli_MACAddress, MAC_LEN);
            const INT snr = sta->cli_SNR;
            const UINT tx_rate = sta->cli_LastDataDownlinkRate;
            const UINT rx_rate = sta->cli_LastDataUplinkRate;
            const ULONG tx_bytes = sta->cli_BytesSent;
            const ULONG rx_bytes = sta->cli_BytesReceived;
            const ULONG tx_frames = sta->cli_PacketsSent;
            const ULONG rx_frames = sta->cli_PacketsReceived;
            const ULONG tx_retries = sta->cli_RetransCount;

            size_t s = osw_tlv_put_nested(t, OSW_STATS_STA);
            osw_tlv_put_string(t, OSW_STATS_STA_PHY_NAME, phy_name);
            osw_tlv_put_string(t, OSW_STATS_STA_VIF_NAME, vif_name);
            osw_tlv_put_hwaddr(t, OSW_STATS_STA_MAC_ADDRESS, &addr);

            osw_tlv_put_u32(t, OSW_STATS_STA_SNR_DB, snr);
            osw_tlv_put_u32(t, OSW_STATS_STA_TX_BYTES, tx_bytes);
            osw_tlv_put_u32(t, OSW_STATS_STA_RX_BYTES, rx_bytes);
            osw_tlv_put_u32(t, OSW_STATS_STA_TX_FRAMES, tx_frames);
            osw_tlv_put_u32(t, OSW_STATS_STA_RX_FRAMES, rx_frames);
            osw_tlv_put_u32(t, OSW_STATS_STA_TX_RATE_MBPS, tx_rate);
            osw_tlv_put_u32(t, OSW_STATS_STA_RX_RATE_MBPS, rx_rate);
            osw_tlv_put_u32(t, OSW_STATS_STA_TX_RETRIES, tx_retries);

            osw_tlv_end_nested(t, s);
        }

        free(stas);
    }
}

static void
osw_drv_request_stats_cb(struct osw_drv *drv,
                         unsigned int stats_mask) // look for OSW_STATS_ENUM_H_INCLUDED
{
    const struct wifihal_3_0_priv *priv = osw_drv_get_priv(drv);
    struct osw_tlv t = {0};
    int rix;

    for (rix = 0; rix < MAX_NUM_RADIOS; rix++) {
        if (stats_mask & (1 << OSW_STATS_CHAN))
            osw_drv_fill_tlv_chan_stats(priv, &t, rix);
        if (stats_mask & (1 << OSW_STATS_BSS_SCAN))
            osw_drv_fill_tlv_bss_scan_stats(priv, &t, rix);
        if (stats_mask & (1 << OSW_STATS_STA))
            osw_drv_fill_tlv_sta_stats(priv, &t, rix);
    }

    osw_drv_report_stats(drv, &t);
    osw_tlv_fini(&t);
}

static void
osw_drv_push_frame_tx_cb(struct osw_drv *drv,
                         const char *phy_name,
                         const char *vif_name,
                         struct osw_drv_frame_tx_desc *desc)
{
    const size_t dot11_header_size = sizeof(struct osw_drv_dot11_frame_header);
    const uint8_t *payload = osw_drv_frame_tx_desc_get_frame(desc);
    const struct osw_drv_dot11_frame *frame = (const struct osw_drv_dot11_frame*) payload;
    const struct osw_channel *channel = osw_drv_frame_tx_desc_get_channel(desc);
    const UINT freq_mhz = channel ? channel->control_freq_mhz : 0;
    size_t payload_len = osw_drv_frame_tx_desc_get_frame_len(desc);
    INT ret;
    INT apIndex;
    mac_address_t sta_addr = {0};
    UCHAR *hal_payload;
    const struct wifihal_3_0_priv *priv = osw_drv_get_priv(drv);

    if (vif_name == NULL) {
        LOGD("osw: drv: wifi_hal_3_0: vif_name is required, failing");
        osw_drv_report_frame_tx_state_failed(drv);
        return;
    }

    apIndex = vif_name_to_ap_index(priv, vif_name);

    if (dot11_header_size >= payload_len) {
        LOGD("osw: drv: wifi_hal_3_0: frame tx too small, failing");
        osw_drv_report_frame_tx_state_failed(drv);
        return;
    }

    /* FIXME: This assumes the payload is an action frame
     * but it doesn't have to be.
     */
    payload += dot11_header_size;
    payload_len -= dot11_header_size;

    /* HAL API has non-const arguments. Instead of casting
     * make a copy for be type-clean.
     */
    hal_payload = MEMNDUP(payload, payload_len);
    memcpy(&sta_addr, &frame->header.da, sizeof(sta_addr));

    ret = MEASURE(priv->wifi_sendActionFrame, (apIndex, sta_addr, freq_mhz, hal_payload, payload_len));
    if (ret == RETURN_OK)
        osw_drv_report_frame_tx_state_submitted(drv);
    else
        osw_drv_report_frame_tx_state_failed(drv);

    FREE(hal_payload);
}

const struct osw_drv_ops g_wifihal_3_0_ops = {
    .name = DRV_NAME,
    .init_fn = osw_drv_init_cb,
    .get_phy_list_fn = osw_drv_get_phy_list_cb,
    .get_vif_list_fn = osw_drv_get_vif_list_cb,
    .get_sta_list_fn = osw_drv_get_sta_list_cb,
    .request_phy_state_fn = osw_drv_request_phy_state_cb,
    .request_vif_state_fn = osw_drv_request_vif_state_cb,
    .request_sta_state_fn = osw_drv_request_sta_state_cb,
    .request_config_fn = osw_drv_request_config_cb,
    .request_sta_deauth_fn = osw_drv_request_sta_deauth_cb,
    .request_stats_fn = osw_drv_request_stats_cb,
    .push_frame_tx_fn = osw_drv_push_frame_tx_cb,
};

OSW_DRV_DEFINE(g_wifihal_3_0_ops);
