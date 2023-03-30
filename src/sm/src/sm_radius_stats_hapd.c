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

#define _GNU_SOURCE /* needed for the hapd_cli macro function */

#include <ev.h>
#include <stdbool.h>
#include <net/if.h>
#include <linux/un.h>
#include <linux/limits.h>

#include "sm.h"
#include "ovsdb.h"
#include "ovsdb_table.h"
#include "opensync-ctrl.h"
#include "opensync-hapd.h"

/* TODO : Clarify how this can be exposed from hapd.c, perhaps just move macros to the header? We probably don't want more heap allocations. */
#define E(...) strexa(__VA_ARGS__)
#if CONFIG_HOSTAP_TIMEOUT_T_SWITCH
#define CMD_TIMEOUT(...) "timeout", "-s", "KILL", "-t", "3", ## __VA_ARGS__
#else
#define CMD_TIMEOUT(...) "timeout", "-s", "KILL", "3", ## __VA_ARGS__
#endif
#define HAPD_CLI(hapd, ...) E(CMD_TIMEOUT("hostapd_cli", "-p", hapd->ctrl.sockdir, "-i", hapd->ctrl.bss, ## __VA_ARGS__))

inline static unsigned int
sm_hapd_exec_hapd_cli_and_parse(struct hapd *hapd, const char *vif_name);

static ovsdb_table_t table_Wifi_VIF_State;
static ovsdb_table_t table_Wifi_Radio_State;

static struct hapd *sm_radius_alloc_new_hapd(const char *vif_name)
{
    json_t *where;
    struct schema_Wifi_Radio_State rstate;
    struct schema_Wifi_VIF_State vstate;
    static bool init = false;

    if (!init)
    {
        OVSDB_TABLE_INIT_NO_KEY(Wifi_VIF_State);
        OVSDB_TABLE_INIT_NO_KEY(Wifi_Radio_State);
        init = true;
    }

    if (WARN_ON(!(where = ovsdb_tran_cond(OCLM_STR, SCHEMA_COLUMN(Wifi_VIF_State, if_name), OFUNC_EQ, vif_name))))
    {
        LOGE("%s : Error calling ovsdb_where_simple for %s & Wifi_VIF_State table.", __func__, vif_name);
        return NULL;
    }
    if (WARN_ON(!ovsdb_table_select_one_where(&table_Wifi_VIF_State, where, &vstate)))
    {
        LOGE("%s : Failed to find %s in Wifi_VIF_State table.", __func__, vif_name);
        return NULL;
    }
    if (WARN_ON(!(where = ovsdb_tran_cond(OCLM_UUID,
        SCHEMA_COLUMN(Wifi_Radio_State, vif_states), OFUNC_INC, vstate._uuid.uuid))))
    {
        LOGE("%s : Error calling ovsdb_tran_cond for %s vstate and Wifi_Radio_State", __func__, vif_name);
        return NULL;
    }
    if (WARN_ON(!ovsdb_table_select_one_where(&table_Wifi_Radio_State, where, &rstate)))
    {
        LOGE("%s : Failed to find %s related entry in Wifi_Radio_State", __func__, vif_name);
        return NULL;
    }

    return hapd_new(rstate.if_name, vif_name);
}

unsigned int
sm_radius_collect_data (const char *vif_name)
{
    struct hapd *hapd = NULL;

    if (!(hapd = hapd_lookup(vif_name)) &&
        !(hapd = sm_radius_alloc_new_hapd(vif_name)))
    {
        LOGD("%s : Failed to obtain an hapd object related to %s.", __func__, vif_name);
        return 0;
    }

    return sm_hapd_exec_hapd_cli_and_parse(hapd, vif_name);
}

inline static unsigned int
sm_hapd_exec_hapd_cli_and_parse(struct hapd *hapd, const char *vif_name)
{
    int port, index;
    const char *ip;
    unsigned int count = 0;

    const char *mib = HAPD_CLI(hapd, "mib");
    const char *token = mib;

    if (!token)
        LOGD("%s : The return from HAPD_CLI(hapd, \"mib\") is empty.", __func__);

    while (token && (token = strstr(token, "radiusAuthServerIndex")))
    {
        sm_radius_stats_t *stats = sm_radius_new_stats_object(vif_name);

        index = atoi(ini_geta(token, "radiusAuthServerIndex"));
        ip = ini_geta(token, "radiusAuthServerAddress");
        port = atoi(ini_geta(token, "radiusAuthClientServerPortNumber"));

        STRSCPY_WARN(stats->radiusAuthServerAddress, ip);

        stats->radiusAuthServerIndex = index;
        stats->radiusAuthClientServerPortNumber = port;

        stats->radiusAuthClientRoundTripTime = atoi(ini_geta(token, "radiusAuthClientRoundTripTime"));
        stats->radiusAuthClientAccessRequests = atoi(ini_geta(token, "radiusAuthClientAccessRequests"));
        stats->radiusAuthClientAccessRetransmissions = atoi(ini_geta(token, "radiusAuthClientAccessRetransmissions"));
        stats->radiusAuthClientAccessAccepts = atoi(ini_geta(token, "radiusAuthClientAccessAccepts"));
        stats->radiusAuthClientAccessRejects = atoi(ini_geta(token, "radiusAuthClientAccessRejects"));
        stats->radiusAuthClientAccessChallenges = atoi(ini_geta(token, "radiusAuthClientAccessChallenges"));
        stats->radiusAuthClientMalformedAccessResponses = atoi(ini_geta(token, "radiusAuthClientMalformedAccessResponses"));
        stats->radiusAuthClientBadAuthenticators = atoi(ini_geta(token, "radiusAuthClientBadAuthenticators"));
        stats->radiusAuthClientPendingRequests = atoi(ini_geta(token, "radiusAuthClientPendingRequests"));
        stats->radiusAuthClientTimeouts = atoi(ini_geta(token, "radiusAuthClientTimeouts"));
        stats->radiusAuthClientUnknownTypes = atoi(ini_geta(token, "radiusAuthClientUnknownTypes"));
        stats->radiusAuthClientPacketsDropped = atoi(ini_geta(token, "radiusAuthClientPacketsDropped"));

        sm_radius_add_stats_object(stats);

        count++;
        token++;
    }

    return count;
}
