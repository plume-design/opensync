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

/*!
 * File containing all the unit test api's related to test target library
 * Which will be invoked from main() of unit_test_target.c
 ***/

#include <errno.h>

#include "header.h"
#include "kconfig.h"

/*! \brief
 * Function to test the following:
 * Existence of /proc/locadavg and /proc/uptime.
 * Fetching Loadavg and uptime of device
 *
 * @param[out] pass (if able to verify existence of files and fetched the uptime
 * avgload) fail otherwise
 */
void test_target_stats_device_get()
{
    /*setup*/
    dpp_device_record_t *device_entry = NULL;
    device_entry = (dpp_device_record_t *) malloc( sizeof(dpp_device_record_t));

    if (NULL == device_entry) {
        LOGI("Failed to malloc");
        return;
    }

    int *ptr = (int *) malloc (sizeof(int));
    if (NULL == ptr) {
        LOGI("Failed to malloc");
        return;
    }
    char *cmd1 = "/proc/loadavg";
    char *cmd2 = "/proc/uptime";
    bool ret;

    /*run_test*/
    if (access(cmd1, F_OK)) {
        TEST_ASSERT_FALSE_MESSAGE(1, "Failed, file /proc/loadavg does not exist");
    }

    /*If /proc/uptime is not present and expecting test to fail*/
    if (access(cmd2, F_OK)) {
        TEST_ASSERT_FALSE_MESSAGE(1, "Failed, file /proc/uptime does not exist");
    }

    /*Passing correct argument and expecting test to Pass*/
    ret = target_stats_device_get(device_entry);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Passes, able to get device stats");

    /*teardown */
    if (device_entry)
        free(device_entry);

    if(ptr)
        free(ptr);

}//END OF FUNCTION

/*! \brief
 * Function to test the api's which are getting device info, like serial_no, sku_no
 * model_no, version and h/w revision
 *
 * @param[out] pass (if could able to get device info) fail otherwise
 */
void test_target_device_info()
{
    /*setup*/
    char buf[BUFFER_SIZE];
    int ret;
    target_init(TARGET_INIT_COMMON, NULL);

    ret = osp_unit_serial_get(buf, sizeof(buf));
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, to get serial no");

    ret = osp_unit_sku_get(buf, sizeof(buf)) ;
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, to get sku no");

    ret = osp_unit_model_get(buf, sizeof(buf));
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, to get model no");

    ret = osp_unit_sw_version_get(buf, sizeof(buf));
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, to get software version");

    ret = osp_unit_hw_revision_get(buf, sizeof(buf));
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, to get hardware revision");

}//END OF FUNCTION

/*! \brief
 * Function to test target_map_init, which initializes the map for specific
 * target, which contains list of interfaces in the form of map_list
 *
 * @param[out] pass(if could able to initialize the map) fail otherwise
 */
void test_target_map_init()
{
    bool ret = target_map_init();
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, to initialize map");

}//END OF FUNCTION

/*! \brief
 * Function to test target_map_insert, which takes if_name and map_name as input
 * and add to map_list
 *
 * @param[out] pass (if able to add map to map_list) fail otherwise
 */
void test_target_map_insert()
{
    /*setup*/
    char *map_name = "map3";
    int ret;

#ifdef CONFIG_TARGET_WAN_BRIDGE_NAME
    char *if_name = CONFIG_TARGET_WAN_BRIDGE_NAME"3";
#else
    char *if_name = "br-wan3";
#endif

    /*if map is not initialized and other inputs are correct*/
    target_map_close();
    ret = target_map_insert(if_name, map_name);
    test_target_map_init();/*changing to its original form*/

    TEST_ASSERT_FALSE_MESSAGE(ret, "Failed, map is not initialized");

    /*if if_name and map_name is correct and map is also initialized*/
    test_target_map_init();
    ret = target_map_insert(if_name, map_name);

    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, not able to insert to map");

}//END OF FUNCTION

/*! \brief
 * Function to test target_map_ifname, which maps the if_name in the map_list
 * returns the map_name
 *
 * @param[out] pass (if if_name do exist in the map_list) fail otherwise
 */
void test_target_map_ifname()
{
    /*setup*/
    char *if_name = "home-ap-24";
    char *ret = NULL;
    bool ret_val;
    char *map_name = "map3";

    /*Providing correct if_name but map is closed*/
    test_target_map_close();
    ret = target_map_ifname(if_name);
    TEST_ASSERT_FALSE_MESSAGE((ret != if_name), "Failed, map is closed");
    /*map is initialized and providing correct if_name*/
    test_target_map_init();
    ret_val = target_map_insert(if_name, map_name);
    TEST_ASSERT_FALSE_MESSAGE(!ret_val, "Failed, map is not initialized");
    ret = target_map_ifname(if_name);
    TEST_ASSERT_FALSE_MESSAGE((ret == if_name), "Failed, to map if_name to map_name");
    /*Invoking other function with map_name as input*/
    test_target_unmap_ifname(ret);
    ret_val = target_map_insert(if_name, map_name);
    TEST_ASSERT_FALSE_MESSAGE(!ret_val, "Failed, map is not initialized");
    ret = target_map_ifname(if_name);
    test_target_unmap_ifname_exists(ret);
}//END OF FUNCTION

/*! \brief
 * Function to test target_unmap_ifname, which takes map as input
 * and returns the if_name from the map
 *
 * @param[out] pass (if map_name is exist in the map) fail otherwise
 */
void test_target_unmap_ifname(char *map_name)
{
    char *if_name = NULL;

    /*providing wrong if_name as input*/
    if_name = target_unmap_ifname("br1");
    TEST_ASSERT_FALSE_MESSAGE((if_name == map_name), "Failed, if_name is wrong");

    /*Providing map_name and map is also initialized*/
    if_name = target_unmap_ifname(map_name);
    TEST_ASSERT_FALSE_MESSAGE((if_name == map_name), "Failed, to get if_name");

    /*Providing map_name is input and map is closed*/
    test_target_map_close();
    if_name = target_unmap_ifname(map_name);
    test_target_map_init();/*converting map to it's original form*/
    TEST_ASSERT_FALSE_MESSAGE((if_name != map_name), "Failed, to get if_name because map is closed");

}//END OF FUNCTION


/*! \brief
 * Function to test target_map_ifname_exists, which check the existence of map
 * for if_name, which is passed from from the called function
 *
 * @param[out] pass(if map for if_name exist) fail otherwise
 */
void test_target_map_ifname_exists()
{
    /*setup*/
    char *if_name = "home-ap-24";
    char *map_name = "map3";
    int ret;

    /*Input is correct but map is closed*/
    test_target_map_close();
    ret = target_map_ifname_exists(if_name);
    test_target_map_init();
    TEST_ASSERT_FALSE_MESSAGE(ret, "Failed, to check existence of if_name, because map is closed");
    /*Input is correct and map is openned*/
    ret = target_map_insert(if_name, map_name);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, map is not initialized");
    ret = target_map_ifname_exists(if_name);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, if_name does not exist in map");

}//END OF FUNCTION

/*! \brief
 * Function to test target_map_close, which closes the running map of interfaces
 *
 * @param[out] pass (if able to close map) fail otherwise
 */
void test_target_map_close()
{
    bool ret = target_map_close();
    test_target_map_init();
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, to close the map");

}//END OF FUNCTION

/*! \brief
 * Function to test target_unmap_ifname_exists, which check the map entry for the
 * given if_name
 *
 * @param[out] pass (if if_name exist in the map) fail otherwise
 */
void test_target_unmap_ifname_exists(char *map_name)
{
    /*setup*/
    int ret;

    /*Providing wrong map_name */
    ret = target_unmap_ifname_exists("sam");
    TEST_ASSERT_FALSE_MESSAGE(ret, "Failed, map_name is invalid");

    /*Providing correct map_name and map is opened*/
    test_target_map_init();
    ret = target_unmap_ifname_exists(map_name);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, map_name doesn't exist in map");

    /*Proving correct map_name but map is closed*/
    test_target_map_close();
    ret = target_unmap_ifname_exists(map_name);
    test_target_map_init();
    TEST_ASSERT_FALSE_MESSAGE(ret, "Failed, to check map_name, because map is closed");

}//END OF FUNCTION


/*! \brief
 * Function to target_tools_dir, which returns path tools directory
 * where all the plume related tools will be present
 *
 * @param[out] pass(if returned path is not NULL) fail otherwise
 */
void test_target_tools_dir()
{
    const char *temp = target_tools_dir();
    TEST_ASSERT_FALSE_MESSAGE(!temp, "Failed, because "CONFIG_INSTALL_PREFIX"/tools not exist");

}//END OF FUNCTION

/*! \brief
 * Function to test target_scripts_dir, which returns path of bin directory
 * where all the plume related binaries will be present
 *
 * @param[out] pass(if returned path is not NULL) fail otherwise
 */
void test_target_bin_dir()
{
    const char *temp = target_bin_dir();
    TEST_ASSERT_FALSE_MESSAGE(!temp, "Failed, because "CONFIG_INSTALL_PREFIX"/bin not exist");

}//END OF FUNCTION

/*! \brief
 * Function to test target_scripts_dir, which returns path of script directory
 * where all the plume related scripts will be present
 *
 * @param[out] pass(if returned path is not NULL) fail otherwise
 */
void test_target_scripts_dir()
{
    const char *temp = target_scripts_dir();
    TEST_ASSERT_FALSE_MESSAGE(!temp, "Failed, because "CONFIG_INSTALL_PREFIX"/bin not exist");

}//END OF FUNCTION

/*! \brief
 * Function to test target_persistent_storage_dir, which returns the /etc/plume
 * location which is defined in macro
 *
 * @param[out] pass(if location returned) fail otherwise
 */
void test_target_persistent_storage_dir()
{
    const char *temp = target_persistent_storage_dir();
    TEST_ASSERT_FALSE_MESSAGE(!temp, "Failed, because "CONFIG_TARGET_PATH_PERSISTENT" not exist");

}//END OF FUNCTION

/*! \brief
 * Function to test target_is_radio_interface_ready, which test radio interface
 * is ready or not
 *
 * @param[out] pass(if given radio-interface is up) fail otherwise
 */
void test_target_is_radio_interface_ready()
{
    /*setup*/
    char *radio_if_name = "wifi0";
    bool ret;

    ret = target_is_radio_interface_ready(radio_if_name);
    if (ret == false) {
#ifdef CONFIG_TARGET_WAN_BRIDGE_NAME
        ret = target_is_interface_ready(CONFIG_TARGET_WAN_BRIDGE_NAME);
#else
        ret = target_is_interface_ready("br-wan");
#endif
        return; /*means br-wan is up, so wifi0 should be down*/
    }
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, radio-interface wifi0 is not ready");
    TEST_ASSERT_FALSE_MESSAGE(!ret, "PASS");

}//END OF FUNCTION

/*! \brief
 * Function tp test target_is_interface_ready which takes if_name as argument
 * and checks, if_name is ready or not
 *
 * @param[out] pass (if given if_name is up) fail otherwise
 */
void test_target_is_interface_ready()
{
    /*setup*/
    bool ret;

#ifdef CONFIG_TARGET_WAN_BRIDGE_NAME
        char *if_name = CONFIG_TARGET_WAN_BRIDGE_NAME;
#else
        char *if_name = "br-home";
#endif
    ret = target_is_interface_ready(if_name);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, if_name is not ready");

}//END OF FUNCTION

/*! \brief
 * Function to test target_survey_record_alloc which allocates memory by malloc
 *
 * @param[out] pass(if able to allocate memory) fail otherwise
 */
void test_target_survey_record_alloc()
{
    target_survey_record_t *ptr = target_survey_record_alloc();
    TEST_ASSERT_FALSE_MESSAGE(!ptr , "Failed, to allocate memory for survey record");

}//END OF FUNCTION

/*! \brief
 * Function to test target_ethclient_iflist_get function which returns array of
 * character array
 *
 * @param[out] pass(if @ptr contains correct data) fail otherwise
 */
void test_target_ethclient_iflist_get()
{
    /*setup*/
    const char **ptr = NULL;

    ptr = target_ethclient_iflist_get();
    TEST_ASSERT_FALSE_MESSAGE(!ptr, "Failed, to get eth if_list" );
}//END OF FUNCTION

/*! \brief
 * Function to fill Wifi_Master_State info to @master
 * Can be called from any api's which needs Wifi_Master_State info
 *
 * @param[in] master to get master state info
 * @param[out] Fills @master with appropriate data and return true on success false
 *            otherwise
 */
bool get_Wifi_Master_State_Info(ovsdb_table_t *master)
{
    if (!master) return false;
    memset(master, 0, sizeof(ovsdb_table_t));
    return true;

}//END OF FUNCTION

/*! \brief
 * Function to fill SM radio info to @radio
 * Which can be called from any api's
 *
 * @param[in] radio to get SM radio_state
 * @param[out] fills @radio and returns true on success fail otherwise
 */
bool get_sm_radio_info(sm_radio_state_t *radio, enum data_type type)
{
    struct ev_loop *loop = EV_DEFAULT;
    target_init(TARGET_INIT_MGR_SM, loop);
    if(!radio) return false;
    bool status = true;

    #if defined(CONFIG_PLATFORM_IS_BCM)
    switch(type) {
        case VALID:
            memset(radio, 0, sizeof(sm_radio_state_t));
            STRSCPY(radio->config.phy_name, "wl0");
            break;
        case INVALID:
            memset(radio, 0, sizeof(sm_radio_state_t));
            STRSCPY(radio->config.phy_name, "wl1");
            break;
        default:
            status=false;
    }
    #else
    switch(type) {
        case VALID:
            memset(radio, 0, sizeof(sm_radio_state_t));
            STRSCPY(radio->config.if_name, "wifi0");
            break;
        case INVALID:
            memset(radio, 0, sizeof(sm_radio_state_t));
            STRSCPY(radio->config.if_name, "wifi2");
            break;
        default:
            status=false;
    }
    #endif
    return status;
}//END OF FUNCTION

/*! \brief
 * Function to test target_stats_device_temp_get() function which gets the device
 * temperature from device
 *
 * @param[out] pass(if temperature info written to @temp) fail otherwise
 */
void test_target_stats_device_temp_get()
{
    /*setup*/
    sm_radio_state_t radio;
    dpp_device_temp_t temp;
    bool ret;

    ret = get_sm_radio_info(&radio, INVALID);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, to fill radio related info");
    ret = get_sm_radio_info(&radio, VALID);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, to fill radio related info");
    ret = target_stats_device_temp_get(&radio.config, &temp);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, to get tempreture");

}// END OF FUNCTION

/*!\brief
 * Function generate fake data fills to @radio. Data can be valid and invalid
 * and can be invoked from any function
 *
 * @param[in]  radio to get fake data
 * @param[out] returns true on success false otherwise
 */
bool get_radio_info(radio_entry_t *radio, enum data_type type)
{
    if (!radio) return false;
    bool status = true;

    switch(type) {
        case VALID:
            memset(radio, 0, sizeof(radio_entry_t));
        break;

        case INVALID:
            memset(radio, 0, sizeof(radio_entry_t));
        break;

        default:
            status=false;
        break;
    }
    return status;

}//END OF FUNCTION

/*!\brief
 * Function to test target_stats_capacity_get function, which fetched target
 * stats capacity.
 *
 * @param[out] pass(if target_stats_capacity_get filled correct data to @radio_cfg_ctx)
 *             fail otherwise.
 */
void test_target_stats_capacity_get()
{
    /*
     * On some platforms (BCM) target_capacity_data_t is void, use a pointer below to silence
     * compiler errors
     */
    target_capacity_data_t  *results;
    radio_entry_t  radio_cfg_ctx;
    bool ret;

    if (!kconfig_enabled(CONFIG_SM_CAPACITY_QUEUE_STATS)) {
        TEST_IGNORE_MESSAGE("Capacity stats are disabled");
        return;
    }

    results = malloc(sizeof(*results));
    memset(results, 0, sizeof(*results));

    ret = get_radio_info(&radio_cfg_ctx, INVALID);/*To get radio info*/
    TEST_ASSERT_FALSE_MESSAGE(false, "Failed, to fill radio related info");
    ret = target_stats_capacity_get (&radio_cfg_ctx, results);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, to get updated stats capacity");

    ret = get_radio_info(&radio_cfg_ctx, VALID);/*To get radio info*/
    ret = target_stats_capacity_get (&radio_cfg_ctx, results);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, to get updated stats capacity");

    free(results);

}//END OF FUNCTION

/*! \brief
 * Function to test target_device_execute function, which checks the existence
 * of /dev/watchdog and /dev/watchdog
 *
 * @param[out] pass if, target_device_execute returns true fail otherwise
 */
void test_target_device_execute()
{
    const char *cmd = NULL;
    bool ret;

    ret = target_device_execute(cmd);
    TEST_ASSERT_FALSE_MESSAGE(ret, "failed, because input is NULL");

    cmd = "sls";
    ret = target_device_execute(cmd);
    TEST_ASSERT_FALSE_MESSAGE(ret, "failed, because input is not a valid command");

    cmd = "[ -e /dev/watchdog ] && echo 'V' > /dev/watchdog";
    if (access("/dev/watchdog", F_OK)) {
         TEST_ASSERT_FALSE_MESSAGE(true, "Failed, file /dev/watchdog does not exist");
    }

}//END OF FUNCTION

/*! \brief
 * Function to test target_device_wdt_ping function, which checks existence of
 * CONFIG_INSTALL_PREFIX/bin/wpd and /dev/watchdog and performs ping operation after a
 * certain timed out
 * @param[out] pass, if target_device_wdt_ping returns true, fail otherwise
 */
void test_target_device_wdt_ping()
{
    if(access(CONFIG_INSTALL_PREFIX"/bin/wpd", F_OK)){
        TEST_ASSERT_FALSE_MESSAGE(true, "Failed, beacuse "CONFIG_INSTALL_PREFIX"/bin/wpd doesn't exist");
    }

    if(access("/dev/watchdog", F_OK)){
        TEST_ASSERT_FALSE_MESSAGE(true, "Failed, beacuse /dev/watchdog doesn't exist");
    }

    bool ret = target_device_wdt_ping();
    TEST_ASSERT_FALSE_MESSAGE(!ret, "failed, to wdt_ping..");

}//END OF FUNCTION

/*! \brief
 * Function to check the existence of CONFIG_INSTALL_PREFIX/bin/restart.sh and to test
 * target_device_restart_managers function, which restart all managers.
 * @param[out] pass, if target_device_restart_managers returns true fail otherwise
 */
void test_target_device_restart_managers()
{
    bool ret;
    if (access(CONFIG_INSTALL_PREFIX"/bin/restart.sh", F_OK)) {
        TEST_ASSERT_FALSE_MESSAGE(1,"Failed, because "CONFIG_INSTALL_PREFIX"/bin/restart.sh doesn't exist");
    }
    ret = target_device_restart_managers();
    TEST_ASSERT_FALSE_MESSAGE(!ret, "failed, to restart managers");

}//END OF FUNCTION

/*! \brief
 * Function to test target_device_capabilities_get, which returns the device
 * capabilities
 * @param[out] pass, if returns true, fail otherwise
 */
void test_target_device_capabilities_get()
{
    bool ret;
    ret = target_device_capabilities_get();
    TEST_ASSERT_FALSE_MESSAGE(!ret, "failed, to get the capabilities");

}//END OF FUNCTION

/*! \brief
 * Function to test target_device_connectivity_check, which checks link connectivity,
 * router state, ntp and internet connectivity
 * @param[out] pass, if target_device_connectivity_check returns true, fail otherwise
 */
void test_target_device_connectivity_check()
{
    bool ret;
    char *ifname = "bhaul-sta-24";
    target_connectivity_check_t cstate;
    target_connectivity_check_option_t opts;

    opts = LINK_CHECK | ROUTER_CHECK | NTP_CHECK | INTERNET_CHECK;

    memset(&cstate, 0, sizeof(target_connectivity_check_t));
    ret = target_device_connectivity_check(ifname, &cstate, opts);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "failed, to check connectivity");

}//END OF FUNCTION


/*! \brief
 * Function to test target_radio_init, which initializes VIF_Config and
 * Radio_Config and sets some callbacks
 * @param[out] pass, if returns true, fail otherwise
 */
void test_target_radio_init()
{
    bool ret;
    struct target_radio_ops radio_ops = {
    .op_vconf = NULL,
    .op_rconf = NULL,
    .op_vstate = NULL,
    .op_rstate = NULL,
    .op_client = NULL,
    .op_clients = NULL,
    .op_flush_clients = NULL,
};

    ret = target_radio_init(&radio_ops);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "failed, to radio initialization");

}//END OF FUNCTION

/*! \brief
 * Function to test target_vif_state_get, which gets vif_state info
 * @param[out] pass, if target_vif_state_get returns true, fail otherwise
 */
void test_target_vif_state_get()
{
    bool ret;
    char *ifname = "home-ap-24";
    struct schema_Wifi_VIF_State vstate;

    ret = target_vif_state_get(ifname, &vstate);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "failed to get vif_state");

}//END OF FUNCTION

/*! \brief
 * Function to test target_radio_state_get, which gets Radio_state
 * information.
 * @param[out] pass, if target_radio_state_get returns true,fail otherwise
 */
void test_target_radio_state_get()
{
    bool ret;
    char *ifname = "wifi0";
    struct schema_Wifi_Radio_State rstate;

    memset(&rstate, 0, sizeof(rstate));
    test_target_radio_init();
    ret = target_radio_state_get(ifname, &rstate);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "failed to get vif_state");

}//END OF FUNCTION

/*! \brief
 * Function to fill the fake data to @vconf, @rconf, @changed
 * @param[out] pass, if able to fills the fake data, fail otherwise
 */
bool fill_vif_config(struct schema_Wifi_VIF_Config *vconf,
  struct schema_Wifi_Radio_Config *rconf, struct schema_Wifi_VIF_Config_flags *changed)
{
    memset (vconf, 0, sizeof(struct schema_Wifi_VIF_Config));
    memset (rconf, 0, sizeof(struct schema_Wifi_Radio_Config));
    memset (changed, 0, sizeof(struct schema_Wifi_VIF_Config_flags));
    STRSCPY(vconf->if_name, "bhaul-ap-24");
    vconf->vif_radio_idx = 1;
    STRSCPY(vconf->mode, "ap");
    vconf->min_hw_mode_exists = true;
    vconf->ssid_broadcast_exists = true;
    vconf->ap_bridge = true;
    vconf->uapsd_enable = true;
    rconf->channel_exists = true;
    rconf->channel = 1;
    STRSCPY(rconf->freq_band, "2.4G");
    rconf->hw_mode_exists= true;
    STRSCPY(rconf->hw_mode, "11n");
    STRSCPY(rconf->ht_mode, "HT40");
    changed->enabled = true;
    changed->mode = true;
    changed->vif_radio_idx = false;
    changed->ap_bridge = false;
    changed->uapsd_enable = false;
    changed->vif_dbg_lvl = false;
    changed->rrm = false;
    changed->mac_list_type = false;
    changed->mac_list = false;
    return true;
}

/*! \brief
 * Function to test target_vif_config_set2, which will set VIF config as per
 * changes done, mentioned in @changed
 *
 * @param[out] pass, if able to set vif_config, fail otherwise
 */
void test_target_vif_config_set2()
{
    /*setup */
    struct schema_Wifi_VIF_Config *vconf = NULL;
    struct schema_Wifi_Radio_Config *rconf = NULL;
    struct schema_Wifi_Credential_Config *cconfs = NULL;
    struct schema_Wifi_VIF_Config_flags *changed = NULL;
    int num_cconfs = 10;
    bool ret;
    vconf = malloc(sizeof(struct schema_Wifi_VIF_Config));
    rconf = malloc(sizeof(struct schema_Wifi_Radio_Config));
    changed = malloc(sizeof(struct schema_Wifi_VIF_Config_flags));
    cconfs = malloc(sizeof(struct schema_Wifi_Credential_Config));

    if (!vconf) {
        TEST_ASSERT_FALSE_MESSAGE(true, "Failed, to do malloc for vconf");
    }
    if (!rconf) {
        TEST_ASSERT_FALSE_MESSAGE(true, "Failed, to do malloc for rconf");
    }
    if (!changed) {
        TEST_ASSERT_FALSE_MESSAGE(true, "Failed, to do malloc for changed");
    }
    if (!cconfs) {
        TEST_ASSERT_FALSE_MESSAGE(true, "Failed, to do malloc for cconf");
    }

    fill_vif_config(vconf, rconf, changed);/*To fill the fake data */
    memset (cconfs, 0, sizeof(struct schema_Wifi_Credential_Config));
    ret = target_vif_config_set2(vconf, rconf, cconfs, changed, num_cconfs);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "failed, to set vif configurations");

    /*teardown*/
    if(vconf)
        free(vconf);
    if(rconf)
        free(rconf);
    if(changed)
        free(changed);
    if(cconfs)
        free(cconfs);
}//END OF FUNCTION

/*! \brief
 * Function to fill @rconf with fake data of same type
 * and returns true/false.
 * @param[out] pass, if @rconf filled with correct data, fail otherwise
 */
bool fill_Radio_Config (struct schema_Wifi_Radio_Config *rconf)
{
    if (!rconf) return false;
    STRSCPY(rconf->if_name, "wifi0");
    rconf->channel_exists = true;
    rconf->channel = 1;
    STRSCPY(rconf->freq_band, "2.4G");
    rconf->hw_mode_exists= true;
    STRSCPY(rconf->hw_mode, "11n");
    STRSCPY(rconf->ht_mode, "HT40");
    return true;
}//END OF FUNCTION

/*! \brief
 * Function to changed the members presents in @changed variable and returns true/false
 * @param[out] pass, if able to fill the changes, fail otherwise
 */
bool fill_Radio_Config_flags(struct schema_Wifi_Radio_Config_flags *changed)
{
    if (!changed) return false;
    changed->ht_mode = true;
    changed->channel = false;
    return true;
}//END OF FUNCTION

/*! \brief
 * Function to test target_radio_config_set2, which sets radio config
 * @param[out] pass, if target_radio_config_set2 returns true, fail otherwise
 */
void test_target_radio_config_set2()
{
    /*setup */
    struct schema_Wifi_Radio_Config *rconf = NULL;
    struct schema_Wifi_Radio_Config_flags *changed = NULL;
    bool ret;

    rconf = malloc(sizeof(struct schema_Wifi_Radio_Config));
    if (!rconf) {
        TEST_ASSERT_FALSE_MESSAGE(true, "Failed, to do malloc");
    }

    ret = fill_Radio_Config(rconf); /*Function to get radio_config*/
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, to fill radio_config info");
    changed = malloc(sizeof( struct schema_Wifi_Radio_Config_flags));
    if (!changed) {
        TEST_ASSERT_FALSE_MESSAGE(true, "Failed, to do malloc");
    }
    memset(changed,0, sizeof(struct schema_Wifi_Radio_Config_flags));
    ret = fill_Radio_Config_flags(changed);/*Function to get radio_config_flags*/
    TEST_ASSERT_FALSE_MESSAGE(!ret, "Failed, to fill radio_config_flags info");

    ret = target_radio_config_set2(rconf, changed);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "failed, to set radio configurations");
    /*teardown*/
    if(rconf)
        free(rconf);
    if(changed)
        free(changed);
}//END OF FUNCTION

/*! \brief
 * Function to test target_stats_device_fanrpm_get, which read cuurent_rpm file
 * and gets rpm value and write into @fan_rpm
 * @param[out] pass, if target_stats_device_fanrpm_get, returns true, fail otherwise
 */
void test_target_stats_device_fanrpm_get()
{
#ifdef TARGET_CAESAR
    /*setup */
    uint32_t fan_rpm;
    bool ret;
    char *cmd = "/sys/class/hwmon/hwmon0/current_rpm";

    if (access(cmd, F_OK) ) {
        TEST_ASSERT_FALSE_MESSAGE(true, "Failed, current_rpm file doesn't exist");
    }
    ret = target_stats_device_fanrpm_get(&fan_rpm);
    TEST_ASSERT_FALSE_MESSAGE(!ret, "failed, to get thermal management");
#endif

}//END OF FUNCTION

/*! \brief
 * Function to test target_radio_config_init2, which shows example of integration
 * of third party configuration entity rather than ovsdb.
 *@param[out] pass, if target_radio_config_init2 returns true, fail otherwise
 */
void test_target_radio_config_init2()
{
    bool ret = target_radio_config_init2();
    TEST_ASSERT_FALSE_MESSAGE((ret == false), "failed, to initialize radio_config");

}//END OF FUNCTION

/*! \brief
 * Function to test target_radio_config_need_reset, which returns true if
 * QCA_TARGET_NEED_RESET is "y"
 * @param[out] pass, if target_radio_config_need_reset returns true, fail otherwise
 */
void test_target_radio_config_need_reset()
{
    /*setup*/
    bool ret;
    char *str = NULL;
    str = getenv("QCA_TARGET_CONFIG_NEED_RESET");
    ret  = target_radio_config_need_reset();
    if (!str && ret) {
        TEST_ASSERT_FALSE_MESSAGE(1, "failed, to reset radio_config1");
    }else if (str){
        if (!ret) {
            TEST_ASSERT_FALSE_MESSAGE(1, "failed, to reset radio_config2");
        }
    }
}//END OF FUNCTION
