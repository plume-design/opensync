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

#ifndef OTBR_CLI_API_H_INCLUDED
#define OTBR_CLI_API_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "const.h"
#include "otbr_cli.h"
#include "otbr_cli_error.h"
#include "otbr_cli_util.h"

#include "openthread/netdiag.h"
#include "openthread/thread.h"

struct otbr_network_diagnostic_peer_tlvs_s
{
    struct otNetworkDiagTlv *tlvs;
    size_t num_tlvs;
};

struct otbr_network_diagnostic_tlvs_s
{
    struct otbr_network_diagnostic_peer_tlvs_s *peers;
    size_t num_peers;
};

/**
 * Execute a command to retrieve the current Thread device role
 *
 * @param[out] role  Location where to store the retrieved device role.
 *
 * @return true on success. Failure is logged internally.
 */
bool otbr_cli_get_role(otDeviceRole *role) NONNULL(1);

/**
 * Get a Thread Network multicast address
 *
 * @param[in] whole_network  true` to get Mesh-Local (Realm-Local) multicast address, used to address
 *                           all interfaces reachable within the same Thread network;
 *                           `false` to get Link-Local multicast address, used to address all
 *                           interfaces reachable by a single radio transmission.
 * @param[in] including_med  `true` to also address Minimal End Devices (MEDs) type of Minimal Thread Devices (MTDs),
 *                           in addition to Full Thread Devices (FTDs).
 * @param[in] including_sed  `true` to also address Sleepy End Devices (SEDs) type of Minimal End Devices (MEDs).
 *                           This ignores `including_med` value (implicitly sets it to `true`).
 *                           This address shall be used with care, as it may cause increased power consumption.
 *
 * @return multicast address string (statically allocated), or `NULL` on failure (logged internally).
 */
const char *otbr_cli_get_multicast_address(bool whole_network, bool including_med, bool including_sed);

/**
 * Send network diagnostic request to retrieve TLVs of the specified types
 *
 * @param[in]  address         The address to retrieve network diagnostic information from.
 *                             If unicast address, Diagnostic Get will be sent.
 *                             If multicast address, Diagnostic Query will be sent
 *                             (@see otbr_cli_get_multicast_address).
 * @param[in]  diag_types      An array of network diagnostic types to retrieve.
 * @param[in]  num_diag_types  The number of diagnostic types in the `diag_types` array.
 * @param[in]  timeout         The maximum time (seconds) to wait for the diagnostic information.
 *                             Using the default value (-1) is usually too short, especially for
 *                             multicast addresses.
 * @param[out] tlvs            Pointer on the structure to store the retrieved TLVs.
 *                             Call @ref otbr_cli_get_network_diagnostic_tlvs_free after usage!
 *                             Note that the returned structure may contain less TLVs than requested.
 *
 * @return true on success. Failure is logged internally.
 */
bool otbr_cli_get_network_diagnostic(
        const char *address,
        const otNetworkDiagTlvType *diag_types,
        unsigned int num_diag_types,
        float timeout,
        struct otbr_network_diagnostic_tlvs_s *tlvs) NONNULL(1, 2, 5);

/**
 * Free the memory allocated for the network diagnostic TLVs by @ref otbr_cli_get_network_diagnostic
 *
 * @param[in] tlvs  Pointer on the structure to free.
 */
void otbr_cli_get_network_diagnostic_tlvs_free(struct otbr_network_diagnostic_tlvs_s *tlvs) NONNULL(1);

/**
 * Retrieve the Thread Leader Data
 *
 * @param[in] leader_data Pointer on the structure to store the retrieved Leader Data.
 *
 * @return true on success. Failure is logged internally.
 */
bool otbr_cli_get_leader_data(otLeaderData *leader_data) NONNULL(1);

#endif /* OTBR_CLI_API_H_INCLUDED */
