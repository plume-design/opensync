/*
 * Copyright (c) 2021, Sagemcom.
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

#ifndef PWM_OVSDB_H_INCLUDE
#define PWM_OVSDB_H_INCLUDE

#include <stdbool.h>
#include "const.h"
#include "ovsdb_table.h"

typedef enum
{
    GRE_tunnel_DOWN,
    GRE_tunnel_DOWN_PING,
    GRE_tunnel_DOWN_FQDN,
    GRE_tunnel_DOWN_CREATION_ERROR,
    GRE_tunnel_UP,
} GRE_tunnel_status_type;

typedef enum
{
    PWM_STATE_TABLE_KEEP_ALIVE,
    PWM_STATE_TABLE_TUNNEL_IFNAME,
    PWM_STATE_TABLE_VIF_IFNAMES,
    PWM_STATE_TABLE_VLAN_ID,
    PWM_STATE_TABLE_ENABLED,
    PWM_STATE_TABLE_GRE_ENDPOINT,
    PWM_STATE_TABLE_GRE_TUNNEL_STATUS_MSG,
} PWM_State_Table_Member;

extern char *g_tunnel_gre_status_msg;
extern char *g_gre_endpoint;

bool pwm_ovsdb_init(void);
bool pwm_ovsdb_is_enabled(void);
bool pwm_ovsdb_reset(void);
bool pwm_ovsdb_check_remote_endpoint_alive(const char *remote_endpoint);
char* pwm_ovsdb_update_gre_tunnel_status(GRE_tunnel_status_type status);
bool pwm_ovsdb_update_state_table(PWM_State_Table_Member pwm_state_member, bool action_set, void* ptr_value);

#endif /* PWM_OVSDB_H_INCLUDE */
