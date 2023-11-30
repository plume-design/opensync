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

#ifndef OSP_OTBR_H_INCLUDED
#define OSP_OTBR_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <ev.h>

#include "osn_types.h"


/// @file
/// @brief OpenThread Border Router API
///
/// @addtogroup OSP
/// @{


// ===========================================================================
//  OpenThread Border Router API
// ===========================================================================

/// @defgroup OSP_OTBR  OpenThread Border Router API
/// OpenSync OpenThread Border Router API
/// @{


/** Represents a Thread device role */
enum osp_otbr_device_role_e {
    OSP_OTBR_DEVICE_ROLE_DISABLED = 0,  /**< The Thread stack is disabled */
    OSP_OTBR_DEVICE_ROLE_DETACHED = 1,  /**< Not currently participating in a Thread network/partition */
    OSP_OTBR_DEVICE_ROLE_CHILD = 2,     /**< The Thread Child role */
    OSP_OTBR_DEVICE_ROLE_ROUTER = 3,    /**< The Thread Router role */
    OSP_OTBR_DEVICE_ROLE_LEADER = 4,    /**< The Thread Leader role */
};

/** Routing Locator (RLOC) - IEEE 802.15.4 Short Address (0xFFFE = Invalid, 0xFFFF = Broadcast) */
typedef uint16_t rloc16_t;

/** Represents a Thread Network parameters */
struct otbr_osp_network_params_s {
    /** Thread Network Name as null-terminated C string (1-16 chars), mandatory */
    char network_name[16 + 1];
    /** IEEE 802.15.4 PAN ID, or `UINT16_MAX` for random */
    uint16_t pan_id;
    /** Extended PAN ID, or `UINT64_MAX` for random */
    uint64_t ext_pan_id;
    /** Thread Network Key, or all 0x00 for random */
    uint8_t network_key[16];
    /** Mesh Local Prefix (most significant 64 bits of the IPv6 address), or all 0x00 for random */
    osn_ip6_addr_t mesh_local_prefix;
    /** IEEE 802.15.4 Channel (1-26), or 0 for random */
    uint8_t channel;
    /** Channel Mask (0x1 = Channel 1, 0x4000000 = Channel 26), `UINT32_MAX` for all channels */
    uint32_t channel_mask;
    /** Commissioning Credential as null-terminated C string (6-255 chars), or `NULL` for random PSKc */
    const char *commissioning_psk;
};

/** Represents a TLV encoded (as specified by Thread) Active or Pending Operational Dataset */
struct osp_otbr_dataset_tlvs_s {
    uint8_t len;       /**< Size of Operational Dataset in bytes (0, if the dataset is not yet initialized) */
    uint8_t tlvs[254]; /**< Operational Dataset TLVs (only the first `len` bytes are valid, others undefined) */
};

/** Represents a radio link to another Thread device */
struct osp_otbr_link_s {
    rloc16_t rloc16;    /**< RLOC16 address of the device this link connects to */
    uint8_t lq_in;      /**< Link Quality In (range [0, 3], 0 indicates no link) */
    uint8_t lq_out;     /**< Link Quality Out (range [0, 3], 0 indicates no link) */
};

/** Represents information about a Thread device - init with @ref OSP_OTBR_DEVICE_INIT */
struct osp_otbr_device_s {
    rloc16_t rloc16;                    /**< RLOC16 of the device */
    enum osp_otbr_device_role_e role;   /**< Current Thread device role */
    struct {
        osn_ip6_addr_t *addr;           /**< Array of IPv6 addresses (`NULL` if `count` is 0) */
        size_t count;                   /**< Number of IPv6 addresses in `addr` array */
    } ip_addresses;                     /**< Discovered IPv6 addresses */
    uint64_t ext_addr;                  /**< Extended MAC Address (IEEE 802.15.4 Extended Address), 0 = Invalid */
    uint16_t version;                   /**< Thread Version, 0xFFFF = Unknown */
    char vendor_name[32];               /**< Vendor name */
    char vendor_model[32];              /**< Vendor model */
    char vendor_sw_version[16];         /**< Vendor SW Version */
    char thread_stack_version[64];      /**< Thread Stack Version */
    union {
        /**
         * Child-specific device information
         *
         * @note Only valid when `role` is `OSP_OTBR_DEVICE_ROLE_CHILD`.
         */
        struct {
            /**
             * Child device mode
             *
             * These flags are all true for routers or router eligible devices.
             */
            struct {
                /** Whether the child has its receiver on when not transmitting (true for all except SEDs) */
                bool rx_on_when_idle;
                /** Whether the child is Full Thread Device (REED, FED), not only Minimal Thread Device (MED, SED) */
                bool full_thread_device;
                /** Whether device gets full Network Data, not only a stable sub-set */
                bool full_network_data;
            } mode;
            /** Link to the parent (with incoming link quality to child from parent) */
            struct osp_otbr_link_s parent;
        } child;
        /**
         * Router-specific device information
         *
         * @note Only valid when `role` is >= `OSP_OTBR_DEVICE_ROLE_ROUTER` (router or leader).
         */
        struct {
            /** Whether this router acts as a border router providing external connectivity */
            bool is_border_router;
            /** Router/Child devices connected with/to this router (direct radio link) */
            struct {
                struct osp_otbr_link_s *links;  /**< Array of links (`NULL` if `count` is 0) */
                size_t count;                   /**< Number of links in `links` array */
            } neighbors;
        } router;
    };
    /** Network Diagnostic Mac Counters values (RFC 2863) */
    struct {
        /** Inbound packet counters */
        struct {
            /** Discarded received packets because of unknown/unsupported protocol (IfInUnknownProtos) */
            uint32_t unknown_protos;
            /** Inbound packets with errors preventing delivery to higher-layer (IfInErrors) */
            uint32_t errors;
            /** Delivered ucast packets to higher-layer, not addressed to multicast/broadcast (IfInUcastPkts) */
            uint32_t ucast;
            /** Delivered packets to higher-layer, addressed to a broadcast (IfInBroadcastPkts) */
            uint32_t broadcast;
            /** Inbound packets discarded with no error detected (IfInDiscards) */
            uint32_t discards;
        } in;
        /** Outbound packet counters */
        struct {
            /** Outbound packets unable to transmit due to errors (IfOutErrors) */
            uint32_t errors;
            /** Total requested ucast packets for transmission (IfOutUcastPkts) */
            uint32_t ucast;
            /** Total requested broadcast packets for transmission (IfOutBroadcastPkts) */
            uint32_t broadcast;
            /** Outbound packets discarded with no error detected (IfOutDiscards) */
            uint32_t discards;
        } out;
    } diag_counters;
};

#define OSP_OTBR_DEVICE_INIT (struct osp_otbr_device_s){ .rloc16 = 0xFFFE, .version = 0xFFFF }


/** Represents a list of @ref osp_otbr_device_s */
struct osp_otbr_devices_s {
    struct osp_otbr_device_s *devices;  /**< Array of devices (`NULL` if `count` is 0) */
    size_t count;                       /**< Number of devices in `devices` array */
};

/** Represents a Thread network scan or discovery result */
struct osp_otbr_scan_result_s
{
    uint64_t ext_addr;   /**< IEEE 802.15.4 Extended Address */
    uint64_t ext_pan_id; /**< Thread Extended PAN ID */
    uint16_t pan_id;     /**< IEEE 802.15.4 PAN ID */
    char name[16 + 1];   /**< Thread Network Name */
    struct
    {
        uint8_t length;       /**< Length of steering data (bytes) */
        uint8_t data[16];     /**< Steering data byte values */
    } steering_data;          /**< Steering Data */
    uint8_t channel;          /**< IEEE 802.15.4 Channel */
    int8_t rssi;              /**< RSSI (dBm) */
    uint8_t lqi;              /**< LQI */
    uint16_t joiner_udp_port; /**< Joiner UDP Port */
    uint8_t version;          /**< Version */
    bool native;              /**< Native Commissioner flag */
    bool discover;            /**< Result from MLE Discovery */
};

/** Represents a list of @ref osp_otbr_scan_result_s */
struct osp_otbr_scan_results_s
{
    struct osp_otbr_scan_result_s *networks; /**< Array of scan or discovery results (`NULL` if `count` is 0) */
    size_t count;                            /**< Number of results in `networks` array */
};

/**
 * Callback invoked when the Operational Dataset changes
 *
 * @param[in] dataset  A pointer to the Operational Dataset TLVs.
 */
typedef void osp_otbr_on_dataset_change_cb_t(struct osp_otbr_dataset_tlvs_s *dataset);

/**
 * Callback invoked every time the Thread network topology is updated
 *
 * @param[in] role      The Thread device role of this device.
 * @param[in] devices   List of Thread devices currently in the network.
 *                      Only available if attached to a Thread network (role is child, router or leader).
 *                      Data in this structure is valid only for the duration of the callback,
 *                      the caller frees the structure after the callback returns.
 */
typedef void osp_otbr_on_network_topology_cb_t(enum osp_otbr_device_role_e role, struct osp_otbr_devices_s *devices);

/**
 * Callback invoked when the Thread Network Scan or Discovery completes
 *
 * @param[in] networks  List of Thread networks discovered.
 */
typedef void osp_otbr_on_network_scan_result_cb_t(struct osp_otbr_scan_results_s *networks);

/**
 * Initialize the OpenThread Border Router (OTBR) subsystem
 *
 * @param[in] loop                       Event loop used to handle asynchronous events from the OTBR daemon
 *                                       and invoke the callbacks.
 * @param[in] on_dataset_change_cb       Optional callback invoked when the Active Operational Dataset changes.
 * @param[in] on_network_topology_cb     Optional callback invoked when the Thread network topology is updated.
 * @param[in] on_network_scan_result_cb  Optional callback invoked with the Thread network discovery results.
 *
 * @return true on success. Failure is logged internally.
 *
 * @note Call osp_otbr_close() after successful initialization to cleanup the subsystem resources.
 */
bool osp_otbr_init(
        struct ev_loop *loop,
        osp_otbr_on_dataset_change_cb_t *on_dataset_change_cb,
        osp_otbr_on_network_topology_cb_t *on_network_topology_cb,
        osp_otbr_on_network_scan_result_cb_t *on_network_scan_result_cb);

/**
 * Start the OTBR Daemon
 *
 * If the daemon is already running, it is stopped and restarted with the new parameters.
 *
 * This function does not start the Thread radio, use @ref osp_otbr_set_thread_radio() for this purpose.
 *
 * @param[in]  thread_iface   Thread network interface name.
 * @param[in]  network_iface  Backbone network interface name, or `NULL` to disable Border Routing feature.
 * @param[out] eui64          Location where to store the factory-assigned IEEE EUI-64.
 * @param[out] ext_addr       Location where to store the IEEE 802.15.4 Extended Address used by the Thread interface.
 *
 * @return true on success. Failure is logged internally.
 */
bool osp_otbr_start(const char *thread_iface, const char *network_iface, uint64_t *eui64, uint64_t *ext_addr);

/**
 * Set the interval between periodic scans of the Thread network(s) and topology reports
 *
 * @param[in] topology  Interval between topology reports (seconds),
 *                      0 to disable periodic analysis,
 *                      -1 to keep the current value.
 * @param[in] discovery Interval between scanning for Thread networks using MLE Discovery operations (seconds),
 *                      0 to disable periodic MLE Discovery,
 *                      -1 to keep the current value.
 *
 * @return true on success. Failure is logged internally.
 */
bool osp_otbr_set_report_interval(int topology, int discovery);

/**
 * Create a new Thread Network dataset using provided network parameters
 *
 * This function does not modify the current active or pending dataset, nor does it
 * manage the Thread radio. It is intended to be used to create a new dataset, which
 * can be later applied using @ref osp_otbr_set_dataset().
 *
 * @param[in]  params   A pointer to the network parameters.
 * @param[out] dataset  A pointer to where the generated dataset TLVs will be copied.
 *
 * @return true on success. Failure is logged internally.
 */
bool osp_otbr_create_network(const struct otbr_osp_network_params_s *params, struct osp_otbr_dataset_tlvs_s *dataset);

/**
 * Set the Pending or Active Operational Dataset
 *
 * If the Active Dataset does not include an Active Timestamp, the dataset is only partially complete.
 *
 * If Thread is enabled on a device that has a partially complete Active Dataset, the device will attempt
 * to attach to an existing Thread network using any existing information in the dataset. Only the Thread
 * Network Key is needed to attach to a network.
 *
 * If channel is not included in the Active Dataset, the device will send MLE Announce messages across
 * different channels to find neighbors on other channels.
 *
 * This function does not start the Thread radio (use @ref osp_otbr_set_thread_radio() for this purpose),
 * but it might restart it if it is already running, in order to apply the new dataset.
 *
 * This function results in invocation of @ref osp_otbr_on_dataset_change_cb_t callback,
 * if the new dataset is successfully applied and differs from the existing one.
 *
 * @param[in]  dataset  A pointer to the Operational Dataset TLVs.
 * @param[in]  active   Set to `true` to set the Active instead of Pending Dataset.
 *
 * @return true on success. Failure is logged internally.
 */
bool osp_otbr_set_dataset(const struct osp_otbr_dataset_tlvs_s *dataset, bool active);

/**
 * Get the Pending or Active Operational Dataset
 *
 * @param[out] dataset  A pointer to where the Operational Dataset TLVs will be copied.
 * @param[in]  active   Set to `true` to get the Active instead of Pending Dataset.
 *
 * @return true on success (`dataset->len` will be set to 0 if the desired dataset is missing).
 *         Failure is logged internally.
 */
bool osp_otbr_get_dataset(struct osp_otbr_dataset_tlvs_s *dataset, bool active);

/**
 * Enable or disable the Thread protocol operation
 *
 * If enabled and a valid dataset exists, the device will attempt to attach
 * to a Thread network or create a new one (based on the dataset).
 * If disabled, the device will detach from the current Thread network (if any).
 *
 * @param[in] enable  `true` to enable Thread radio and protocol operation, `false` to disable.
 *
 * @return true on success. Failure is logged internally.
 */
bool osp_otbr_set_thread_radio(bool enable);

/**
 * Stop the OTBR Daemon
 *
 * Nothing is done if the daemon has not been started yet (`true` is returned).
 * Does not explicitly stop the Thread radio (use @ref osp_otbr_set_thread_radio()
 * for this purpose), but it will be stopped as a side effect of stopping the daemon.
 *
 * @return true on success. Failure is logged internally.
 */
bool osp_otbr_stop(void);

/**
 * Stop the OTBR Daemon and cleanup the subsystem resources
 *
 * Nothing is done if the subsystem has not been initialized yet.
 */
void osp_otbr_close(void);


/**
 * Get a device ID from a given RLOC16 ([0, 62] if the RLOC16 is of a Router, [1, 511] if Child)
 *
 * @param[in] rloc16  RLOC16 address of the device.
 *
 * @return device ID or 0xFFFE if the `rloc16` is invalid.
 */
static inline uint16_t osp_otbr_device_id(const rloc16_t rloc16) {
    const uint16_t child_id = rloc16 & 0x1FF;  /*< Lower 9 bits of RLOC16 */
    const uint16_t router_id = rloc16 >> 10;   /*< Upper 6 bits of RLOC16 */

    /* Bit 9 is reserved, OT_NETWORK_MAX_ROUTER_ID (Maximum Router ID) is 62 */
    if ((rloc16 & (1u << 9)) || (router_id > 62))
    {
        return 0xFFFE;
    }
    /* Because a Router is not a Child, the Child ID for a Router is always 0 */
    return (child_id > 0) ? child_id : router_id;
}

/**
 * Get an RLOC16 from a given combination of Router ID and Child ID
 *
 * @param[in] router_id  Router ID in range [0, 62].
 * @param[in] child_id   Child ID in range [1, 511] or 0 for a Router.
 *
 * @return RLOC16 address of the device or 0xFFFE if provided IDs are invalid.
 */
static inline rloc16_t osp_otbr_rloc16(const uint8_t router_id, const uint16_t child_id) {
    return ((router_id <= 62) && (child_id <= 0x1FF)) ? ((uint16_t) (router_id << 10) | child_id) : 0xFFFE;
}


/// @} OSP_OTBR
/// @} OSP

#endif /* OSP_OTBR_H_INCLUDED */
