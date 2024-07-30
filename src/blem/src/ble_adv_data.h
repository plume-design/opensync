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

#ifndef BLE_ADV_DATA_H_INCLUDED
#define BLE_ADV_DATA_H_INCLUDED

#include <stdint.h>

/* Note: Values shall be stored in little-endian order.
 *         Bluetooth Core Specification | Vol 1, Part E, Section 2.9 Type Names
 *
 * Compile-time-constant macro alternative to htole16()
 */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define TO_LE16(x) (x)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define TO_LE16(x) ((uint16_t)(((x) >> 8) | ((x) << 8)))
#endif

/** @see ble_advertising_set_s */
typedef struct ble_advertising_set_s ble_advertising_set_t;

/**
 * Callback invoked when BLE advertising state changes
 *
 * @param[in] adv_set       Copy of the advertising set structure.
 * @param     active        Whether advertising is currently active.
 * @param     adv_tx_power  Actual advertising transmit power in 0.1 dBm steps.
 */
typedef void (*blem_ble_adv_on_state_t)(ble_advertising_set_t *adv_set, bool active, int16_t adv_tx_power);

/**
 * Advertising data structure for general OpenSync use cases
 */
typedef struct __attribute__((packed))
{
    /**
     * "Complete list of 16-bit Service UUIDs" advertising data element
     *
     * See Core Specification Supplement, Part A, Section 1.1 Complete List of 16-bit Service Class UUIDs.
     */
    struct __attribute__((packed))
    {
        const uint8_t len;  /*< = 1 + <Number of UUIDs> * 2 */
        const uint8_t type; /*< = 0x03 */
        uint16_t uuids[1];  /*< = <16-bit Service or Service Class UUID> */
    } service;

    /**
     * "Manufacturer Specific Data" advertising data element definition
     *
     * See Core Specification Supplement, Part A, Section 1.4 Manufacturer Specific Data.
     */
    struct __attribute__((packed))
    {
        const uint8_t len;     /*< = 1 + 2 + <Length of the data> = 1 + 2 + 20 */
        const uint8_t type;    /*< = 0xFF */
        uint16_t cid;          /*< = <Company Identifier Code> */
        const uint8_t data[0]; /*< ... <len - 3> bytes of company-specific data follows */
    } mfd;

    /**
     * "Manufacturer Specific Data" data - OpenSync BLE Beacon payload
     *
     * This structure is anonymous for ease of use in the code, as mostly
     * only these fields of the advertising data are used in the code.
     */
    struct __attribute__((packed))
    {
        const uint8_t version; /**< Version of this beacon data structure, fixed value 0x05 */
        char serial_num[12];   /**< ASCII-encoded node serial number, no null-termination required if 12-char long */
        uint8_t msg_type;      /**< Type of the payload currently present in the `msg` field */
        /** Node message data */
        struct __attribute__((packed))
        {
            uint8_t status;           /**< Connectivity status, when `msg_type` is 0x00 */
            const uint8_t _rfu[1];    /**< Unused field */
            uint8_t pairing_token[4]; /**< Random token used in pairing passkey generation */
        } msg;
    };
} ble_adv_data_general_t;

/**
 * Advertising data payload structure for OpenSync Proximity use case
 *
 * See Apple Developer - iBeacon | Proximity Beacon Specification, Release R1.
 */
typedef struct __attribute__((packed))
{
    const uint8_t length;       /**< Advertising data element length: 0x1A */
    const uint8_t type;         /**< Advertising data element type: 0xFF (Manufacturer Specific Data) */
    const uint16_t company_id;  /**< Company Identifier Code: 0x004C (Apple, Inc.) */
    const uint16_t beacon_type; /**< Beacon Type: 0x1502 (Proximity Beacons) */
    uint8_t proximity_uuid[16]; /**< A universally unique identifier that represents the beacon's identifier */
    uint16_t major;             /**< The most significant value associated with the beacon */
    uint16_t minor;             /**< The least significant value associated with the beacon */
    int8_t measured_power;      /**< Received signal strength indicator (RSSI) value at 1 meter from the node */
} ble_adv_data_proximity_t;

/**
 * BLE Advertising Data (AD) structure
 *
 * The Advertising, Periodic Advertising, and Scan Response data consists of
 * a sequence of AD structures. Each AD structure shall have a Length field
 * of one octet, which contains the Length value and shall not be zero, and
 * a Data field of Length octets. The first octet of the Data field shall
 * contain the AD type field. The content of the remaining Length - 1 octets
 * in the Data field depends on the value of the AD type field and is called
 * the AD data.
 *
 * See Bluetooth Core Specification | Vol 3, Part C, Section 11 Advertising and Scan Response Data Format.
 */
typedef struct __attribute__((packed))
{
    uint8_t len;        /**< Length of the AD structure
                         *
                         *   The length value excludes this length field itself (1 octet),
                         *   but included the `type` field (1 octet) and number of octets
                         *   in the `data` field. It shall therefore never be 0.
                         */
    const uint8_t type; /**< Type identifier of the AD structure, as defined in Assigned Numbers
                         *
                         *   After this octet, the `len - 1` octets of AD structure data follows.
                         *   The AD type data formats and meanings are defined in Core Specification
                         *   Supplement, Part A, Section 1 Data Types Definitions and Formats.
                         */
    /* uint8_t data[] */
} ble_ad_structure_t;

/**
 * The full Advertising Data packet payload
 *
 * This packet represents raw AdvData part of the advertising PDU, excluding the
 * Flags AD structure, which is expected to be prepended by the Bluetooth stack.
 *
 * Used when advertising in modes:
 *  - Non-connectable undirected advertising (ADV_NONCONN_IND)
 *  - Scannable undirected advertising event (ADV_SCAN_IND)
 *  - Connectable undirected advertising (ADV_IND)
 *
 * See Bluetooth Core Specification | Vol 3, Part C, Section 11 Advertising and Scan Response Data Format.
 */
typedef union
{
    const uint8_t raw[28];       /**< Raw advertising data
                                  *
                                  *   This is a maximum-size buffer for the advertising data, but only the
                                  *   significant part of the data (valid AD structures) should be sent
                                  *   over the air, excluding trailing zero padding.
                                  *
                                  *   The size of the buffer is 28 bytes, because the Flags AD element (3 bytes: 020106)
                                  *   is automatically prepended by the Bluetooth stack (Core Specification Supplement,
                                  *   Part A, Section 1.3 Flags)
                                  */
    const ble_ad_structure_t ad; /**< The first AD structure */

    ble_adv_data_general_t general;     /**< Advertising data structure for general OpenSync use cases */
    ble_adv_data_proximity_t proximity; /**< Advertising data structure for OpenSync Proximity use case */
} ble_advertising_data_t;

/**
 * The Scan Response Data packet payload
 *
 * Used when advertising in modes:
 *  - Scannable undirected advertising event (ADV_SCAN_IND)
 *  - Connectable undirected advertising (ADV_IND)
 *
 * See Bluetooth Core Specification | Vol 3, Part C, Section 11 Advertising and Scan Response Data Format.
 */
typedef union
{
    const uint8_t raw[31];       /**< Raw scan response data
                                  *
                                  *   This is a maximum-size buffer for the scan response data, but only
                                  *   the significant part of the data (valid AD structures) should be
                                  *   sent over the air, excluding trailing zero padding.
                                  */
    const ble_ad_structure_t ad; /**< The first AD structure */

    /**
     * "Complete Local Name" advertising data element
     *
     * See Core Specification Supplement, Part A, Section 1.2 Complete Local Name.
     */
    struct __attribute__((packed))
    {
        uint8_t len;        /**< Length of this AD structure, <number of characters in `name`> + 1 */
        const uint8_t type; /**< Type of this AD structure, fixed value 0x09 */
        char name[31 - 2];  /**< Advertised complete device name
                             *
                             *   This field can be shorter than 29 octets and does not have to be
                             *   null-terminated, its actual length is stored in the `len` field.
                             */
    } cln;
} ble_scan_response_data_t;

/**
 * A set of full advertising data and parameters
 */
struct ble_advertising_set_s
{
    uint16_t interval;    /**< Advertising interval in milliseconds - @see osp_ble_set_advertising_params */
    int16_t adv_tx_power; /**< Advertising transmit power in 0.1 dBm steps - @see osp_ble_set_advertising_tx_power */
    blem_ble_adv_on_state_t on_state_cb; /**< Optional callback invoked when advertising state changes */
    /** Advertising parameters */
    struct
    {
        bool enabled;     /**< Whether advertising is enabled - @see osp_ble_set_advertising_params */
        bool connectable; /**< Whether to advertise as connectable - @see osp_ble_set_connectable */
        bool changed;     /**< Flag indicating whether `enabled` or `connectable` values changed and shall be updated */
    } mode;
    /** Advertising data payload - @see osp_ble_set_advertising_data */
    struct
    {
        ble_advertising_data_t data; /**< Advertising data payload - @see osp_ble_set_advertising_data */
        bool changed;                /**< Flag indicating whether `data` value changed and shall be updated */
    } adv;
    /** Scan response data payload - @see osp_ble_set_scan_response_data */
    struct
    {
        bool enabled;                  /**< Whether scan response is enabled - @see osp_ble_set_advertising_params */
        ble_scan_response_data_t data; /**< Scan response data payload - @see osp_ble_set_scan_response_data */
        bool changed; /**< Flag indicating whether `enabled` or `data` values changed and shall be updated */
    } scan_rsp;
};

/**
 * Advertising sets with their priorities (0 is the highest)
 *
 * Only one - the first enabled - advertising set is used at a time.
 */
typedef enum
{
    BLE_ADVERTISING_SET_GENERAL = 0,   /**< Advertising set of type @ref ble_adv_data_general_t */
    BLE_ADVERTISING_SET_PROXIMITY = 1, /**< Advertising set of type @ref ble_adv_data_proximity_t */
} ble_advertising_set_id_t;

/**
 * Inspect the advertising packet payload to calculate the length of its significant part
 *
 * @param[in] data        Advertising payload to inspect.
 * @param[in] max_length  Maximum allowed length of the advertising payload.
 *
 * @return Length of the advertising payload, 0 if the payload is invalid (logged internally).
 */
uint8_t ble_adv_data_get_length(const ble_ad_structure_t *const first_ad, const size_t max_length);

/** Advertising sets */
extern ble_advertising_set_t g_advertising_sets[2];

#endif /* BLE_ADV_DATA_H_INCLUDED */
