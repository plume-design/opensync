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

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <errno.h>

#include "const.h"
#include "log.h"
#include "util.h"

#include "lnx_mcast.h"

/*
 * ===========================================================================
 *  Public API
 * ===========================================================================
 */

lnx_igmp_t *lnx_igmp_new()
{
    lnx_igmp_t *self = lnx_mcast_bridge_igmp_init();

    if (self->initialized)
        return self;

    LOGI("Initializing IGMP");

    /* Initialize defaults */
    self->version = OSN_IGMPv3;
    self->unknown_group = OSN_MCAST_UNKNOWN_FLOOD;
    self->robustness_value = 2;
    self->max_groups = 100;
    self->aging_time = 300;
    self->fast_leave_enable = true;
    self->initialized = true;

    return self;
}

bool lnx_igmp_del(lnx_igmp_t *self)
{
    return true;
}

bool lnx_igmp_snooping_set(
        lnx_igmp_t *self,
        struct osn_igmp_snooping_config *config)
{
    LOG(DEBUG, "lnx_igmp_snooping_set: Setting IGMP snooping");

    self->version = config->version;
    self->snooping_enabled = config->enabled;
    STRSCPY_WARN(self->snooping_bridge, (config->bridge != NULL) ? config->bridge : "");
    STRSCPY_WARN(self->static_mrouter, (config->static_mrouter != NULL) ? config->static_mrouter : "");
    self->unknown_group = config->unknown_group;
    self->robustness_value = (config->robustness_value != 0) ? config->robustness_value : 2;
    self->max_groups = (config->max_groups != 0) ? config->max_groups : 100;
    self->fast_leave_enable = config->fast_leave_enable;

    /* Exceptions */
    lnx_mcast_free_string_array(self->mcast_exceptions, self->mcast_exceptions_len);
    self->mcast_exceptions_len = config->mcast_exceptions_len;
    self->mcast_exceptions = config->mcast_exceptions;

    return true;
}

bool lnx_igmp_proxy_set(
        lnx_igmp_t *self,
        struct osn_igmp_proxy_config *config)
{
    (void)self;

    /* Free unused strings */
    lnx_mcast_free_string_array(config->group_exceptions, config->group_exceptions_len);
    lnx_mcast_free_string_array(config->allowed_subnets, config->allowed_subnets_len);

    return true;
}

bool lnx_igmp_querier_set(
        lnx_igmp_t *self,
        struct osn_igmp_querier_config *config)
{
    (void)self;
    (void)config;

    return true;
}

bool lnx_igmp_other_config_set(
        lnx_igmp_t *self,
        const struct osn_mcast_other_config *other_config)
{
    long aging_time = lnx_mcast_other_config_get_long(other_config, "aging_time");

    LOG(DEBUG, "lnx_igmp_other_config_set: Setting IGMP other config");

    self->aging_time = (aging_time != 0) ? aging_time : 300;

    return true;
}

bool lnx_igmp_update_iface_status(
        lnx_igmp_t *self,
        char *ifname,
        bool enable)
{
    LOG(DEBUG, "lnx_igmp_update_iface_status: Updating interface %s status to: %s", ifname, enable ? "UP" : "DOWN");
    
    if (strncmp(ifname, self->snooping_bridge, IFNAMSIZ) == 0)
        self->snooping_bridge_up = enable;
    if (strncmp(ifname, self->static_mrouter, IFNAMSIZ) == 0)
        self->static_mrouter_up = enable;
    if (strncmp(ifname, self->proxy_upstream_if, IFNAMSIZ) == 0)
        self->proxy_upstream_if_up = enable;
    if (strncmp(ifname, self->proxy_downstream_if, IFNAMSIZ) == 0)
        self->proxy_downstream_if_up = enable;

    return true;
}

bool lnx_igmp_apply(lnx_igmp_t *self)
{
    /* Apply OVS config */
    lnx_mcast_apply();

    return true;
}
