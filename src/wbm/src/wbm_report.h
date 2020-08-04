#ifndef WBM_REPORT_H_INCLUDED
#define WBM_REPORT_H_INCLUDED

#include "wbm_engine.h"

typedef enum
{
    WBM_REPORT_STATUS_SUCCESS = 0,
    WBM_REPORT_STATUS_ERR_CONN,
    WBM_REPORT_STATUS_ERR_SEND,
    WBM_REPORT_STATUS_ERR_INPUT,
    WBM_REPORT_STATUS_ERR_INTERNAL,
    WBM_REPORT_STATUS_UNDEFINED
} wbm_report_status_t;

int wbm_report_init(void);
wbm_report_status_t wbm_report_publish(wbm_node_t *node);

#endif /* WBM_REPORT_H_INCLUDED */
