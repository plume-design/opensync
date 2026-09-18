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

#ifndef OX_DM_FIRMWARE_H_INCLUDED
#define OX_DM_FIRMWARE_H_INCLUDED

#include <stdbool.h>
#include <ox_types.h>

/**
 * TR181 paths exposed by this module (backed by AWLAN_Node OVSDB columns):
 *
 *   Device.DeviceInfo.FirmwareImageNumberOfEntries             (R)  = 2 (constant)
 *   Device.DeviceInfo.ActiveFirmwareImage                     (R)  = FirmwareImage.1 (constant)
 *   Device.DeviceInfo.BootFirmwareImage                       (R)  = FirmwareImage.1 (constant)
 *   Device.DeviceInfo.FirmwareImage.{i}.Name                  (R)  inst1←firmware_version, inst2=""
 *   Device.DeviceInfo.FirmwareImage.{i}.Status                (R)  inst1="Active", inst2←upgrade_status
 *   Device.DeviceInfo.FirmwareImage.{i}.Alias                 (R)  inst1="active", inst2="inactive"
 *   Device.DeviceInfo.FirmwareImage.{i}.X_OPENSYNC_DecryptionPassword (RW) inst2→firmware_pass
 *   Device.DeviceInfo.FirmwareImage.{i}.Download()            (method) inst2→firmware_url+upgrade_timer
 *   Device.DeviceInfo.FirmwareImage.{i}.Activate()            (method) inst2→upgrade_timer (flash + reboot)
 *
 * Usage — call in this order:
 *   1. os_tr181_register_object("Device.DeviceInfo.") — caller's responsibility
 *   2. ox_dm_firmware_register()   — before os_tr181_publish_objects()
 *   3. os_tr181_publish_objects()  — called by oxm_main.c
 *   4. ox_dm_firmware_post_publish() — after os_tr181_publish_objects()
 */

/**
 * Phase 1: Register schema with the TR181 library.
 * Must be called after ox_router_init() and before os_tr181_publish_objects().
 */
bool ox_dm_firmware_register(ox_router_t *router);

/**
 * Phase 2: Create the two fixed FirmwareImage instances and start OVSDB monitoring.
 * Must be called after os_tr181_publish_objects().
 */
bool ox_dm_firmware_post_publish(ox_router_t *router);

#endif /* OX_DM_FIRMWARE_H_INCLUDED */
