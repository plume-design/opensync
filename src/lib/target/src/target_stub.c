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

#include <stdbool.h>
#include <ev.h>
#include <assert.h>
#include <errno.h>

#include "ds.h"
#include "ds_dlist.h"
#include "os.h"
#include "log.h"
#include "target.h"
#include "target_impl.h"
#include "target_common.h"
#include "schema.h"
#include "build_version.h"

#define MODULE_ID LOG_MODULE_ID_TARGET

/******************************************************************************
 * Init
 *****************************************************************************/

#ifndef IMPL_target_ready
bool target_ready(struct ev_loop *loop)
{
    return true;
}
#endif

#ifndef IMPL_target_init
bool target_init(target_init_opt_t opt, struct ev_loop *loop)
{
    return true;
}
#endif

#ifndef IMPL_target_close
bool target_close(target_init_opt_t opt, struct ev_loop *loop)
{
    return true;
}
#endif


#ifndef IMPL_target_log_open
bool target_log_open(char *name, int flags)
{
    return log_open(name, flags);
}
#endif

#ifndef IMPL_target_log_pull
bool target_log_pull(const char *upload_location, const char *upload_token)
{
    return true;
}
#endif

#ifndef IMPL_target_log_pull_ext
bool target_log_pull_ext(const char *upload_location, const char *upload_token, const char *upload_method)
{
    return true;
}
#endif

#ifndef IMPL_target_log_state_file
// target_log_state_file is obsolete, not providing a stub
#else
#warning target_log_state_file is obsolete, use CONFIG_TARGET_PATH_LOG_STATE instead!
#endif

#ifndef IMPL_target_log_trigger_dir
// target_log_trigger_dir is obsolete, not providing a stub
#else
#warning target_log_trigger_dir is obsolete, use CONFIG_TARGET_PATH_LOG_TRIGGER instead!
#endif

#ifndef IMPL_target_tls_cacert_filename
const char *target_tls_cacert_filename(void)
{
    assert(!"tls_cacert_filename not defined for current platform");
    return NULL;
}
#endif

#ifndef IMPL_target_tls_mycert_filename
const char *target_tls_mycert_filename(void)
{
    assert(!"tls_mycert_filename not defined for current platform");
    return NULL;
}
#endif

#ifndef IMPL_target_tls_privkey_filename
const char *target_tls_privkey_filename(void)
{
    assert(!"tls_privkey_filename not defined for current platform");
    return NULL;
}
#endif

/******************************************************************************
 * RADIO
 *****************************************************************************/

#ifndef IMPL_target_radio_init
bool target_radio_init(const struct target_radio_ops *ops)
{
    return false;
}
#endif

#ifndef IMPL_target_radio_config_init2
bool target_radio_config_init2(void)
{
    return false;
}
#endif

#ifndef IMPL_target_radio_config_need_reset
bool target_radio_config_need_reset(void)
{
    return false;
}
#endif

#ifndef IMPL_target_radio_config_set2
bool target_radio_config_set2(const struct schema_Wifi_Radio_Config *rconf,
                              const struct schema_Wifi_Radio_Config_flags *changed)
{
  return true;
}
#endif

#ifndef IMPL_target_vif_sta_remove
bool target_vif_sta_remove(const char *ifname, const uint8_t *mac_addr)
{
    return false;
}
#endif

#ifndef IMPL_target_is_radio_interface_ready
bool target_is_radio_interface_ready(char *phy_name)
{
    return true;
}
#endif

#ifndef IMPL_target_radio_state_get
bool target_radio_state_get(char *ifname, struct schema_Wifi_Radio_State *rstate)
{
  return true;
}
#else
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#pragma message("target_radio_state_get is deprecated")
#endif
#endif

/******************************************************************************
 *  INTERFACE
 *****************************************************************************/

#ifndef IMPL_target_wan_interface_name
const char *target_wan_interface_name()
{
    const char *iface_name = "eth0";
    return iface_name;
}
#endif

#ifndef IMPL_target_is_interface_ready
bool target_is_interface_ready(char *if_name)
{
    return true;
}
#endif

/******************************************************************************
 * VIF
 *****************************************************************************/

#ifndef IMPL_target_vif_config_set3
bool target_vif_config_set3(const struct schema_Wifi_VIF_Config *vconf,
                            const struct schema_Wifi_Radio_Config *rconf,
                            const struct schema_Wifi_Credential_Config *cconfs,
                            const struct schema_Wifi_VIF_Config_flags *changed,
                            const struct schema_Wifi_VIF_Neighbors *nbors_list,
                            const struct schema_RADIUS *radius_list,
                            int num_cconfs,
                            int num_nbors_list,
                            int num_radius_list)
{
    (void) nbors_list;
    (void) radius_list;
    (void) num_nbors_list;
    (void) num_radius_list;

    LOG_ONCE(NOTICE, "Target does not implement target_vif_config_set3. "
                     "WPA2/3-Enterprise and Fast Transition won't work");
    return target_vif_config_set2(vconf, rconf, cconfs, changed, num_cconfs);
}
#endif

#ifndef IMPL_target_vif_config_set2
bool target_vif_config_set2(const struct schema_Wifi_VIF_Config *vconf,
                            const struct schema_Wifi_Radio_Config *rconf,
                            const struct schema_Wifi_Credential_Config *cconfs,
                            const struct schema_Wifi_VIF_Config_flags *changed,
                            int num_cconfs)
{
    return true;
}
#endif

#ifndef IMPL_target_vif_state_get
bool target_vif_state_get(char  *ifname, struct schema_Wifi_VIF_State *vstate)
{
    return true;
}
#else
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#pragma message("target_vif_state_get is deprecated")
#endif
#endif

/******************************************************************************
 * DPP
 *****************************************************************************/

#ifndef IMPL_target_dpp_supported
bool target_dpp_supported(void)
{
    return false;
}
#endif

#ifndef IMPL_target_dpp_config_set
bool target_dpp_config_set(const struct schema_DPP_Config **config)
{
    return false;
}
#endif

#ifndef IMPL_target_dpp_key_get
bool target_dpp_key_get(struct target_dpp_key *key)
{
    return false;
}
#endif

/******************************************************************************
 * Ethernet clients
 *****************************************************************************/

#ifndef IMPL_target_ethclient_brlist_get
const char **target_ethclient_brlist_get()
{
    static const char *brlist[] = { CONFIG_TARGET_LAN_BRIDGE_NAME, NULL };
    return brlist;
}
#endif

#ifndef IMPL_target_ethclient_iflist_get
const char **target_ethclient_iflist_get()
{
    static const char *iflist[] = { "eth0", NULL };
    return iflist;
}
#endif

/******************************************************************************
 * SERVICE
 *****************************************************************************/

#ifndef IMPL_target_device_config_register
bool target_device_config_register(void *awlan_cb)
{
    return true;
}
#endif

#ifndef IMPL_target_device_config_set
bool target_device_config_set(struct schema_AWLAN_Node *awlan)
{
    return true;
}
#endif

#ifndef IMPL_target_device_execute
bool target_device_execute(const char *cmd)
{
    return true;
}
#endif
#ifndef IMPL_target_device_connectivity_check
bool target_device_connectivity_check(const char *ifname,
                                      target_connectivity_check_t *cstate,
                                      target_connectivity_check_option_t opts)
{
    return true;
}
#endif

#ifndef IMPL_target_device_restart_managers_helper
bool target_device_restart_managers_helper(const char *calling_func)
{
    return true;
}
#endif

#ifndef IMPL_target_device_wdt_ping
bool target_device_wdt_ping()
{
    return true;
}
#endif

/******************************************************************************
 * DM
 *****************************************************************************/

/*
 * Give up on everything and just call the restart.sh script. This should reset the system
 * to a clean slate, restart OVSDB and kick off a new instance of DM.
 */
#ifndef IMPL_target_managers_restart_helper
void target_managers_restart_helper(const char *calling_func)
{
    int fd;
    char cmd[TARGET_BUFF_SZ];

    const char *scripts_dir = target_scripts_dir();
    int max_fd = sysconf(_SC_OPEN_MAX);

    LOG(EMERG, "=======  GENERAL RESTART  ========");

    SPRINTF(cmd, "%s/restart.sh", scripts_dir);

    LOG(EMERG, "Plan B is executing restart script: %s", cmd);

    /* Close file descriptors from 3 and above */
    for(fd = 3; fd < max_fd; fd++) close(fd);

    os_backtrace_dump_manager_restart(calling_func);
    /* When the parent process exits, the child will get disowned */
    if (fork() != 0)
    {
        exit(1);
    }

    setsid();

    /* Execute the restart script */
    if (scripts_dir == NULL)
    {
        LOG(ERR, "Error, script dir not defined");
    }
    else
    {
        struct stat sb;
        if ( !(0 == stat(scripts_dir, &sb) && S_ISDIR(sb.st_mode)) )
        {
            LOG(ERR, "Error, scripts dir does not exist");
        }
    }
    execl(cmd, cmd, NULL);

    LOG(EMERG, "Failed to execute plan B!");
}
#endif

/******************************************************************************
 * NM
 *****************************************************************************/
#ifndef IMPL_target_mac_learning_register
bool target_mac_learning_register(target_mac_learning_cb_t *omac_cb)
{
    return false;
}
#endif

/******************************************************************************
 * PATHS
 *****************************************************************************/
#ifndef IMPL_target_tools_dir
const char* target_tools_dir(void)
{
    assert(!"tools_dir not defined for current platform.");
    return NULL;
}
#endif

#ifndef IMPL_target_bin_dir
const char* target_bin_dir(void)
{
    assert(!"bin_dir not defined for current platform.");
    return NULL;
}
#endif

#ifndef IMPL_target_scripts_dir
const char* target_scripts_dir(void)
{
    // For backwards compatibility, return bin dir
    return target_bin_dir();
}
#endif

#ifndef IMPL_target_persistent_storage_dir
const char *target_persistent_storage_dir(void)
{
    assert(!"persistent_storage_dir not defined for current platform.");
    return NULL;
}
#endif

/******************************************************************************
 * STATS
 *****************************************************************************/
#ifndef IMPL_target_radio_tx_stats_enable
bool target_radio_tx_stats_enable(
        radio_entry_t              *radio_cfg,
        bool                        status)
{
    return false;
}
#endif

#ifndef IMPL_target_radio_fast_scan_enable
bool target_radio_fast_scan_enable(
        radio_entry_t              *radio_cfg,
        ifname_t                    if_name)
{
    return false;
}
#endif

#ifndef IMPL_target_stats_scan_get
bool target_stats_scan_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        dpp_neighbor_report_data_t *scan_results)
{
    return false;
}
#endif

#ifndef IMPL_target_stats_scan_start
bool target_stats_scan_start(radio_entry_t *radio_cfg,
                             uint32_t *chan_list,
                             uint32_t chan_num,
                             radio_scan_type_t scan_type,
                             int32_t dwell_time,
                             target_scan_cb_t *scan_cb,
                             void *scan_ctx)
{
    return false;
}
#endif

#ifndef IMPL_target_stats_survey_get
bool target_stats_survey_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        target_stats_survey_cb_t   *survey_cb,
        ds_dlist_t                 *survey_list,
        void                       *survey_ctx)
{
    return false;
}
#endif

#ifndef IMPL_target_stats_survey_convert
bool target_stats_survey_convert (
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type,
        target_survey_record_t     *data_new,
        target_survey_record_t     *data_old,
        dpp_survey_record_t        *survey_record)
{
    return false;
}
#endif

#ifndef IMPL_target_survey_record_alloc
target_survey_record_t *target_survey_record_alloc()
{
    return NULL;
}
#endif

#ifndef IMPL_target_stats_device_get
bool target_stats_device_get(
        dpp_device_record_t        *device_entry)
{
    static bool printed = false;
    if (!printed) {
        LOG(DEBUG, "Sending device report: stats not supported");
        printed = true;
    }
    return false;
}
#endif

#ifndef IMPL_target_stats_device_temp_get
bool target_stats_device_temp_get(
        radio_entry_t              *radio_cfg,
        dpp_device_temp_t          *temp_entry)
{
    static bool printed = false;
    if (!printed) {
        LOG(DEBUG, "Sending device report: temperature not supported");
        printed = true;
    }
    return false;
}
#endif

#ifndef IMPL_target_stats_device_fanrpm_get
bool target_stats_device_fanrpm_get(uint32_t        *fan_rpm)
{
    static bool printed = false;
    if (!printed) {
        LOG(DEBUG, "Sending device report: FAN rpm not supported");
        printed = true;
    }
    return false;
}
#endif

#ifndef IMPL_target_stats_device_fandutycycle_get
bool target_stats_device_fandutycycle_get(uint16_t *fan_duty_cycle)
{
    static bool printed = false;
    (void)fan_duty_cycle;

    if (!printed) {
        LOG(DEBUG, "Sending device report: FAN duty cycle not supported");
        printed = true;
    }

    return false;
}
#endif

#ifndef IMPL_target_get_btrace_type
btrace_type target_get_btrace_type()
{
    return BTRACE_FILE_LOG;
}
#endif

#ifndef IMPL_target_client_record_alloc
target_client_record_t *target_client_record_alloc()
{
    return NULL;
}
#endif

#ifndef IMPL_target_client_record_free
void target_client_record_free(target_client_record_t *record)
{
    FREE(record);
}
#endif

#ifndef IMPL_target_survey_record_free
void target_survey_record_free(target_survey_record_t *result)
{
    FREE(result);
}
#endif

#ifndef IMPL_target_stats_clients_convert
bool target_stats_clients_convert(
        radio_entry_t              *radio_cfg,
        target_client_record_t     *data_new,
        target_client_record_t     *data_old,
        dpp_client_record_t        *client_result)
{
    return false;
}
#endif

#ifndef IMPL_target_stats_clients_get
bool target_stats_clients_get(
        radio_entry_t              *radio_cfg,
        radio_essid_t              *essid,
        target_stats_clients_cb_t  *client_cb,
        ds_dlist_t                 *client_list,
        void                       *client_ctx)
{
    return false;
}
#endif

#ifndef IMPL_target_stats_scan_stop
bool target_stats_scan_stop(
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type)
{
    return false;
}
#endif

/******************************************************************************
 * BSAL
 *****************************************************************************/
#ifndef IMPL_target_bsal_init
int target_bsal_init(bsal_event_cb_t event_cb, struct ev_loop* loop)
{
    (void)event_cb;
    (void)loop;
    return -1;
}
#endif

#ifndef IMPL_target_bsal_cleanup
int target_bsal_cleanup( void )
{
    return -1;
}
#endif

#ifndef IMPL_target_bsal_iface_add
int target_bsal_iface_add(const bsal_ifconfig_t *ifcfg)
{
    (void)ifcfg;
    return -1;
}
#endif

#ifndef IMPL_target_bsal_iface_update
int target_bsal_iface_update(const bsal_ifconfig_t *ifcfg)
{
    (void)ifcfg;
    return -1;
}
#endif

#ifndef IMPL_target_bsal_iface_remove
int target_bsal_iface_remove(const bsal_ifconfig_t *ifcfg)
{
    (void)ifcfg;
    return -1;
}
#endif

#ifndef IMPL_target_bsal_client_add
int target_bsal_client_add(const char *ifname, const uint8_t *mac_addr,
                           const bsal_client_config_t *conf)
{
    (void)ifname;
    (void)mac_addr;
    (void)conf;
    return -1;
}
#endif

#ifndef IMPL_target_bsal_client_update
int target_bsal_client_update(const char *ifname, const uint8_t *mac_addr,
                              const bsal_client_config_t *conf)
{
    (void)ifname;
    (void)mac_addr;
    (void)conf;
    return -1;
}
#endif

#ifndef IMPL_target_bsal_client_remove
int target_bsal_client_remove(const char *ifname, const uint8_t *mac_addr)
{
    (void)ifname;
    (void)mac_addr;
    return -1;
}
#endif

#ifndef IMPL_target_bsal_client_measure
int target_bsal_client_measure(const char *ifname, const uint8_t *mac_addr,
                               int num_samples)
{
    (void)ifname;
    (void)mac_addr;
    (void)num_samples;
    return -1;
}
#endif

#ifndef IMPL_target_bsal_client_disconnect
int target_bsal_client_disconnect(const char *ifname, const uint8_t *mac_addr,
                                  bsal_disc_type_t type, uint8_t reason)
{
    (void)ifname;
    (void)mac_addr;
    (void)type;
    (void)reason;
    return -1;
}
#endif

#ifndef IMPL_target_bsal_client_info
int target_bsal_client_info(const char *ifname, const uint8_t *mac_addr, bsal_client_info_t *info)
{
    (void)ifname;
    (void)mac_addr;
    (void)info;
    return -1;
}
#endif

#ifndef IMPL_target_bsal_bss_tm_request
int target_bsal_bss_tm_request(const char *ifname, const uint8_t *mac_addr,
                               const bsal_btm_params_t *btm_params)
{
    (void)ifname;
    (void)mac_addr;
    (void)btm_params;
    return -1;
}
#endif

#ifndef IMPL_target_bsal_rrm_beacon_report_request
int target_bsal_rrm_beacon_report_request(const char *ifname,
                        const uint8_t *mac_addr, const bsal_rrm_params_t *rrm_params)
{
    (void)ifname;
    (void)mac_addr;
    (void)rrm_params;
    return -1;
}
#endif

#ifndef IMPL_target_bsal_rrm_set_neighbor
int target_bsal_rrm_set_neighbor(const char *ifname, const bsal_neigh_info_t *nr)
{
    (void)ifname;
    (void)nr;
    return -1;
}
#endif

#ifndef IMPL_target_bsal_rrm_remove_neighbor
int target_bsal_rrm_remove_neighbor(const char *ifname, const bsal_neigh_info_t *nr)
{
    (void)ifname;
    (void)nr;
    return -1;
}
#endif

#ifndef IMPL_target_bsal_rrm_get_neighbors
int target_bsal_rrm_get_neighbors(const char *ifname, bsal_neigh_info_t *nrs,
                                  unsigned int *nr_cnt, const unsigned int max_nr_cnt)
{
    (void)ifname;
    (void)nrs;
    (void)nr_cnt;
    (void)max_nr_cnt;
    return -1;
}
#endif

#ifndef IMPL_target_bsal_send_action
int target_bsal_send_action(const char *ifname, const uint8_t *mac_addr, const uint8_t *data, unsigned int data_len)
{
    (void)ifname;
    (void)mac_addr;
    (void)data;
    (void)data_len;
    return -ENOTSUP;
}
#endif
