#ifndef PKTGEN_H_INCLUDED
#define PKTGEN_H_INCLUDED

enum pktgen_status
{
    PKTGEN_STATUS_UNDEFINED = 0,
    PKTGEN_STATUS_SUCCEED,
    PKTGEN_STATUS_BUSY,
    PKTGEN_STATUS_FAILED,
};

/**
 * @brief This API will check the pktgen support and initializes pktgen library
 *
 * @return 0 on success, -1 otherwise.
 */
int pktgen_init(void);

/**
 * @brief This API will check if pktgen is currently in running state
 *
 * @return 1 if is running, 0 otherwise
 */
int pktgen_is_running(void);

/**
 * @brief This API will start pktgen blasting thread
 *
 * @param if_name interface name where the target MAC is connected to
 * @param dest_mac destination MAC address
 * @param packet_size packet size
 *
 * @return PKTGEN_STATUS_SUCCEED on success, appropriate error code otherwise.
 */
enum pktgen_status pktgen_start_wifi_blast(char *if_name, char *dest_mac, int packet_size);

/**
 * @brief This API will stop pktgen blasting thread
 *
 * @return 0 on success, -1 otherwise.
 */
int pktgen_stop_wifi_blast(void);

/**
 * @brief This API will free all internally used resources
 *
 * @return 0 on success, -1 otherwise.
 */
int pktgen_uninit(void);

#endif /* PKTGEN_H_INCLUDED */
