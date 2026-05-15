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

#ifndef PM_NODE_FH_CONFIG_H_INCLUDED
#define PM_NODE_FH_CONFIG_H_INCLUDED

#include <stddef.h>
#include <stdbool.h>

/*
 * Controller → device:
 *   The controller takes all necessary actions, updating Wifi_VIF_Config and
 *   Wifi_Inet_Config as required, and reports the resolved state to the device
 *   via cloud_fh_config.  The device reads this state on demand via
 *   pm_cloud_fh_config_get().
 *
 * Device → controller:
 *   The device upserts a Node_Config row using pm_node_fh_config_request().
 *   The controller detects the new row, fires GatewayFhChangeRequest, clears
 *   the entry via a KvConfig task, and reports the resolved state back to the
 *   device via cloud_fh_config.
 */

/* Node_Config identifiers for the fronthaul role change request row. */
#define PM_NODE_FH_CONFIG_MODULE  "node_fh_config"
#define PM_CLOUD_FH_CONFIG_MODULE "cloud_fh_config"
#define PM_NODE_FH_CONFIG_KEY     "role_enable"

/**
 * Request a fronthaul role enable/disable change from the controller by
 * upserting a Node_Config row:
 *   module = PM_NODE_FH_CONFIG_MODULE ("node_fh_config")
 *   key    = PM_NODE_FH_CONFIG_KEY    ("role_enable")
 *   value  = "<role>.<bool>"          (e.g. "hs.false")
 *
 * @param role    Predefined fronthaul role string: "hs", "iot", "guest",
 *                "employee", or "flex".
 * @param enable  true to request enable, false to request disable.
 * @return        true on success, false on invalid role or OVSDB failure.
 */
bool pm_node_fh_config_request(const char *role, bool enable);

/**
 * Read the resolved fronthaul role state reported by the controller via
 * cloud_fh_config.
 *
 * @param role      Caller-provided buffer to receive the role string
 *                  (e.g. "hs", "guest").
 * @param role_size Size of the role buffer in bytes.
 * @param enable    On success, set to true if the role is enabled, false if disabled.
 * @return          true if a row was found and successfully parsed, false otherwise.
 */
bool pm_cloud_fh_config_get(char *role, size_t role_size, bool *enable);

#endif /* PM_NODE_FH_CONFIG_H_INCLUDED */
