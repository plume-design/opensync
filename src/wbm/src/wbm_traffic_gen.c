#include <ev.h>
#include <float.h>
#include <inttypes.h>

#include "log.h"
#include "util.h"
#include "pktgen.h"
#include "dpp_client.h"
#include "target_native.h"
#include "target_common.h"
#include "osn_types.h"

#include "wbm_traffic_gen.h"

#define PKTGEN_WAIT_MAX_MS          1000
#define PKTGEN_WAIT_MS              50
#define TRAFFIC_CONSEQ_CNT          2

typedef enum {
    TRAFFIC_GEN_STATE_FAILED = 0,
    TRAFFIC_GEN_STATE_UNDEFINED,
    TRAFFIC_GEN_STATE_STOPPING,
    TRAFFIC_GEN_STATE_STOPPED,
    TRAFFIC_GEN_STATE_STARTING,
    TRAFFIC_GET_STATE_WAIT_TRAFFIC,
    TRAFFIC_GEN_STATE_STARTED,
} traffic_gen_state_t;

typedef struct traffic_gen_config
{
    radio_type_t            radio_type;
    char                    if_name[32];
    char                    phy_name[32];
    char                    dest_mac[OSN_MAC_ADDR_LEN];
    int                     packet_size;

    traffic_gen_state_t     pktgen_state;
    int                     pktgen_wait;

    wbm_traffic_gen_start_t pktgen_done_cb;
    void                    *pktgen_context;
} traffic_gen_config_t;

static ev_timer                 g_timer_pktgen_poll;
static traffic_gen_config_t     g_config;

static void wbm_traffic_gen_timer_set(ev_timer *timer, uint32_t timeout_ms)
{
    double delay;

    delay = (timeout_ms > 0) ? ((timeout_ms / 1000.0) + (ev_time() - ev_now(EV_DEFAULT))) : DBL_MIN;
    ev_timer_stop(EV_DEFAULT, timer);
    ev_timer_set(timer, delay, 0);
    ev_timer_start(EV_DEFAULT, timer);
}

static char* wbm_traffic_gen_state_get(void)
{
    switch (g_config.pktgen_state)
    {
        case TRAFFIC_GEN_STATE_FAILED:
            return "FAILED";
        case TRAFFIC_GEN_STATE_STOPPING:
            return "STOPPING";
        case TRAFFIC_GEN_STATE_STOPPED:
            return "STOPPED";
        case TRAFFIC_GEN_STATE_STARTING:
            return "STARTING";
        case TRAFFIC_GET_STATE_WAIT_TRAFFIC:
            return "WAIT_TRAFFIC";
        case TRAFFIC_GEN_STATE_STARTED:
            return "STARTED";
        default:
            return "UNDEFINED";
    }
}

static int wbm_traffic_gen_poll_schedule(void)
{
    g_config.pktgen_wait += PKTGEN_WAIT_MS;

    LOGD("%s: Scheduling poll function. State[%s] Wait[%d]ms Wait_MAX[%d]ms",
         __func__, wbm_traffic_gen_state_get(), g_config.pktgen_wait, PKTGEN_WAIT_MAX_MS);

    if (g_config.pktgen_wait > PKTGEN_WAIT_MAX_MS) {
        return -1;
    }

    wbm_traffic_gen_timer_set(&g_timer_pktgen_poll, PKTGEN_WAIT_MS);
    return 0;
}

static int wbm_traffic_gen_tx_bytes_get(uint64_t *tx)
{
    int res;
    osn_mac_addr_t mac;
    dpp_client_stats_t stats;

    osn_mac_addr_from_str(&mac, g_config.dest_mac);
    res = target_stats_client_get(
            g_config.radio_type,
            g_config.if_name,
            g_config.phy_name,
            mac.ma_addr,
            &stats);
    if (res != true) {
        return -1;
    }

    *tx = stats.bytes_tx;
    return 0;
}

static void wbm_traffic_gen_poll(struct ev_loop *loop, ev_timer *timer, int revents)
{
    static int traffic_count;
    static uint64_t traffic_tx;
    uint64_t tx;
    int error;

    LOGD("%s: Entering poll function. State[%s]", __func__, wbm_traffic_gen_state_get());

    if (   (g_config.pktgen_state == TRAFFIC_GEN_STATE_FAILED)
        || (g_config.pktgen_state == TRAFFIC_GEN_STATE_UNDEFINED))
    {
        if (!pktgen_is_running()) {
            g_config.pktgen_state = TRAFFIC_GEN_STATE_STOPPED;
        }
    }

    if (g_config.pktgen_state == TRAFFIC_GEN_STATE_STOPPING)
    {
        if (!pktgen_is_running()) {
            g_config.pktgen_state = TRAFFIC_GEN_STATE_STOPPED;
            g_config.pktgen_wait = 0;
        } else if (wbm_traffic_gen_poll_schedule() == 0) {
            return;
        }
    }

    if (g_config.pktgen_state == TRAFFIC_GEN_STATE_STOPPED)
    {
        error = pktgen_start_wifi_blast(g_config.if_name, g_config.dest_mac, g_config.packet_size)
                != PKTGEN_STATUS_SUCCEED;
        g_config.pktgen_state = error ? TRAFFIC_GEN_STATE_FAILED : TRAFFIC_GEN_STATE_STARTING;
    }

    if (g_config.pktgen_state == TRAFFIC_GEN_STATE_STARTING)
    {
        if (pktgen_is_running()) {
            if (wbm_traffic_gen_tx_bytes_get(&traffic_tx) == 0) {
                traffic_count = 0;
                g_config.pktgen_state = TRAFFIC_GET_STATE_WAIT_TRAFFIC;
            }
        } else if (wbm_traffic_gen_poll_schedule() == 0) {
            return;
        }
    }

    if (g_config.pktgen_state == TRAFFIC_GET_STATE_WAIT_TRAFFIC)
    {
        if (wbm_traffic_gen_tx_bytes_get(&tx) == 0)
        {
            traffic_count = (traffic_tx != tx) ? (traffic_count + 1) : 0;
            LOGD("%s: Wait traffic: bytes old/new [%"PRIu64"/%"PRIu64"]; count new/expect [%d/%d]",
                 __func__, traffic_tx, tx, traffic_count, TRAFFIC_CONSEQ_CNT);
            traffic_tx = tx;

            if (traffic_count == TRAFFIC_CONSEQ_CNT) {
                g_config.pktgen_state = TRAFFIC_GEN_STATE_STARTED;
            } else if (wbm_traffic_gen_poll_schedule() == 0) {
                return;
            }
        }
    }

    error = g_config.pktgen_state != TRAFFIC_GEN_STATE_STARTED;
    g_config.pktgen_done_cb(error, g_config.pktgen_context);

    if (error) {
        wbm_traffic_gen_stop();
    }
}

void wbm_traffic_gen_async_start(
        radio_type_t            radio_type,
        char                    *if_name,
        char                    *phy_name,
        char                    *mac,
        int                     packet_sz,
        wbm_traffic_gen_start_t done_cb,
        void                    *ctx)
{
    LOGD("%s: Traffic generator. State[%s]", __func__, wbm_traffic_gen_state_get());

    if (g_config.pktgen_done_cb != NULL)
    {
        LOGE("%s: Traffic generator has already been started. State[%s]",
             __func__, wbm_traffic_gen_state_get());
        done_cb(1, ctx);
        return;
    }

    g_config.radio_type = radio_type;
    STRSCPY(g_config.if_name, if_name);
    STRSCPY(g_config.phy_name, phy_name);
    STRSCPY(g_config.dest_mac, mac);
    g_config.packet_size = packet_sz;

    g_config.pktgen_done_cb = done_cb;
    g_config.pktgen_context = ctx;
    g_config.pktgen_wait = 0;
    wbm_traffic_gen_timer_set(&g_timer_pktgen_poll, 0);
}

void wbm_traffic_gen_stop(void)
{
    LOGD("%s: State[%s]", __func__, wbm_traffic_gen_state_get());

    if (g_config.pktgen_done_cb == NULL) {
        LOGW("%s: Traffic generator is not run. State[%s]", __func__, wbm_traffic_gen_state_get());
        return;
    }

    if (g_config.pktgen_state >= TRAFFIC_GEN_STATE_STARTING) {
        pktgen_stop_wifi_blast();
        g_config.pktgen_state = TRAFFIC_GEN_STATE_STOPPING;
    }

    ev_timer_stop(EV_DEFAULT, &g_timer_pktgen_poll);
    g_config.pktgen_done_cb = NULL;
    g_config.pktgen_context = NULL;
    g_config.pktgen_wait = 0;
}

void *wbm_traffic_gen_get_ctx(void)
{
    return g_config.pktgen_context;
}

int wbm_traffic_gen_init(void)
{
    g_config.pktgen_done_cb = NULL;
    g_config.pktgen_context = NULL;
    g_config.pktgen_wait = 0;
    g_config.pktgen_state = TRAFFIC_GEN_STATE_UNDEFINED;
    ev_init(&g_timer_pktgen_poll, wbm_traffic_gen_poll);
    return pktgen_init();
}

void wbm_traffic_gen_uninit(void)
{
    if (g_config.pktgen_state >= TRAFFIC_GEN_STATE_STARTING) {
        pktgen_stop_wifi_blast();
    }

    pktgen_uninit();
    ev_timer_stop(EV_DEFAULT, &g_timer_pktgen_poll);
}
