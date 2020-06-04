/*
* Copyright (c) 2020, Charter, Inc.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* 3. Neither the name of the copyright holder nor the names of its contributors
*    may be used to endorse or promote products derived from this software
*    without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef PWM_INCLUDE_FIREWALL
#define PWM_INCLUDE_FIREWALL

#include <stdbool.h>

#define PWM_NETFILER_GRE_RX "pw_gre_rx"
#define PWM_NETFILER_GRE_TX "pw_gre_tx"

bool pwm_add_fw_rules(const char *endpoint);
bool pwm_del_fw_rules(void);

/******************************************************************************/
/* Radius Server                                                              */
/******************************************************************************/
#define PWM_NETFILER_RS_TX "pw_rs_tx"

bool pwm_add_rs_fw_rules( const char *rs_ip_address, const char *rs_ip_port );
bool pwm_del_rs_fw_rules( void );


/******************************************************************************/
/* DHCP packet divert to queue rule                                           */
/******************************************************************************/
bool pwm_add_pktroute_to_q_rule(const char *name, const char *intf);
bool pwm_del_pktroute_to_q_rule( const char *name );


#endif

