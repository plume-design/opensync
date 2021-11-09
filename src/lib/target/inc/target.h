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

#include "target_bsal.h"

/**
 * @file target.h
 * @brief Base target API header
 *
 * At the end of this header the platform specific header TARGET_H is also included,
 * which can declare additional APIs and will usually include also the target_common.h
 */

/// @defgroup LIB_TARGET OpenSync Target Library
/// Target API.
/// @{

#define TARGET_BUFF_SZ              256

/// @defgroup LIB_TARGET_INIT Initialization and Cleanup
/// Target library initialization and cleanup.
/// @{

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
/*  TARGET_INIT_MGR_LEDM            =  9, obsoleted */
    TARGET_INIT_MGR_OM              = 10,
    TARGET_INIT_MGR_BLEM            = 11,
    TARGET_INIT_MGR_QM              = 12,
    TARGET_INIT_MGR_PM              = 13,
    TARGET_INIT_MGR_FSM             = 14,
    TARGET_INIT_MGR_TM              = 15,
    TARGET_INIT_MGR_HELLO_WORLD     = 16,
    TARGET_INIT_MGR_FCM             = 17,
    TARGET_INIT_MGR_PPM             = 18,
    TARGET_INIT_MGR_NFM             = 19,
    TARGET_INIT_MGR_MAPTM           = 20,
    TARGET_INIT_MGR_LTEM            = 21
} target_init_opt_t;

/**
 * @brief Wait for platform readiness
 *
 * The purpose of target_ready API is to allow the subsystem to fully initialize
 * and is ready for processing. The API needs to be blocking. Successful return
 * starts managers and spawns monitoring.
 *
 * Example actions that target may want to perform:
 * - Check if all default network interfaces are setup.
 * - Check if date and time are set correctly.
 * - Cache platform data (model, id, version, etc.) for further use.
 *
 * If the target readiness is assured before starting OpenSync
 * (for example by systemd dependencies) the implementation of this function
 * may just return true.
 *
 * @param loop main loop handle
 * @return true on success
 */
bool target_ready(struct ev_loop *loop);

/**
 * @brief Perform platform specific initialization
 *
 * The purpose of target_init is to allow initialization of vendor specific parameters:
 * IOCTL or netlink sockets, linked lists, etc. This API is called from every manager
 * prior to executing the functional APIs. If given vendor specific initialization is
 * not needed for one of the managers (for instance there is no need to have Wifi HAL
 * initialized for DM) it can be a no-op for that manager. Note: each manager is
 * a separate Linux process.
 *
 * @param opt specifies from which manager this API is called
 * @param loop main loop handle
 * @return true on success
 */
bool target_init(target_init_opt_t opt, struct ev_loop *loop);

/**
 * @brief Perform platform specific cleanups on exit
 *
 * Once the manager exits its processing loop, the target_close API gets called.
 * Example implementation can call cleanup actions of HAL library that was
 * initialized inside target_init(). Note: this function is called from
 * every manager, so cleanup action must correspond to init action for specific
 * manager.
 *
 * @param opt specifies from which manager this API is called
 * @param loop main loop handle
 * @return true on success
 */
bool target_close(target_init_opt_t opt, struct ev_loop *loop);

/// @} LIB_TARGET_INIT

/// @defgroup LIB_TARGET_MANAGERS Control of Managers
/// Definitions and API related to control of managers.
/// @{

/******************************************************************************
 *  MANAGERS definitions
 *****************************************************************************/
typedef struct
{
  char                             *name;           /* process name */
  pid_t                             pid;            /* process PID  */
  bool                              started;        /* process started? */
  int                               ordinal;        /* used only to relate to wtimer */
  int                               always_restart; /* always restart the process */
  int                               restart_delay;  /* delay before restart */
  bool                              needs_plan_b;   /* Execute restart plan B */
} target_managers_config_t;

/**
 * @brief List of managers to start
 *
 * This defines the subset of managers that DM can start with adding entries to
 * target_managers_config table.
 *
 * Example:
 * @code
 * target_managers_config_t target_managers_config[] =
 * {
 *     { .name = TARGET_MANAGER_PATH("wm"), .needs_plan_b = true,  },
 *     { .name = TARGET_MANAGER_PATH("nm"), .needs_plan_b = true,  },
 *     { .name = TARGET_MANAGER_PATH("cm"), .needs_plan_b = true,  },
 *     { .name = TARGET_MANAGER_PATH("lm"), .needs_plan_b = true,  },
 *     { .name = TARGET_MANAGER_PATH("sm"), .needs_plan_b = false, },
 * }
 * @endcode
 *
 * The needs_plan_b parameter is part of the monitoring recovery mechanism
 * where DM restarts ALL managers (true) through target_managers_restart or
 * just particular managers (false).
 */
extern target_managers_config_t     target_managers_config[];
extern int                          target_managers_num;

/// @} LIB_TARGET_MANAGERS

/// @defgroup LIB_TARGET_INTERFACES Interface API
/// Definitions and API related to control of interfaces.
/// @{

/******************************************************************************
 *  INTERFACE definitions
 *****************************************************************************/
/**
 * @brief Check if radio interface is ready
 * @param phy_name radio interface name
 * @return true on success
 */
bool target_is_radio_interface_ready(char *phy_name);

/**
 * @brief Check if interface is ready
 * @param if_name interface name
 * @return true on success
 */
bool target_is_interface_ready(char *if_name);

/**
 * @brief Get wan interface name
 * @return wan interface name
 */
const char *target_wan_interface_name();

/// @} LIB_TARGET_INTERFACES

/// @defgroup LIB_TARGET_ETHCLIENT Ethernet Clients API
/// Definitions and API related to control of ethernet clients.
/// @{

/******************************************************************************
 *  Ethernet clients definitions
 *****************************************************************************/

const char **target_ethclient_iflist_get();
const char **target_ethclient_brlist_get();

/// @} LIB_TARGET_ETHCLIENT


/// @defgroup LIB_TARGET_MAP Interface Mapping API
/// API for mapping of interface names that the cloud uses to actual interface
/// names.
///
/// This API is deprecated. The actual interface names are defined in a device
/// profile, configured on the cloud.
/// @{

/******************************************************************************
 *  MAP definitions
 *****************************************************************************/
bool target_map_init(void);
bool target_map_close(void);
bool target_map_insert(char *if_name, char *map_name);

char *target_map_ifname(char *ifname);
bool target_map_ifname_exists(const char *ifname);
char *target_unmap_ifname(char *ifname);
bool target_unmap_ifname_exists(const char *ifname);

/// @} LIB_TARGET_MAP

/******************************************************************************
 *  OM definitions
 *****************************************************************************/
/// @cond INTERNAL

typedef enum {
    TARGET_OM_PRE_ADD       = 0,
    TARGET_OM_POST_ADD,
    TARGET_OM_PRE_DEL,
    TARGET_OM_POST_DEL
} target_om_hook_t;

bool target_om_hook(target_om_hook_t hook, const char *openflow_rule);
/// @endcond INTERNAL

/// @defgroup LIB_TARGET_TLS Certificate Management
/// Definitions and API related to control of certificates.
/// @{

/******************************************************************************
 *  TLS definitions
 *****************************************************************************/

/**
 * @brief Get the TLS CA certificate filename
 * @return CA filename
 */
const char *target_tls_cacert_filename(void);

/**
 * @brief Get the TLS certificate filename
 * @return certificate filename
 */
const char *target_tls_mycert_filename(void);

/**
 * @brief Get the TLS private key filename
 * @return private key filename
 */
const char *target_tls_privkey_filename(void);

/// @} LIB_TARGET_TLS

/// @defgroup LIB_TARGET_OTHER Miscellaneous Overrides
/// API used for various overrides.
/// @{

/******************************************************************************
 *  Common definitions
 *****************************************************************************/

/**
 * @brief Enable logging
 *
 * By default calls log_open. Can be overridden with platform specific functionality.
 *
 * @param name name of the binary (manager name)
 * @param flags see flags for log_open()
 * @return true on success
 */
bool target_log_open(char *name, int flags);

/**
 * @brief Collect logs
 *
 * This function will be called upon cloud request. Main job of it is to
 * collect logs (/var/log/messages, dmesg, ...) and system information
 * (interface configuration, running processes, disk usage, ...) and upload
 * them as a single *.tgz file to location specified by "upload_location"
 * parameter. Note that *.tgz file must be named as a given "upload_token" with
 * .tgz suffix (ie: upload_token.tgz).
 *
 * @note Single *.tgz file must be smaller than 10MB.
 * @note Use of Log Manager is optional
 *
 * @param upload_location URL for upload
 * @param upload_token filename to upload
 * @return true on success
 */
bool target_log_pull(const char *upload_location, const char *upload_token);

/**
 * @brief Collect logs (using specified method).
 *
 * An extended variant of the basic target_log_pull() function.
 *
 * @param upload_location      URL
 * @param upload_token         filename to upload
 * @param upload_method        method/procedure required to upload a logpull
 *                             with the specified type of URL.
 * @return true on success
 */
bool target_log_pull_ext(const char *upload_location, const char *upload_token, const char *upload_method);

/**
 * @brief Get the directory for scripts
 * @return scripts directory
 */
const char* target_scripts_dir(void);

/**
 * @brief Get the directory for tools
 *
 * The needed tools inside tools_dir are defined by the cloud controller.
 * For example, the speed test binary is a tool that can be used by the cloud
 * controller.
 * The default implementation can be used by defining CONFIG_TARGET_PATH_TOOLS.
 *
 * @return tools directory
 */
const char* target_tools_dir(void);

/**
 * @brief Get the directory for binaries
 * @return binaries directory
 */
const char* target_bin_dir(void);

/**
 * @brief Get a persistent storage mount point
 * @return persistent storage directory
 */
const char *target_persistent_storage_dir(void);

/// @cond INTERNAL
btrace_type target_get_btrace_type(void);
/// @endcond INTERNAL

/**
 * @brief Restart all managers
 *
 * This function restarts all specified managers gracefully. Exact
 * implementation depends on the device and is the responsibility of the
 * vendor. An example might be to call an init.d restart script that performs
 * something similar to stopping and then starting the managers.  The script
 * must ensure the system is properly un-initialized and then initialized
 * again, before DM is started again. This includes correct operation and state
 * of ovsdb-server and contents of all tables, removing wireless interfaces,
 * stopping wpa_supplicant, etc.
 *
 * @return true on success
 */
void target_managers_restart(void);

/// @} LIB_TARGET_OTHER


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

/// @} LIB_TARGET

#endif /* TARGET_H_INCLUDED */
