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

#ifndef TARGET_COMMON_H_INCLUDED
#define TARGET_COMMON_H_INCLUDED

#include "dppline.h"
#include "ds_dlist.h"

#include "schema.h"

/**
 * @file target_common.h
 * @brief Additional target API header
 *
 * The declarations in this header depend on the platform specific declaration
 * from header TARGET_H, which is why it is separated from @ref target.h
 */

/// @addtogroup LIB_TARGET
/// @{

/// @defgroup LIB_TARGET_RADIO Radio API
/// Definitions and API related to control of radios.
/// @{

/******************************************************************************
 *  RADIO definitions
 *****************************************************************************/

/**
 * @brief Used to report chirping in target_radio_ops.op_dpp_announcement()
 */
struct target_dpp_chirp_obj {
    const char *ifname;
    const char *mac_addr;
    const char *sha256_hex;
};

/**
 * @brief Used to report configuration completion in
 * target_radio_ops.op_dpp_conf_enrollee()
 */
struct target_dpp_conf_enrollee {
    const char *ifname;
    const char *sta_mac_addr;
    const char *sta_netaccesskey_sha256_hex;  /**< public key hash */
    const char *config_uuid;
};

/**
 * @brief Possible AKMs that can be reported by
 * target_radio_ops.op_dpp_conf_network() in struct target_dpp_conf_network
 */
enum target_dpp_conf_akm {
    TARGET_DPP_CONF_UNKNOWN,
    TARGET_DPP_CONF_PSK,
    TARGET_DPP_CONF_SAE,
    TARGET_DPP_CONF_PSK_SAE,
    TARGET_DPP_CONF_DPP,
    TARGET_DPP_CONF_DPP_SAE,
    TARGET_DPP_CONF_DPP_PSK_SAE,
};

/**
 * @brief Used to report configuration completion in
 * target_radio_ops.op_dpp_conf_network()
 *
 * Depending on the target_dpp_conf_network.akm value, other fields
 * are expected to be set accordingly.
 *
 * Whenever a PSK or SAE AKM is listed, then the following fields
 * must be set:
 * - target_dpp_conf_network.ssid_hex
 * - target_dpp_conf_network.psk_hex or target_dpp_conf_network.pmk_hex
 *
 * Whenever a DPP is listed, then the following fields must be set:
 * - target_dpp_conf_network.dpp_netaccesskey_hex
 * - target_dpp_conf_network.dpp_connector
 * - target_dpp_conf_network.dpp_csign_hex
 *
 * In some cases all fields must be set.
 */
struct target_dpp_conf_network {
    const char *ifname;
    enum target_dpp_conf_akm akm;
    const char *ssid_hex;
    const char *psk_hex;
    const char *pmk_hex;
    const char *dpp_netaccesskey_hex;  /**< private key part */
    const char *dpp_connector;
    const char *dpp_csign_hex;
    const char *config_uuid;
};

/**
 * @brief Used to identify what key material is provided in target_dpp_key
 *
 * These are all EC keys. You can refer to hostapd project
 * to get a better idea.
 */
enum target_dpp_key_type {
    TARGET_DPP_KEY_PRIME256V1,
    TARGET_DPP_KEY_SECP384R1,
    TARGET_DPP_KEY_SECP512R1,
    TARGET_DPP_KEY_BRAINPOOLP256R1,
    TARGET_DPP_KEY_BRAINPOOLP384R1,
    TARGET_DPP_KEY_BRAINPOOLP512R1,
};

#define TARGET_DPP_KEY_LEN 512

/**
 * @brief Used for extender onboarding, see target_dpp_key_get()
 */
struct target_dpp_key {
    enum target_dpp_key_type type;
    char hex[TARGET_DPP_KEY_LEN + 1];
};

/**
 * @brief List of callbacks for radio/vif changes
 */
struct target_radio_ops {
    /** target calls this whenever middleware (if exists) wants to
     *  update vif configuration */
    void (*op_vconf)(const struct schema_Wifi_VIF_Config *vconf,
                     const char *phy);

    /** target calls this whenever middleware (if exists) wants to
     *  update radio configuration */
    void (*op_rconf)(const struct schema_Wifi_Radio_Config *rconf);

    /** target calls this whenever system vif state has changed,
     *  e.g. channel changed, target_vif_config_set2() was called */
    void (*op_vstate)(const struct schema_Wifi_VIF_State *vstate,
                      const char *phy);

    /** target calls this whenever system radio state has changed,
     *  e.g. channel changed, target_radio_config_set2() was called */
    void (*op_rstate)(const struct schema_Wifi_Radio_State *rstate);

    /** target calls this whenever a client connects or disconnects */
    void (*op_client)(const struct schema_Wifi_Associated_Clients *client,
                      const char *vif,
                      bool associated);

    /** target calls this whenever it wants to re-sync all clients due
     *  to, e.g. internal event buffer overrun. */
    void (*op_clients)(const struct schema_Wifi_Associated_Clients *clients,
                       int num,
                       const char *vif);

    /** target calls this whenever it wants to clear out
     *  all clients on a given vif; intended to use when target wants to
     *  fully re-sync connects clients (i.e. the call will be followed
     *  by op_client() calls) or when a vif is deconfigured abruptly */
    void (*op_flush_clients)(const char *vif);

    /** target calls this whenever it wants to resolve radius servers
     *  returned by 'mib' hostap command to UUIDs to be put into
     *  Wifi_VIF_State table */
    void (*op_radius_state)(const struct schema_RADIUS *radiuses,
                            int num,
                            const char *vif);

    /** target calls this whenever it wants to notify WM about
     *  configured neighboring APs. This is used to keep track
     *  of the 'state' to be compared with 'config' for stateless
     *  Neighbors table */
    void (*op_nbors_state)(const struct schema_Wifi_VIF_Neighbors *neighbors,
                           int num,
                           const char *vif);

    /** target shall call this whenever chirping packets are received */
    void (*op_dpp_announcement)(const struct target_dpp_chirp_obj *c);

    /** target shall call this whenever DPP Enrollee is given out a
     *  DPP Configuration. This marks completion of a prior
     *  target_dpp_config_set() call.
     *  This shall not be called from within target_dpp_config_set() itself.
     */
    void (*op_dpp_conf_enrollee)(const struct target_dpp_conf_enrollee *c);

    /** target shall call this whenever DPP Configurator gives us out
     *  a configuration. This marks completion of a prior
     *  target_dpp_config_set() call.
     *  This shall not be called from within target_dpp_config_set() itself.
     */
    void (*op_dpp_conf_network)(const struct target_dpp_conf_network *c);

    /** target shall call this whenever DPP Configurator failed at any
     *  stage (internal timeout, rejection, empty conf object, etc).
     *  This marks completion of a prior target_dpp_config_set() call.
     *  This shall not be called from within target_dpp_config_set() itself.
     */
    void (*op_dpp_conf_failed)(void);

    /** target shall call this whenever a STA interface receives a CSA
     *  intention from the parent AP. The intention can be either
     *  Action Frame with CSA, or Beacon with CSA IE countdown. It is
     *  safe to call this multiple number of times as Beacon CSA IE
     *  countdown goes down. Target must check if this function is
     *  NULL. If it's NULL then it must not be called. To denote 80+80
     *  MHz operation use chan_width_mhz=8080.
     *  When this is not NULL the target implementation shall no
     *  longer perform any implicit CSA between PHYs. It may, but does
     *  not need to, still perform inheritance of CSA from STA to AP
     *  interfaces.
     */
    void (*op_csa_rx)(const char *phy_name,
                      const char *vif_name,
                      int chan_pri_freq_mhz,
                      int chan_width_mhz);
};

/**
 * @brief Hands over WM callbacks so target can notify about vif/radio statuses
 *
 * Target implementation is expected to notify WM about things like channel
 * changes, configuration being applied, clients connecting and disconnecting,
 * etc. via provided callbacks.
 *
 * Target implementation is free to perform early bookkeeping initialization,
 * e.g. open up sockets to middleware HAL API it talks to, etc.
 *
 * @return true if target is okay. False if it could not initialize. False
 * results in WM using old target API currently. In the future WM will refuse
 * to start if False is returned.
 */
bool target_radio_init(const struct target_radio_ops *ops);

/**
 * @brief Initialize radio interface configuration
 *
 * This is called during WM initialization only if
 * target_radio_config_need_reset() is true.
 *
 * This is expected to call op_rconf and op_vconf with initial radio/vif
 * configuration parameters.
 *
 * This is intended to handle residential gateways / systems with middleware
 * HAL that can take control over ovsdb.
 *
 * @return true on success.
 */
bool target_radio_config_init2(void);

/**
 * @brief Target tells if it requires full re-sync with Config/State
 *
 * If target implementation talks with a middleware HAL that can sometimes take
 * control over Plume cloud then this function should return true whenever
 * middleware is supposed to be in charge of the wireless configuration.
 *
 * When true target is expected to call op_vconf and op_rconf during
 * target_radio_config_init2().
 *
 * @return true if middleware exists and target wants
 * target_radio_config_init2() to be called.
 */
bool target_radio_config_need_reset(void);

/**
 * @brief Apply the configuration for the radio interface
 *
 * This is API v2. Will be called only if target_radio_init() returned
 * true during init.
 *
 * @param rconf complete desired radio config
 * @param changed list of fields from rconf that are out of sync with
 * regard to rstate
 * @return true on success, false means the call will be retried later
 */
bool target_radio_config_set2(const struct schema_Wifi_Radio_Config *rconf,
                              const struct schema_Wifi_Radio_Config_flags *changed);

/**
 * @brief Get state of radio interface
 *
 * This function is used to retrieve the current state of a radio interface
 *
 * @note
 * Depending on the implementation, some of the returned values in rstate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param rstate output; radio interface state
 * @return true on success
 */
bool target_radio_state_get(char *ifname, struct schema_Wifi_Radio_State *rstate);

/// @} LIB_TARGET_RADIO

/// @defgroup LIB_TARGET_VIF VIF API
/// Definitions and API related to control of VIFs.
/// @{

/******************************************************************************
 *  VIF definitions
 *****************************************************************************/

/**
 * @brief Apply the configuration for the vif interface
 *
 * If vconf.wpa_key_mgmt contains "dpp" then the interface shall capture DPP
 * Announcements (chirping) and report it through
 * target_radio_ops.op_dpp_announcement().
 *
 * @param vconf complete desired vif config
 * @param rconf complete desired radio config
 * @param cconfs complete desired vif credential config, used for
 * extender mode to provide multiple network for sta vif
 * @param changed list of fields from vconf that are out of sync with
 * state
 * @param num_cconfs number of cconfs entries
 * @return true on success, false means the call will be retried later
 */
bool target_vif_config_set2(const struct schema_Wifi_VIF_Config *vconf,
                            const struct schema_Wifi_Radio_Config *rconf,
                            const struct schema_Wifi_Credential_Config *cconfs,
                            const struct schema_Wifi_VIF_Config_flags *changed,
                            int num_cconfs);

/**
 * @brief Apply the configuration for the vif interface
 *
 * Function enhanced with RADIUS table handling. This allows
 * hapd.c to parse VIF to RADIUS table bindings.
 *
 * @param vconf complete desired vif config
 * @param rconf complete desired radio config
 * @param cconfs complete desired vif credential config, used for
 * extender mode to provide multiple network for sta vif
 * @param changed list of fields from vconf that are out of sync with
 * state
 * @param nbors_list neighboring APs for FT rxkh definitions
 * @param radius_list containing all matching RADIUS table entries
 * @param num_cconfs number of cconfs entries
 * @param num_nbors_list number of entries in nbors_list
 * @param num_radius_list number of entries in radius_list
 * @return true on success, false means the call will be retried later
 */
bool target_vif_config_set3(const struct schema_Wifi_VIF_Config *vconf,
                            const struct schema_Wifi_Radio_Config *rconf,
                            const struct schema_Wifi_Credential_Config *cconfs,
                            const struct schema_Wifi_VIF_Config_flags *changed,
                            const struct schema_Wifi_VIF_Neighbors *nbors_list,
                            const struct schema_RADIUS *radius_list,
                            int num_cconfs,
                            int num_nbors_list,
                            int num_radius_list);

/**
 * @brief Interrogate target if v3 version vif_config_set
 * is supported. This means that RADIUS table can be passed
 * to hapd.c for evaluation.
 *
 * @return true if target_vif_config_set3 is implemented.
 */
bool target_vif_config_set3_supported(void);

/**
 * @brief Remove station from driver
 *
 * Get rid of station reference from driver by trying issuing a deauth.
 * Note that this does not mean that deauth frame will be sent,
 * that is the case only when station is still associated.
 *
 * @param ifname interface name
 * @param mac_addr station mac address
 * @return true on success
 */
bool target_vif_sta_remove(const char *ifname, const uint8_t *mac_addr);

/**
 * @brief Get state of vif interface
 *
 * This function is used to retrieve the current state of a vif interface
 *
 * @note
 * Depending on the implementation, some of the returned values in vstate may
 * be a copy  of last applied configuration and not a reflection of the actual
 * interface state
 *
 * @param ifname interface name
 * @param vstate output; vif interface state
 * @return true on success
 */
bool target_vif_state_get(char *ifname, struct schema_Wifi_VIF_State *vstate);

/// @} LIB_TARGET_VIF

/// @defgroup LIB_TARGET_DPP DPP API
/// Definitions and API related to Device Provisioning Protocol.
/// @{

/******************************************************************************
 *  DPP definitions
 *****************************************************************************/

/**
 * @brief Interrogate target if DPP is supported
 *
 * @return true if DPP supported
 */
bool target_dpp_supported(void);

/**
 * @brief Start or stop DPP related actions
 *
 * The @p config is always non-NULL. It points to a list of pointers and
 * uses a NULL pointer guard to denote list end. One can iterate over it
 * like so:
 *
 *   for (; *config; config++) { ... }
 *
 * If @p config[0] is NULL, it means there are no DPP_Config jobs for the
 * target to execute. This is equivalent to target_dpp_config_set(NULL).
 *
 * When @p config[0] is NULL:
 *  - any ongoing chirping, listening or authentication must be stopped
 *  - if any sta interfaces are present, they must resume roaming
 *  - any configurators, bootstraps shall be flushed
 *  - DPP announcements shall still be reported via
 *    target_radio_ops.op_dpp_announcement() as per
 *    target_vif_config_set2() configuration
 *
 * Otherwise:
 *  - if any other DPP action was already programmed in the target, it must
 *    be stopped and flushed
 *  - depending on @p config[].auth value, the target shall start chirping,
 *    listening, initiate auth, or wait for chirping
 *
 * Upon completion of a job, one of the target_radio_ops must be called:
 *  - target_radio_ops.op_dpp_conf_enrollee(): when acting as Configurator
 *  - target_radio_ops.op_dpp_conf_network(): when acting as Enrollee
 *  - target_radio_ops.op_dpp_conf_failed(): either Enrollee or Configurator
 *
 * The following fields need to be respected by the target implementation:
 *  - configurator_key_hex
 *  - configurator_key_curve
 *  - configurator_conf_role
 *  - configurator_conf_ssid_hex
 *  - configurator_conf_psk_hex
 *  - peer_bi_uri
 *  - own_bi_key_hex
 *  - own_bi_key_curve
 *  - timeout_seconds
 *  - auth
 *  - ifnames
 *  - status
 *
 * The following fields need to be ignored by the target implementation. These
 * fields are used to expose given DPP_Config's results back to the cloud and
 * are managed by opensync core. These are essentially provided back explicitly
 * via target_radio_ops.op_dpp_conf_enrollee() or
 * target_radio_ops.op_dpp_conf_network():
 *  - sta_mac_addr
 *  - sta_netaccesskey_hex
 *  - akm
 *  - ssid_hex
 *  - wpa_pass_hex
 *  - wpa_pmk_hex
 *  - dpp_netaccesskey_hex
 *  - dpp_connector
 *  - dpp_csign_hex
 *  - config_uuid
 *  - renew
 *
 * @param config DPP action specification, or NULL
 * @return true on success
 */
bool target_dpp_config_set(const struct schema_DPP_Config **config);

/**
 * @brief Fetch a key to be used for DPP Onboarding
 *
 * Opensync extenders may attempt to perform onboarding
 * through DPP. For this purpose they must be provisioned
 * with a compatible EC key. That key needs to be exposed in
 * this call.
 *
 * Logistically the public representation of the EC key must
 * be known to the controller inventory so that parent APs
 * can recognize it and allow it in.
 *
 * It is intended to be used through DPP 1.2 Announcements.
 */
bool target_dpp_key_get(struct target_dpp_key *key);

/// @} LIB_TARGET_DPP

/// @defgroup LIB_TARGET_STATS Statistics Related APIs
/// Definitions and API related to statistics.
/// @{

/******************************************************************************
 *  STATS definitions
 *****************************************************************************/

/**
 * @brief Enable radio tx stats
 * @param radio_cfg radio interface handle
 * @param status true (enable) or false (disable)
 * @return true on success
 */
bool target_radio_tx_stats_enable(
        radio_entry_t              *radio_cfg,
        bool                        status);

/**
 * @brief Enable radio fast scan
 * @param radio_cfg radio interface handle
 * @param if_name radio interface name
 * @return true on success
 */
bool target_radio_fast_scan_enable(
        radio_entry_t              *radio_cfg,
        ifname_t                    if_name);

/******************************************************************************
 *  CLIENT definitions
 *****************************************************************************/
target_client_record_t *target_client_record_alloc();
void target_client_record_free(target_client_record_t *record);

typedef bool target_stats_clients_cb_t (
        ds_dlist_t                 *client_list,
        void                       *ctx,
        int                         status);

/**
 * @brief Get clients stats
 *
 * The results will be provided to the callback function and can be called
 * either synchronously or asynchronously depending on platform specifics
 *
 * @param radio_cfg radio interface handle
 * @param essid SSID string
 * @param client_cb callback function
 * @param client_list output; resulting client list
 * @param client_ctx optional context for callback
 * @return true on success
 */
bool target_stats_clients_get (
        radio_entry_t              *radio_cfg,
        radio_essid_t              *essid,
        target_stats_clients_cb_t  *client_cb,
        ds_dlist_t                 *client_list,
        void                       *client_ctx);

/**
 * @brief Calculate client stats deltas
 *
 * Calculates the deltas between new and old client list and stores the result
 * into client_record
 *
 * @param radio_cfg radio interface handle
 * @param client_list_new new values
 * @param client_list_old old values
 * @param client_record output; calculated deltas
 * @return true on success
 */
bool target_stats_clients_convert (
        radio_entry_t              *radio_cfg,
        target_client_record_t     *client_list_new,
        target_client_record_t     *client_list_old,
        dpp_client_record_t        *client_record);

/// @} LIB_TARGET_STATS

/// @defgroup LIB_TARGET_SURVEY Survey API
/// Definitions and API related to surveys.
/// @{

/******************************************************************************
 *  SURVEY definitions
 *****************************************************************************/
target_survey_record_t *target_survey_record_alloc();
void target_survey_record_free(target_survey_record_t *record);

typedef bool target_stats_survey_cb_t (
        ds_dlist_t                 *survey_list,
        void                       *survey_ctx,
        int                         status);

/**
 * @brief Get radio channel survey stats
 *
 * The results will be provided to the callback function and can be called
 * either synchronously or asynchronously depending on platform specifics
 *
 * @param radio_cfg radio interface handle
 * @param chan_list list of channels
 * @param chan_num  number of channels in list
 * @param scan_type scan type
 * @param survey_cb callback function
 * @param survey_list output; survey stats
 * @param survey_ctx optional context for callback
 * @return true on success
 */
bool target_stats_survey_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        target_stats_survey_cb_t   *survey_cb,
        ds_dlist_t                 *survey_list,
        void                       *survey_ctx);

/**
 * @brief Calculate channel survey deltas
 *
 * Calculates the deltas between new and old channel survey and stores the result
 * into survey_record
 *
 * @param radio_cfg radio interface handle
 * @param scan_type scan type
 * @param data_new  new values
 * @param data_old  old values
 * @param survey_record output; calculated deltas
 * @return true on success
 */
bool target_stats_survey_convert (
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type,
        target_survey_record_t     *data_new,
        target_survey_record_t     *data_old,
        dpp_survey_record_t        *survey_record);

/// @} LIB_TARGET_SURVEY

/// @defgroup LIB_TARGET_NEIGHBOR Neighbor Scanning Related API
/// Definitions and API related to neighbor scanning.
/// @{

/******************************************************************************
 *  NEIGHBOR definitions
 *****************************************************************************/
typedef bool target_scan_cb_t(
        void                       *scan_ctx,
        int                         status);

/**
 * @brief Start neighbor scan
 *
 * The scanning will be performed in background and the callback function will
 * be called when the results are available. The actual results need to be
 * fetched with target_stats_scan_get()
 *
 * @param radio_cfg  radio interface handle
 * @param chan_list  channel list
 * @param chan_num   number of channels
 * @param scan_type  scan type
 * @param dwell_time dwell time in ms
 * @param scan_cb    callback function
 * @param scan_ctx   optional context for callback
 * @return true on success
 */
bool target_stats_scan_start (
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        int32_t                     dwell_time,
        target_scan_cb_t           *scan_cb,
        void                       *scan_ctx);

/**
 * @brief Stop neighbor scan
 *
 * @param radio_cfg  radio interface handle
 * @param scan_type  scan type
 * @return true on success
 */
bool target_stats_scan_stop (
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type);

/**
 * @brief Get neighbor stats
 *
 * @param radio_cfg  radio interface handle
 * @param chan_list  channel list
 * @param chan_num   number of channels
 * @param scan_type  scan type
 * @param scan_results output; neighbor stats
 * @return true on success
 */
bool target_stats_scan_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        dpp_neighbor_report_data_t *scan_results);

/// @} LIB_TARGET_NEIGHBOR

/// @defgroup LIB_TARGET_DEVICE_STATS Device Info API
/// Definitions and API related to device information.
/// @{

/******************************************************************************
 *  DEVICE definitions
 *****************************************************************************/

/**
 * @brief Get device stats
 *
 * Returns device load average (loadavg), uptime and file handles counts
 *
 * @param device_entry output; device stats
 * @return true on success
 */
bool target_stats_device_get(
        dpp_device_record_t        *device_entry);

/**
 * @brief Get device temperature
 *
 * @param radio_cfg radio interface handle
 * @param device_entry output; device stats
 * @return true on success
 */
bool target_stats_device_temp_get(
        radio_entry_t              *radio_cfg,
        dpp_device_temp_t          *device_entry);

/**
 * @brief Get device fan RPM
 *
 * @param fan_rpm RPM of the internal fan
 * @return true on success
 */
bool target_stats_device_fanrpm_get(uint32_t *fan_rpm);

/**
 * @brief Get the device fan duty cycle
 *
 * @param fan_duty_cycle Currently set fan duty cycle in per mille (‰),
 *                       0 meaning the fan is turned off and
 *                       1000 meaning the fan is at full power.
 * @return true on success
 */
bool target_stats_device_fandutycycle_get(uint16_t *fan_duty_cycle);

/// @} LIB_TARGET_DEVICE_STATS

/// @cond INTERNAL
/// @defgroup LIB_TARGET_CAPACITY Capacity Stats API (obsolete)
/// Obsolete API
/// @{

/******************************************************************************
 *  CAPACITY definitions
 *****************************************************************************/

/**
 * @brief obsolete: capacity stats
 * @return true on success
 */
bool target_stats_capacity_enable(
        radio_entry_t              *radio_cfg,
        bool                        enabled);

/**
 * @brief obsolete: capacity stats
 * @return true on success
 */
bool target_stats_capacity_get (
        radio_entry_t              *radio_cfg,
        target_capacity_data_t     *capacity_new);

/**
 * @brief obsolete: capacity stats
 * @return true on success
 */
bool target_stats_capacity_convert(
        target_capacity_data_t     *capacity_new,
        target_capacity_data_t     *capacity_old,
        dpp_capacity_record_t      *capacity_entry);

/// @} LIB_TARGET_CAPACITY
/// @endcond INTERNAL

/// @defgroup LIB_TARGET_DEVICE Device Control API
/// Definitions and API related to device control.
/// @{

/******************************************************************************
 *  DEVICE definitions
 *****************************************************************************/

/**
 * @brief Subscribe to changes of device config
 *
 * This is for changes of device config that originate from external management
 * protocols not ovsdb. The changes will then be applied to ovsdb by the callback.
 * The device config is a data described inside AWLAN_Node table. The example
 * implementation may want to set custom cloud redirector address here and call
 * the awlan_cb() whenever the redirector address is updated.
 * If the redirector address is static and the target is not going to
 * update any other field of AWLAN_Node table it is safe to make this function
 * a no-op.
 *
 * callback type: void (*update)(struct schema_AWLAN_Node *awlan,
 *   schema_filter_t *filter);
 *
 * @param awlan_cb callback function
 * @return true on success
 */
bool target_device_config_register(void *awlan_cb);

/**
 * @brief Apply device config
 *
 * This applies device config from ovsdb to external management protocols (if available).
 * The device config is a data described inside AWLAN_Node table. Example field of that
 * table that may need to be synchronized with target-specific implementation is
 * a 'device_mode'.
 * If target doesn't need to perform any action when the content of this table is updated
 * then it is safe to make this function a no-op.
 *
 * @param awlan ovsdb schema for AWLAN_node table.
 * @return true on success
 */
bool target_device_config_set(struct schema_AWLAN_Node *awlan);

/**
 * @brief Execute external tools
 *
 * The implementation of this function should provide ability to run
 * a shell command.
 *
 * @param cmd command string
 * @return true on success
 */
bool target_device_execute(const char* cmd);

/** States returned by @ref target_device_connectivity_check() */
typedef struct {
    bool link_state;          //!< @brief  If link has an IP, the link_state should
                              //!< be set to 'true' if it can be pinged.
                              //!< Otherwise a custom (vendor-specific) way of
                              //!< checking link state must be provided.
    bool router_ipv4_state;   //!< true if the ipv4 of default gateway can be pinged.
    bool router_ipv6_state;   //!< true if the ipv6 of default gateway can be pinged.
    bool internet_ipv4_state; //!< True if external IP address can be pinged.
    bool internet_ipv6_state; //!< True if the IP of default gateway can be pinged.
    bool internet_state;      //!< True if external IP address can be pinged.
    bool ntp_state;           //!< True if current datetime is set correctly.
} target_connectivity_check_t;

/** Option flags for @ref target_device_connectivity_check() */
typedef enum {
    LINK_CHECK     = 1 << 0,
    ROUTER_CHECK   = 1 << 1,
    INTERNET_CHECK = 1 << 2,
    NTP_CHECK      = 1 << 3,
    IPV4_CHECK     = 1 << 4,
    IPV6_CHECK     = 1 << 5,
    FAST_CHECK     = 1 << 6,
} target_connectivity_check_option_t;

/**
 * @brief Get device connectivity status
 *
 * For example implementation, see target_kconfig.c
 *
 * @param ifname  interface name
 * @param cstate  connectivity state
 * @param opts    which checks to perform
 * @return true if all links are in correct state, false otherwise.
 */
bool target_device_connectivity_check(const char *ifname,
                                      target_connectivity_check_t *cstate,
                                      target_connectivity_check_option_t opts);

/**
 *  @brief Restart plume managers
 *  @return true on success
 */
bool target_device_restart_managers_helper(const char *calling_func);
#define target_device_restart_managers(void) target_device_restart_managers_helper(__func__)

/**
 *  @brief Ping watchdog system
 *
 *  If the target provides a watchdog that checks if OpenSync managers
 *  are alive, the implementation of this function should feed that
 *  watchdog.
 *  If target doesn't use such functionality it's safe to just return
 *  true.
 *
 *  @return true on success
 */
bool target_device_wdt_ping();

/// @} LIB_TARGET_DEVICE

/// @defgroup LIB_TARGET_MAC_LEARNING MAC Learning API
/// Definitions and API related to MAC learning.
/// @{

/******************************************************************************
 *  MAC LEARNING definitions
 *****************************************************************************/

/** @brief Ethernet client change callback type */
typedef bool target_mac_learning_cb_t(
            struct schema_OVS_MAC_Learning *omac,
            bool oper_status);

/**
 * @brief Subscribe to ethernet client change events.
 *
 * @param omac_cb a callback function
 * @return true on success
 */
bool target_mac_learning_register(target_mac_learning_cb_t *omac_cb);

/// @} LIB_TARGET_MAC_LEARNING

/******************************************************************************
 *  PLATFORM SPECIFIC definitions
 *****************************************************************************/
/// @defgroup LIB_TARGET_CLIENT_FREEZE Client Freeze API
/// Definitions and API related to Client Freeze functionality.
/// @{

/******************************************************************************
 *  CLIENT NICKNAME definitions
 *****************************************************************************/
typedef bool target_client_nickname_cb_t (
         struct schema_Client_Nickname_Config *cncfg,
         bool                                 status);

bool target_client_nickname_register(target_client_nickname_cb_t *nick_cb);
bool target_client_nickname_set(struct schema_Client_Nickname_Config *cncfg);

/******************************************************************************
 *  CLIENT FREEZE definitions
 *****************************************************************************/
typedef bool target_client_freeze_cb_t (
         struct schema_Client_Freeze_Config *cfcfg,
         bool                                status);

bool target_client_freeze_register(target_client_freeze_cb_t *freze_cb);
bool target_client_freeze_set(struct schema_Client_Freeze_Config *cfcfg);

/// @} LIB_TARGET_CLIENT_FREEZE

/// @} LIB_TARGET

#endif /* TARGET_COMMON_H_INCLUDED */
