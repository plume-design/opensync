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

#ifndef TARGET_H_INCLUDED
#define TARGET_H_INCLUDED

#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ev.h>

#include "os.h"
#include "os_types.h"
#include "schema.h"
#include "os_backtrace.h"

#define TARGET_BUFF_SZ              256

/* Target init options */
typedef enum {
    TARGET_INIT_COMMON              =  0,
    TARGET_INIT_MGR_DM              =  1,
    TARGET_INIT_MGR_CM              =  2,
    TARGET_INIT_MGR_WM              =  3,
    TARGET_INIT_MGR_SM              =  4,
    TARGET_INIT_MGR_NM              =  5,
    TARGET_INIT_MGR_BM              =  6,
    TARGET_INIT_MGR_FM              =  7,
    TARGET_INIT_MGR_LM              =  8,
    TARGET_INIT_MGR_LEDM            =  9,
    TARGET_INIT_MGR_OM              = 10,
    TARGET_INIT_MGR_BLEM            = 11,
    TARGET_INIT_MGR_QM              = 12,
    TARGET_INIT_MGR_PM              = 13,
} target_init_opt_t;

bool target_ready(struct ev_loop *loop);
bool target_init(target_init_opt_t opt, struct ev_loop *loop);
bool target_close(target_init_opt_t opt, struct ev_loop *loop);

/******************************************************************************
 *  MANAGERS definitions
 *****************************************************************************/
typedef struct
{
  char                             *name;           /* process name */
  pid_t                             pid;            /* process PID  */
  bool                              started;        /* process started? */
  int                               ordinal;        /* used only to relate to wtimer */
  bool                              needs_plan_b;   /* Execute restart plan B */
} target_managers_config_t;

extern target_managers_config_t     target_managers_config[];
extern int                          target_managers_num;

/******************************************************************************
 *  INTERFACE definitions
 *****************************************************************************/
bool target_is_radio_interface_ready(char *phy_name);
bool target_is_interface_ready(char *if_name);
const char *target_wan_interface_name();

/******************************************************************************
 *  Ethernet clients definitions
 *****************************************************************************/

const char **target_ethclient_iflist_get();
const char **target_ethclient_brlist_get();

/******************************************************************************
 *  ENTITY definitions
 *****************************************************************************/
bool target_id_get(void *buff, size_t buffsz);
bool target_serial_get(void *buff, size_t buffsz);
bool target_sku_get(void *buff, size_t buffsz);
bool target_model_get(void *buff, size_t buffsz);
bool target_sw_version_get(void *buff, size_t buffsz);
bool target_hw_revision_get(void *buff, size_t buffsz);
bool target_platform_version_get(void *buff, size_t buffsz);
const char *app_build_ver_get();
const char *app_build_profile_get();

/******************************************************************************
 *  MAP definitions
 *****************************************************************************/
bool target_map_init(void);
bool target_map_close(void);
bool target_map_insert(char *if_name, char *map_name);

bool target_map_ifname_init(void);
char *target_map_ifname(char *ifname);
bool target_map_ifname_exists(const char *ifname);
char *target_unmap_ifname(char *ifname);
bool target_unmap_ifname_exists(const char *ifname);

/******************************************************************************
 *  UPGRADE definitions
 *****************************************************************************/

bool   target_upg_download_required(char *url);
char  *target_upg_command();
char  *target_upg_command_full();
char **target_upg_command_args(char *password);
double target_upg_free_space_err();
double target_upg_free_space_warn();

/******************************************************************************
 *  BLE definitions
 *****************************************************************************/

bool target_ble_preinit(struct ev_loop *loop);
bool target_ble_prerun(struct ev_loop *loop);
bool target_ble_broadcast_start(struct schema_AW_Bluetooth_Config *config);
bool target_ble_broadcast_stop(void);

/******************************************************************************
 *  OM definitions
 *****************************************************************************/

bool target_om_add_flow( char *token, struct schema_Openflow_Config *ofconf );
bool target_om_del_flow( char *token, struct schema_Openflow_Config *ofconf );


/******************************************************************************
 *  BM and BSAL definitions
 *****************************************************************************/

bool target_bsal_client_disconnect( char *interface, char *disc_type,
                                    char *mac_str,   uint8_t reason );


/******************************************************************************
 *  TLS definitions
 *****************************************************************************/

const char *target_tls_cacert_filename(void);
const char *target_tls_mycert_filename(void);
const char *target_tls_privkey_filename(void);


/******************************************************************************
 *  Common definitions
 *****************************************************************************/

bool target_log_open(char *name, int flags);
bool target_log_pull(const char *upload_location, const char *upload_token);
const char *target_log_state_file(void);
const char *target_log_trigger_dir(void);

const char *target_led_device_dir(void);

const char* target_scripts_dir(void);
const char* target_tools_dir(void);
const char* target_bin_dir(void);
const char *target_persistent_storage_dir(void);
btrace_type target_get_btrace_type(void);
void target_managers_restart(void);

#if !defined(TARGET_H)
#warning Undefined TARGET_H
#else
// moved to bottom so TARGET_H can refer to above typedefs
#include TARGET_H
#endif

#if !defined(TARGET_SERIAL_SZ)
#define TARGET_SERIAL_SZ            OS_MACSTR_PLAIN_SZ
#endif

#if !defined(TARGET_ID_SZ)
#define TARGET_ID_SZ                OS_MACSTR_PLAIN_SZ
#endif

#endif /* TARGET_H_INCLUDED */
