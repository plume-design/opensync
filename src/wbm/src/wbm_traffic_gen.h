#ifndef WBM_TRAFFIC_GEN_H_INCLUDED
#define WBM_TRAFFIC_GEN_H_INCLUDED

#include "dpp_types.h"

/**
 * @brief Prototype of callback used by traffic generator
 */
typedef void (*wbm_traffic_gen_start_t)(int error, void *ctx);

/**
 * @brief This API will start traffic generator
 *
 * @param radio_type[in]    radio interface type (2G, 5G etc)
 * @param if_name[in]       virtual interface name
 * @param phy_name[in]      physical interface name
 * @param mac[in]           colon separated mac address
 * @param packet_sz[in]     packet size
 * @param done_cb[in]       callback function
 * @param ctx[in]           callback function context
 */
void wbm_traffic_gen_async_start(
        radio_type_t            radio_type,
        char                    *if_name,
        char                    *phy_name,
        char                    *mac,
        int                     packet_sz,
        wbm_traffic_gen_start_t done_cb,
        void                    *ctx);

/**
 * @brief This API will stop traffic generation
 */
void wbm_traffic_gen_stop(void);

/**
 * @brief This API will return context used by traffic generation
 *
 * @return context
 */
void *wbm_traffic_gen_get_ctx(void);

/**
 * @brief Initializes traffic_gen internals
 *
 * @return 0 on success, -1 otherwise.
 */
int wbm_traffic_gen_init(void);

/**
 * @brief Deinitializes traffic_gen internals
 */
void wbm_traffic_gen_uninit(void);

#endif /* WBM_TRAFFIC_GEN_H_INCLUDED */
