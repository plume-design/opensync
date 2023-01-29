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


/***
 * Header File containing all the unit test prototype of unit test api's
 * It will also have setup and tear down api's
 ***/

#include "target.h"
#include "osp_unit.h"
#include "unity.h"
#include <stdio.h>
#include "ovsdb_table.h"
#include "sm.h"
#include "ds.h"
#include "nm2.h"
#include "blem.h"
#include "cm2.h"
#include "dpp_types.h"
#define IF_NAME_SIZE 32
#define BUFFER_SIZE 1024

enum data_type {
    VALID,
    INVALID,
};

bool get_ovsdb_table_info(ovsdb_table_t *table);
bool get_Wifi_Master_State_Info(ovsdb_table_t *master);
ds_tree_t *sm_radios_get();
bool test_stats_capacity_get();

void test_target_stats_device_get();
void test_target_device_info();
void test_target_map_init();
void test_target_map_insert();
void test_target_map_ifname() ;
void test_target_unmap_ifname(char *map_name);
void test_target_map_ifname_exists();
void test_target_unmap_ifname_exists(char *map_name);
void test_target_map_close();
void test_target_tools_dir();
void test_target_bin_dir();
void test_target_scripts_dir();
void test_target_persistent_storage_dir();
void test_target_init();
void test_target_is_radio_interface_ready();
void test_target_is_interface_ready();
void test_target_ethclient_iflist_get();
void test_target_stats_device_temp_get();
void test_target_stats_capacity_get();
void test_target_radio_config_need_reset();
void test_target_radio_config_init2();
void test_target_stats_device_fanrpm_get();
void test_target_radio_config_set2();
void test_target_vif_config_set2();
void test_target_radio_state_get();
void test_target_vif_state_get();
void test_target_radio_init();
void test_target_device_connectivity_check();
void test_target_device_capabilities_get();
void test_target_device_restart_managers();
void test_target_device_wdt_ping();
void test_target_device_execute();

