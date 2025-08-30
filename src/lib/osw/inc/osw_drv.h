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

#ifndef OSW_DRV_H_INCLUDED
#define OSW_DRV_H_INCLUDED

#include <osw_types.h>
#include <osw_tlv.h>
#include <osw_stats_enum.h>
#include <osw_module.h>
#include <module.h>

/**
 * DOC: MLO - Multi-Link Operation
 *
 * MLO is intended to be non-configurable per se.
 * It is expected to be a system property that
 * other components may want, or need, to be aware
 * of to adjust their runtime behavior.
 *
 * For example steering, or firewall rules may
 * need to be applied to different bridge ports
 * when MLO is set up.
 *
 * The reason for MLO being non-configurable (in
 * other words static) is the fact that initial
 * vendor support seems to be lacking in the
 * dynamic runtime reconfiguration of it. Moreover
 * it is expected that some radio combinations may
 * not be possible on various platforms. The
 * lowest common denominator is to consider it as
 * an integration property.
 *
 * If a platform wants to support MLO it needs to:
 *
 *  - have an MLO capable hardware and driver
 *    stack,
 *
 *  - make sure driver provisioning is done on
 *    boot in a way that pre-enables MLO by
 *    whatever means necessary for that platform.
 *
 * OSW is already expecting driver provisioning to
 * be done at boot time by the system integrator,
 * eg. it does not try create/destroy WLAN netdevs
 * at runtime, nor to play around with BSSID or
 * MAC Address allocations.
 *
 * Once MLO support matures and the entire
 * ecosystem around it becomes more flexible this
 * may be revisited and extended.
 */

struct osw_drv;
struct osw_drv_frame_tx_desc;

/**
 * MLO Affiliated netdev state
 *
 * Used inside affiliated netdev state structures
 * to denote their relation to possible (local)
 * MLDs.
 */
struct osw_drv_mld_state {
    /**
     * The MLD Address.
     *
     * Can be empty (00:00:00:00:00:00) to denote
     * the MLD state is invalid / does not exist.
     */
    struct osw_hwaddr addr;

    /**
     * Possible MLD bond netdev name. Depending on
     * driver implementation this can point to:
     *
     *   - a distinct, special netdev that handles
     *     tx/rx data path,
     *
     *   - one of the underlying affiliated netdevs,
     *
     *   - be empty ("") to denote that any and
     *     all of the underlying affiliated
     *     netdevs can be used for tx/rx data
     *     path.
     */
    struct osw_ifname if_name;
};

struct osw_drv_vif_config_ap {
    struct osw_ifname bridge_if_name;
    bool bridge_if_name_changed;

    struct osw_nas_id nas_identifier;
    bool nas_identifier_changed;

    int beacon_interval_tu;
    bool beacon_interval_tu_changed;

    struct osw_channel channel;
    bool channel_changed;
    bool csa_required;

    bool isolated;
    bool isolated_changed;

    bool ssid_hidden;
    bool ssid_hidden_changed;

    bool mcast2ucast;
    bool mcast2ucast_changed;

    struct osw_ap_mode mode;
    bool mode_changed;

    enum osw_acl_policy acl_policy;
    bool acl_policy_changed;

    struct osw_ssid ssid;
    bool ssid_changed;

    struct osw_hwaddr_list acl;
    struct osw_hwaddr_list acl_add;
    struct osw_hwaddr_list acl_del;
    bool acl_changed;

    struct osw_wpa wpa;
    bool wpa_changed;

    struct osw_ap_psk_list psk_list;
    bool psk_list_changed;

    struct osw_radius_list radius_list;
    bool radius_list_changed;
    struct osw_radius_list acct_list;
    bool acct_list_changed;

    struct osw_passpoint passpoint;
    bool passpoint_changed;

    struct osw_ft_encr_key ft_encr_key;
    bool ft_encr_key_changed;

    bool ft_over_ds;
    bool ft_over_ds_changed;

    bool ft_pmk_r1_push;
    bool ft_pmk_r1_push_changed;

    bool ft_psk_generate_local;
    bool ft_psk_generate_local_changed;

    int ft_pmk_r0_key_lifetime_sec;
    bool ft_pmk_r0_key_lifetime_sec_changed;

    int ft_pmk_r1_max_key_lifetime_sec;
    bool ft_pmk_r1_max_key_lifetime_sec_changed;

    int ft_mobility_domain;
    bool ft_mobility_domain_changed;

    struct osw_neigh_list neigh_list;
    struct osw_neigh_list neigh_add_list;
    struct osw_neigh_list neigh_mod_list;
    struct osw_neigh_list neigh_del_list;
    bool neigh_list_changed;

    struct osw_neigh_ft_list neigh_ft_list;
    bool neigh_ft_list_changed;

    struct osw_wps_cred_list wps_cred_list;
    bool wps_cred_list_changed;

    bool wps_pbc;
    bool wps_pbc_changed;

    struct osw_multi_ap multi_ap;
    bool multi_ap_changed;

    enum osw_mbss_vif_ap_mode mbss_mode;
    bool mbss_mode_changed;

    int mbss_group;
    bool mbss_group_changed;

    bool mbo;
    bool mbo_changed;

    bool oce;
    bool oce_changed;

    int oce_min_rssi_dbm;
    bool oce_min_rssi_dbm_changed;

    bool oce_min_rssi_enable;
    bool oce_min_rssi_enable_changed;

    int oce_retry_delay_sec;
    bool oce_retry_delay_sec_changed;

    int max_sta;
    bool max_sta_changed;
};

struct osw_drv_vif_sta_network {
    struct osw_ifname bridge_if_name;
    struct osw_hwaddr bssid;
    struct osw_ssid ssid;
    struct osw_psk psk;
    struct osw_wpa wpa;
    struct osw_drv_vif_sta_network *next;
    bool multi_ap;
    int priority;
};

enum osw_drv_vif_config_sta_operation {
    OSW_DRV_VIF_CONFIG_STA_NOP,
    OSW_DRV_VIF_CONFIG_STA_CONNECT,
    OSW_DRV_VIF_CONFIG_STA_RECONNECT,
    OSW_DRV_VIF_CONFIG_STA_DISCONNECT,
};

struct osw_drv_vif_config_sta {
    enum osw_drv_vif_config_sta_operation operation;
    struct osw_drv_vif_sta_network *network;
    bool network_changed;
};

struct osw_drv_vif_config {
    char *vif_name;
    bool changed;

    enum osw_vif_type vif_type;
    bool vif_type_changed;

    bool enabled;
    bool enabled_changed;

    int tx_power_dbm;
    bool tx_power_dbm_changed;

    union {
        struct osw_drv_vif_config_ap ap;
        struct osw_drv_vif_config_sta sta;
        /* ap_vlan configuration not supported for now */
    } u;
};

struct osw_drv_vif_config_list {
    struct osw_drv_vif_config *list;
    size_t count;
};

struct osw_drv_vif_state_ap {
    bool isolated;
    bool ssid_hidden;
    bool mcast2ucast;
    bool wps_pbc;
    int beacon_interval_tu;
    struct osw_ifname bridge_if_name;
    struct osw_nas_id nas_identifier;
    struct osw_ap_mode mode;
    struct osw_channel channel;
    struct osw_ssid ssid;
    struct osw_hwaddr_list acl;
    enum osw_acl_policy acl_policy;
    struct osw_wpa wpa;
    struct osw_ap_psk_list psk_list;
    struct osw_radius_list radius_list;
    struct osw_radius_list acct_list;
    struct osw_neigh_list neigh_list;
    struct osw_neigh_ft_list neigh_ft_list;
    struct osw_wps_cred_list wps_cred_list;
    struct osw_multi_ap multi_ap;
    struct osw_drv_mld_state mld;
    enum osw_mbss_vif_ap_mode mbss_mode;
    int mbss_group;
    struct osw_passpoint passpoint;
    struct osw_ft_encr_key ft_encr_key;
    bool ft_over_ds;
    bool ft_pmk_r1_push;
    bool ft_psk_generate_local;
    int ft_pmk_r0_key_lifetime_sec;
    int ft_pmk_r1_max_key_lifetime_sec;
    int ft_mobility_domain;
    bool mbo;
    bool oce;
    int oce_min_rssi_dbm;
    bool oce_min_rssi_enable;
    int oce_retry_delay_sec;
    int max_sta;
};

struct osw_drv_vif_state_ap_vlan {
    struct osw_hwaddr_list sta_addrs;
};

enum osw_drv_vif_state_sta_link_status {
    OSW_DRV_VIF_STATE_STA_LINK_UNKNOWN,
    OSW_DRV_VIF_STATE_STA_LINK_CONNECTED,
    OSW_DRV_VIF_STATE_STA_LINK_CONNECTING,
    OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED,
};

struct osw_drv_vif_state_sta_link {
    enum osw_drv_vif_state_sta_link_status status;
    struct osw_ifname bridge_if_name;
    struct osw_channel channel;
    struct osw_hwaddr bssid;

    /**
     * AP MLD Address if the link is tied to one.
     * Otherwise this holds 00:00:00:00:00:00 for
     * regular non-MLD links.
     */
    struct osw_hwaddr mld_addr;

    struct osw_ssid ssid;
    struct osw_psk psk;
    struct osw_wpa wpa;
    bool multi_ap;
};

struct osw_drv_vif_state_sta {
    /* The link describes the current, actual link state. It
     * may not match any of the data in network.
     */
    struct osw_drv_vif_state_sta_link link;

    /* The network defines a (list of) network(s) that the
     * driver currently has set for roaming purposes.
     */
    struct osw_drv_vif_sta_network *network;

    struct osw_drv_mld_state mld;
};

struct osw_drv_vif_state {
    bool exists;
    enum osw_vif_status status;
    enum osw_vif_type vif_type;
    struct osw_hwaddr mac_addr;
    int tx_power_dbm;

    union {
        struct osw_drv_vif_state_ap ap;
        struct osw_drv_vif_state_ap_vlan ap_vlan;
        struct osw_drv_vif_state_sta sta;
    } u;
};

struct osw_drv_phy_config {
    char *phy_name;
    bool changed;

    bool enabled;
    bool enabled_changed;

    int tx_chainmask;
    bool tx_chainmask_changed;

    struct osw_channel radar_next_channel;
    bool radar_next_channel_changed;

    enum osw_radar_detect radar;
    bool radar_changed;

    struct osw_reg_domain reg_domain;
    bool reg_domain_changed;

    struct osw_drv_vif_config_list vif_list;
};

enum osw_drv_phy_tx_chain {
     OSW_DRV_PHY_TX_CHAIN_UNSPEC,
     OSW_DRV_PHY_TX_CHAIN_SUPPORTED,
     OSW_DRV_PHY_TX_CHAIN_NOT_SUPPORTED,
};

struct osw_drv_phy_capab {
    enum osw_drv_phy_tx_chain tx_chain;
};

struct osw_drv_phy_state {
    struct osw_channel_state *channel_states;
    size_t n_channel_states;
    struct osw_reg_domain reg_domain;
    struct osw_hwaddr mac_addr;
    bool exists;
    bool enabled;
    int tx_chainmask;
    struct osw_channel radar_next_channel;
    enum osw_radar_detect radar;
    bool puncture_supported;
};

struct osw_drv_sta_state {
    int key_id;
    int connected_duration_seconds;
    bool connected;
    bool pmf;
    enum osw_akm akm;
    enum osw_cipher pairwise_cipher;

    /**
     * STA MLD Address if the (affiliated) STA is
     * part of an MLD. Otherwise this holds
     * 00:00:00:00:00:00 for regular non-MLD STAs.
     */
    struct osw_hwaddr mld_addr;
};

struct osw_drv_conf {
    struct osw_drv_phy_config *phy_list;
    size_t n_phy_list;
};

typedef void
osw_drv_report_phy_fn_t(const char *phy_name,
                        void *fn_priv);

typedef void
osw_drv_report_vif_fn_t(const char *vif_name,
                        void *fn_priv);

typedef void
osw_drv_report_sta_fn_t(const struct osw_hwaddr *mac_addr,
                        void *fn_priv);

typedef void
osw_drv_init_fn_t(struct osw_drv *drv);

typedef void
osw_drv_get_phy_list_fn_t(struct osw_drv *drv,
                          osw_drv_report_phy_fn_t *report_phy_fn,
                          void *fn_priv);

typedef void
osw_drv_get_vif_list_fn_t(struct osw_drv *drv,
                          const char *phy_name,
                          osw_drv_report_vif_fn_t *report_vif_fn,
                          void *fn_priv);

typedef void
osw_drv_get_sta_list_fn_t(struct osw_drv *drv,
                          const char *phy_name,
                          const char *vif_name,
                          osw_drv_report_sta_fn_t *report_sta_fn,
                          void *fn_priv);

typedef void
osw_drv_request_phy_state_fn_t(struct osw_drv *drv,
                               const char *phy_name);

typedef void
osw_drv_request_vif_state_fn_t(struct osw_drv *drv,
                               const char *phy_name,
                               const char *vif_name);

typedef void
osw_drv_request_sta_state_fn_t(struct osw_drv *drv,
                               const char *phy_name,
                               const char *vif_name,
                               const struct osw_hwaddr *mac_addr);

typedef void
osw_drv_request_config_fn_t(struct osw_drv *drv,
                            struct osw_drv_conf *conf);

/* FIXME: This should actually re-use the
 * push_frame_tx_fn and expect that to be handled
 * by the underlying driver appropriately. If the
 * driver has a dedicated interface/API call
 * internally to do deauth, instead of just mgmt
 * or action tx, then it can call that.
 */
typedef void
osw_drv_request_sta_deauth_fn_t(struct osw_drv *drv,
                                const char *phy_name,
                                const char *vif_name,
                                const struct osw_hwaddr *mac_addr,
                                int dot11_reason_code);

typedef void
osw_drv_request_sta_delete_fn_t(struct osw_drv *drv,
                                const char *phy_name,
                                const char *vif_name,
                                const struct osw_hwaddr *sta_addr);

typedef void
osw_drv_request_stats_fn_t(struct osw_drv *drv,
                           unsigned int stats_mask);

struct osw_drv_scan_params {
    const struct osw_channel *channels;
    size_t n_channels;
    unsigned int dwell_time_msec;
    bool passive;
};

typedef void
osw_drv_request_scan_fn_t(struct osw_drv *drv,
                          const char *phy_name,
                          const char *vif_name,
                          const struct osw_drv_scan_params *params);

typedef void
osw_drv_push_frame_tx_fn_t(struct osw_drv *drv,
                           const char *phy_name,
                           const char *vif_name,
                           struct osw_drv_frame_tx_desc *desc);

struct osw_drv_ops {
    const char *name;
    osw_drv_init_fn_t *init_fn;
    osw_drv_get_phy_list_fn_t *get_phy_list_fn;
    osw_drv_get_vif_list_fn_t *get_vif_list_fn;
    osw_drv_get_sta_list_fn_t *get_sta_list_fn;
    osw_drv_request_phy_state_fn_t *request_phy_state_fn;
    osw_drv_request_vif_state_fn_t *request_vif_state_fn;
    osw_drv_request_sta_state_fn_t *request_sta_state_fn;
    osw_drv_request_sta_deauth_fn_t *request_sta_deauth_fn;
    osw_drv_request_sta_delete_fn_t *request_sta_delete_fn;
    osw_drv_request_config_fn_t *request_config_fn;
    osw_drv_request_stats_fn_t *request_stats_fn;
    osw_drv_request_scan_fn_t *request_scan_fn;
    osw_drv_push_frame_tx_fn_t *push_frame_tx_fn;

    /* TODO
    request_vif_deauth_fn_t
    request_dpp_auth_fn_t
    request_dpp_auth_stop_fn_t
    request_dpp_chirp_fn_t
    request_dpp_chirp_stop_fn_t
    request_scan_fn_t
    request_stats_survey_fn_t
    request_stats_sta_fn_t
    request_send_action_frame_fn_t
    */
};

struct osw_drv_report_vif_probe_req {
    struct osw_hwaddr sta_addr;
    unsigned int snr; /* TODO Replace with osw_signal */
    struct osw_ssid ssid;
};

void
osw_drv_register_ops(const struct osw_drv_ops *ops);

void
osw_drv_unregister_ops(const struct osw_drv_ops *ops);

void
osw_drv_set_priv(struct osw_drv *drv, void *priv);

void *
osw_drv_get_priv(struct osw_drv *drv);

const struct osw_drv_ops *
osw_drv_get_ops(struct osw_drv *drv);

void
osw_drv_report_phy_changed(struct osw_drv *drv,
                           const char *phy_name);

void
osw_drv_report_phy_state(struct osw_drv *drv,
                         const char *phy_name,
                         const struct osw_drv_phy_state *state);

void
osw_drv_report_vif_probe_req(struct osw_drv *drv,
                             const char *phy_name,
                             const char *vif_name,
                             const struct osw_drv_report_vif_probe_req *probe_req);

struct osw_drv_vif_frame_rx {
    const uint8_t *data;
    size_t len;
    unsigned int snr;
};

void
osw_drv_report_vif_frame_rx(struct osw_drv *drv,
                            const char *phy_name,
                            const char *vif_name,
                            const struct osw_drv_vif_frame_rx *rx);

void
osw_drv_report_vif_changed(struct osw_drv *drv,
                           const char *phy_name,
                           const char *vif_name);

void
osw_drv_report_vif_state(struct osw_drv *drv,
                         const char *phy_name,
                         const char *vif_name,
                         const struct osw_drv_vif_state *state);

void
osw_drv_report_vif_channel_change_started(struct osw_drv *drv,
                                          const char *phy_name,
                                          const char *vif_name,
                                          const struct osw_channel *target_channel);

/* When STA link is active and the root AP advertises a CSA
 * - either through Beacon or Action frame - then this
 * function shall be called, regardless of whether the
 * target channel is serviceable by the STA link's radio or
 * not.
 */
void
osw_drv_report_vif_channel_change_advertised(struct osw_drv *drv,
                                             const char *phy_name,
                                             const char *vif_name,
                                             const struct osw_channel *channel);

void
osw_drv_report_vif_wps_success(struct osw_drv *drv,
                               const char *phy_name,
                               const char *vif_name);

void
osw_drv_report_vif_wps_overlap(struct osw_drv *drv,
                               const char *phy_name,
                               const char *vif_name);

void
osw_drv_report_vif_wps_pbc_timeout(struct osw_drv *drv,
                                   const char *phy_name,
                                   const char *vif_name);

void
osw_drv_report_sta_changed(struct osw_drv *drv,
                           const char *phy_name,
                           const char *vif_name,
                           const struct osw_hwaddr *mac_addr);

void
osw_drv_report_sta_state(struct osw_drv *drv,
                         const char *phy_name,
                         const char *vif_name,
                         const struct osw_hwaddr *mac_addr,
                         const struct osw_drv_sta_state *state);

void
osw_drv_report_stats(struct osw_drv *drv,
                     const struct osw_tlv *tlv);

void
osw_drv_report_stats_reset(enum osw_stats_id id);

void
osw_drv_conf_free(struct osw_drv_conf *conf);

/**
 * Lookup a matching osw_drv_vif_config per requested
 * phy_name and vif_name, within the osw_drv_conf.
 *
 * The phy_name can be omitted by passing NULL to match
 * based on vif_name only.
 *
 * vif_name is osw_drv is guaranteed to be unique. The
 * phy_name filtering is for increased consistency
 * guarantees for callers.
 *
 * @return a pointer to osw_drv_vif_config if a match is
 * found or NULL if no match is found.
 */
struct osw_drv_vif_config *
osw_drv_conf_lookup_vif(struct osw_drv_conf *conf,
                        const char *phy_name,
                        const char *vif_name);

bool
osw_drv_work_is_settled(void);

void
osw_drv_report_frame_tx_state_submitted(struct osw_drv *drv);

void
osw_drv_report_frame_tx_state_failed(struct osw_drv *drv);

void
osw_drv_report_sta_assoc_ies(struct osw_drv *drv,
                             const char *phy_name,
                             const char *vif_name,
                             const struct osw_hwaddr *sta_addr,
                             const void *ies,
                             const size_t ies_len);

enum osw_drv_scan_complete_reason {
    OSW_DRV_SCAN_DONE,
    OSW_DRV_SCAN_FAILED,
    OSW_DRV_SCAN_ABORTED,
    OSW_DRV_SCAN_TIMED_OUT,
};

void
osw_drv_report_scan_completed(struct osw_drv *drv,
                              const char *phy_name,
                              const char *vif_name,
                              enum osw_drv_scan_complete_reason reason);

const char *
osw_drv_scan_reason_to_str(enum osw_drv_scan_complete_reason reason);

void
osw_drv_report_overrun(struct osw_drv *drv);

void
osw_drv_invalidate(struct osw_drv *drv);

void
osw_drv_phy_state_report_free(struct osw_drv_phy_state *state);

void
osw_drv_vif_state_report_free(struct osw_drv_vif_state *state);

void
osw_drv_sta_state_report_free(struct osw_drv_sta_state *state);

const char *
osw_drv_vif_state_sta_link_status_to_cstr(enum osw_drv_vif_state_sta_link_status s);

const struct osw_channel *
osw_drv_vif_get_channel(const struct osw_drv_vif_state *state);

const struct osw_drv_vif_sta_network *
osw_drv_vif_sta_network_get(const struct osw_drv_vif_state_sta *vsta);

const struct osw_drv_mld_state *
osw_drv_vif_state_get_mld_state(const struct osw_drv_vif_state *state);

const char *
osw_drv_mld_state_get_name(const struct osw_drv_mld_state *mld_state);

/* TODO
void osw_drv_report_sta_deauth_tx
void osw_drv_report_sta_deauth_rx
void osw_drv_report_sta_disassoc_rx
void osw_drv_report_sta_disassoc_tx
void osw_drv_report_sta_authenticate_rx
void osw_drv_report_sta_authenticate_tx
void osw_drv_report_sta_associate_rx
void osw_drv_report_sta_associate_tx
void osw_drv_report_sta_authorize
void osw_drv_report_sta_deauthorize
void osw_drv_report_sta_low_ack
void osw_drv_report_phy_radar_detected
void osw_drv_report_vif_channel_changed
void osw_drv_report_vif_channel_change_started
void osw_drv_report_vif_disconnected
void osw_drv_report_vif_beacon_loss
void osw_drv_report_stats_survey
void osw_drv_report_stats_sta
void osw_drv_report_scan_entry
void osw_drv_report_scan_completed
void osw_drv_report_dpp_enrollee_handled
void osw_drv_report_dpp_auth_stopped
void osw_drv_report_dpp_chirp_stopped
void osw_drv_report_rx_probe_request
void osw_drv_report_rx_action_frame
*/

#define OSW_DRV_DEFINE(ops) OSW_MODULE(ops) { osw_drv_register_ops(&ops); return NULL; }

#endif /* OSW_DRV_H_INCLUDED */
