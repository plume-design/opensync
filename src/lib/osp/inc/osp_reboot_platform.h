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

#ifndef OSP_REBOOT_PLATFORM_H_INCLUDED
#define OSP_REBOOT_PLATFORM_H_INCLUDED

#include <sys/types.h> /* ssize_t */

#include "osp_reboot.h"

/// @file
/// @brief Platform-specific reboot reason check hook

/**
 * Platform-specific reboot reason check hook.
 *
 * This is called by the generic (pstore) reboot backend when the reboot reason
 * cannot be determined from pstore - i.e. no REBOOT line was logged by any
 * process and no kernel crash dump is present. This happens on a sudden SoC
 * reset that left software no chance to record a reason, most notably a
 * hardware watchdog expiry (e.g. when the process feeding the watchdog was
 * killed).
 *
 * A platform may implement this to inspect a platform-specific source (such as
 * a bootloader reset-cause register) and classify the reset. On success it must
 * set @p type (and optionally @p reason) and return true. If it cannot
 * determine a platform-specific reason it must return false, in which case the
 * generic backend keeps its default classification (power cycle / cold boot).
 *
 * The default implementation (osp_reboot_platform_null.c) always returns false.
 * A platform layer overrides it by replacing that source file in the osp unit
 * via its override.mk (see platform/<plat>/src/lib/osp/override.mk).
 *
 * @param[out]  type       reboot type to report on success
 * @param[out]  reason     reason string buffer (may be NULL)
 * @param[in]   reason_sz  size of the @p reason buffer
 *
 * @return true if the platform-specific check completed successfully,
 *         false if the platform-specific status could not be determined.
 */
bool osp_reboot_platform_check(enum osp_reboot_type *type, char *reason, ssize_t reason_sz);

#endif /* OSP_REBOOT_PLATFORM_H_INCLUDED */
