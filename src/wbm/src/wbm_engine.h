#ifndef WBM_ENGINE_H_INCLUDED
#define WBM_ENGINE_H_INCLUDED

#include <stdint.h>

#include "dpp_types.h"
#include "osn_types.h"

#include "wbm_stats.h"

/* Output parameters */

typedef enum
{
    WBM_STATE_WAITING = 0,
    WBM_STATE_IN_PROGRESS,
    WBM_STATE_DONE,
} wbm_state_t;

typedef enum
{
    WBM_STATUS_SUCCEED      = 0,                /* The test is finished successfully */
    WBM_STATUS_CANCELED     = 1,                /* The request was canceled */
    WBM_STATUS_FAILED       = 2,                /* Internal error happened */
    WBM_STATUS_BUSY         = 3,                /* The device is overloaded (CPU; RAM mem) */
    WBM_STATUS_NO_CLIENT    = 4,                /* The client is not found */
    WBM_STATUS_WRONG_ARG    = 5,                /* Incorrect argument value */
    WBM_STATUS_UNDEFINED
} wbm_status_t;

typedef enum
{
    WBM_TS_RECEIVED = 0,
    WBM_TS_STARTED,
    WBM_TS_FINISHED,
    WBM_TS_MAX
} wbm_ts_t;

typedef struct wbm_plan
{
    char                    plan_id[37];        /* This is an unique ID per request on POD */
    int                     sample_count;       /* Number of samples for the test */
    int                     duration;           /* Test duration in milliseconds */
    int                     packet_size;        /* Size of packet to be send in bytes */
    /* Optional */
    int                     threshold_cpu;      /* Max acceptable CPU usage [%] */
    int                     threshold_mem;      /* Min acceptable free RAM memory [KB] */
    /* Private: */
    uint32_t                ref_cnt;            /* Reference counter to manage the object */
} wbm_plan_t;

typedef struct wbm_result_sample
{
    /* Calculated data */
    double                  throughput;         /* Throughput [Mbps] */
    uint64_t                tx_retrans;         /* Number of re-sent frames for the sample */
    /* Raw data */
    uint64_t                tx_bytes;           /* Number of sent bytes to the client */
    uint64_t                tx_retries;         /* Number of re-sent frames to the client */
    uint64_t                ts;                 /* Timestamp [ms] of the sample collecting */
} wbm_result_sample_t;

typedef struct wbm_result
{
    wbm_result_sample_t     *sample;            /* Array of results for each sample */
    int                     sample_cnt;         /* Number of filled elements in the array */

    wbm_stats_health_t      health;             /* Device health related stats */
    wbm_stats_radio_t       radio;              /* Radio interface related stats */
    wbm_stats_client_t      client;             /* Client (STA) related stats */
} wbm_result_t;

typedef struct wbm_info
{
    char                    if_name[32];        /* Virtual interface name */
    char                    radio_name[32];     /* Radio interface name */
    radio_type_t            radio_type;         /* Radio band in use (2G, 5G, 5GL, 5GU) */
    radio_protocols_t       radio_proto;        /* WiFi standard (80211n, 80211ac ..) */
    uint32_t                chan;               /* Radio channel that is currently used */
    radio_chanwidth_t       chanwidth;          /* The width of the current channel */
} wbm_info_t;

typedef struct wbm_node
{
    wbm_plan_t              *plan;              /* Common data per Plan ID */
    int                     step_id;            /* ID of the request (unique per plan_id) */
    char                    dest_mac[OSN_MAC_ADDR_LEN];  /* Client's MAC address */
    int                     dest_mac_orig_raw;  /* Set if orig MAC format: "A1B2C3D4E5F6" */

    uint64_t                ts[WBM_TS_MAX];     /* Timestamps [ms] since epoch */
    wbm_result_t            result;             /* Blaster test results */
    wbm_info_t              info;               /* Test related information */
    wbm_state_t             state;              /* Current state of the request */
    wbm_status_t            status;             /* Final status of the request */
    char                    status_desc[256];   /* Status description */
} wbm_node_t;

/* Input parameters */

typedef void (*wbm_request_cb_t)(wbm_node_t *node);

typedef struct wbm_config
{
    char                    *key;
    char                    *value;
} wbm_config_t;

typedef struct wbm_request
{
    char                    *plan_id;           /* This is an unique ID per request on POD */
    int                     step_id;            /* ID of the request (unique per Plan_ID) */
    char                    *dest_mac;          /* Client's MAC address */
    uint64_t                timestamp;          /* Timestamp [ms] of the request creating */
    int                     sample_count;       /* Number of samples for the test */
    int                     duration;           /* Test duration in milliseconds */
    int                     packet_size;        /* Size of packet to be send in bytes */
    wbm_config_t            config[64];         /* Array of optional configs: key/value */

    wbm_request_cb_t        cb;                 /* Is called once the result is ready */
} wbm_request_t;

/* Public API */

int wbm_engine_request_add(wbm_request_t *request);
int wbm_engine_plan_is_active(char *plan_id);
void wbm_engine_plan_cancel(char *plan_id);

#endif /* WBM_ENGINE_H_INCLUDED */
