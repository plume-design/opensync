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

#ifndef QOSM_QOS_H_INCLUDED
#define QOSM_QOS_H_INCLUDED

#include <stdbool.h>

#include "ovsdb.h"

/**
 * Apply QoS configuration to the system for an interface.
 *
 * @param[in]   uuid                    OVSDB uuid of the interface
 * @param[out]  qos_qdisc_cfg_exists    Will be set to true if there was any actual QoS
 *                                      configuration for this interface and the underlying
 *                                      QoS backend is qdisc-based.
 */
bool qosm_qos_config_apply(const ovs_uuid_t *uuid, bool *qos_qdisc_cfg_exists);

/**
 * Apply Adaptive QoS.
 */
bool qosm_adaptive_qos_apply();

/**
 * Stop any existing Adaptive QoS.
 */
bool qosm_adaptive_qos_stop();

#endif /* QOSM_QOS_H_INCLUDED */
