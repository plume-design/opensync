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

typedef struct {
    struct schema_Wifi_Radio_Config rconf;
    ds_dlist_t                      vifs_cfg;
    ds_dlist_node_t                 dsl_node;
} target_radio_cfg_t;

typedef struct {
    struct schema_Wifi_VIF_Config   vconf;
    ds_dlist_node_t                 dsl_node;
} target_vif_cfg_t;

typedef struct {
    struct schema_Wifi_Inet_Config  iconfig;
    ds_dlist_node_t                 dsl_node;
} target_inet_config_init_t;

typedef struct {
    struct schema_Wifi_Inet_State   istate;
    ds_dlist_node_t                 dsl_node;
} target_inet_state_init_t;

/******************************************************************************
 *  RADIO definitions
 *****************************************************************************/
bool target_radio_config_init(ds_dlist_t *init_cfg);
bool target_radio_config_set (char *ifname, struct schema_Wifi_Radio_Config *rconf);
bool target_radio_state_get(char *ifname, struct schema_Wifi_Radio_State *rstate);
typedef void target_radio_state_cb_t(struct schema_Wifi_Radio_State *rstate, schema_filter_t *filter);
bool target_radio_state_register(char *ifname, target_radio_state_cb_t *radio_state_cb);
bool target_radio_config_register(char *ifname, void *radio_config_cb);

/******************************************************************************
 *  VIF definitions
 *****************************************************************************/
bool target_vif_config_set (char *ifname, struct schema_Wifi_VIF_Config *vconf);
bool target_vif_state_get(char *ifname, struct schema_Wifi_VIF_State *vstate);
typedef void target_vif_state_cb_t(struct schema_Wifi_VIF_State *rstate, schema_filter_t *filter);
bool target_vif_state_register(char *ifname, target_vif_state_cb_t *vstate_cb);
bool target_vif_config_register(char *ifname, void *vconfig_cb);

/******************************************************************************
 *  DHCP definitions
 *****************************************************************************/
bool target_dhcp_leased_ip_get(struct schema_DHCP_leased_IP *dlip);
bool target_dhcp_leased_ip_register(void *dlip_cb);

/******************************************************************************
 *  CLIENTS definitions
 *****************************************************************************/
bool target_clients_register(char *ifname, void *clients_cb);

/******************************************************************************
 *  INET definitions
 *****************************************************************************/
bool target_inet_state_init(ds_dlist_t *inets_ovs);
bool target_inet_config_init(ds_dlist_t *inets_ovs);

bool target_vif_inet_config_set(char *ifname,
        struct schema_Wifi_Inet_Config *iconf);
bool target_vif_inet_state_get(char *ifname,
        struct schema_Wifi_Inet_State *istate);

bool target_gre_inet_config_set(char *ifname, char *remote_ip,
        struct schema_Wifi_Inet_Config *iconf);
bool target_gre_inet_state_get(char *ifname,  char *remote_ip,
        struct schema_Wifi_Inet_State *istate);

bool target_vlan_inet_config_set(char *ifname,
        struct schema_Wifi_Inet_Config *iconf);
bool target_vlan_inet_state_get(char *ifname,
        struct schema_Wifi_Inet_State *istate);

bool target_eth_inet_state_get(char *ifname,
        struct schema_Wifi_Inet_State *istate);

bool target_ppp_inet_state_get(char *ifname,
        struct schema_Wifi_Inet_State *istate);

bool target_inet_state_register(char   *ifname, void *istate_cb);

/******************************************************************************
 *  STATS definitions
 *****************************************************************************/
bool target_radio_tx_stats_enable(
        radio_entry_t              *radio_cfg,
        bool                        status);

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

bool target_stats_clients_get (
        radio_entry_t              *radio_cfg,
        radio_essid_t              *essid,
        target_stats_clients_cb_t  *client_cb,
        ds_dlist_t                 *client_list,
        void                       *client_ctx);

bool target_stats_clients_convert (
        radio_entry_t              *radio_cfg,
        target_client_record_t     *client_list_new,
        target_client_record_t     *client_list_old,
        dpp_client_record_t        *client_record);

/******************************************************************************
 *  SURVEY definitions
 *****************************************************************************/
target_survey_record_t *target_survey_record_alloc();
void target_survey_record_free(target_survey_record_t *record);

typedef bool target_stats_survey_cb_t (
        ds_dlist_t                 *survey_list,
        void                       *survey_ctx,
        int                         status);

bool target_stats_survey_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        target_stats_survey_cb_t   *survey_cb,
        ds_dlist_t                 *survey_list,
        void                       *survey_ctx);

bool target_stats_survey_convert (
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type,
        target_survey_record_t     *data_new,
        target_survey_record_t     *data_old,
        dpp_survey_record_t        *survey_record);

/******************************************************************************
 *  NEIGHBOR definitions
 *****************************************************************************/
typedef bool target_scan_cb_t(
        void                       *scan_ctx,
        int                         status);

bool target_stats_scan_start (
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        int32_t                     dwell_time,
        target_scan_cb_t           *scan_cb,
        void                       *scan_ctx);

bool target_stats_scan_stop (
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type);

bool target_stats_scan_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        dpp_neighbor_report_data_t *scan_results);

/******************************************************************************
 *  DEVICE definitions
 *****************************************************************************/
bool target_stats_device_get(
        dpp_device_record_t        *device_entry);

bool target_stats_device_temp_get(
        radio_entry_t              *radio_cfg,
        dpp_device_temp_t          *device_entry);

/******************************************************************************
 *  CAPACITY definitions
 *****************************************************************************/
bool target_stats_capacity_enable(
        radio_entry_t              *radio_cfg,
        bool                        enabled);

bool target_stats_capacity_get (
        radio_entry_t              *radio_cfg,
        target_capacity_data_t     *capacity_new);

bool target_stats_capacity_convert(
        target_capacity_data_t     *capacity_new,
        target_capacity_data_t     *capacity_old,
        dpp_capacity_record_t      *capacity_entry);

/******************************************************************************
 *  SERVICE definitions
 *****************************************************************************/
bool target_device_config_register(void *awlan_cb);
bool target_device_config_set(struct schema_AWLAN_Node *awlan);
bool target_device_execute(const char* cmd);

/******************************************************************************
 *  INTERFACE NAME MAP definitions
 *****************************************************************************/
char *target_map_ifname(char *ifname);

/******************************************************************************
 *  MAC LEARNING definitions
 *****************************************************************************/
bool target_mac_learning_register(void * omac_cb);

/******************************************************************************
 *  PLATFORM SPECIFIC definitions
 *****************************************************************************/

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

#endif /* TARGET_COMMON_H_INCLUDED */
