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

#include <net/if.h>

#include "log.h"
#include "execsh.h"
#include "os_util.h"
#include "kconfig.h"

#include "lnx_mcast.h"

/* Default number of apply retries before giving up */
#define MCPD_APPLY_RETRIES  5

void lnx_mcast_apply_fn(struct ev_loop *loop, ev_debounce *w, int revent);

/* Native bridge igmp snooping config */
static char set_mcast_snooping_native[] = _S(echo "$1" > /sys/devices/virtual/net/"$2"/bridge/multicast_snooping);
static char set_fast_leave_native[] = _S(for file in /sys/devices/virtual/net/"$1"/lower*/brport/multicast_fast_leave; \
                                            do echo "$2" > "$file"; done);

lnx_mcast_bridge lnx_mcast_bridge_base;

static lnx_mcast_bridge *lnx_mcast_bridge_init()
{
    lnx_mcast_bridge *self = &lnx_mcast_bridge_base;

    if (self->initialized)
        return self;

    /* Initialize apply debounce */
    ev_debounce_init2(&self->apply_debounce, lnx_mcast_apply_fn, 0.4, 2.0);

    self->initialized = true;

    return self;
}

lnx_igmp_t *lnx_mcast_bridge_igmp_init()
{
    lnx_mcast_bridge *self = lnx_mcast_bridge_init();
    self->igmp_initialized = true;

    return &self->igmp;
}

lnx_mld_t *lnx_mcast_bridge_mld_init()
{
    lnx_mcast_bridge *self = lnx_mcast_bridge_init();
    self->mld_initialized = true;

    return &self->mld;
}

char *lnx_mcast_other_config_get_string(
        const struct osn_mcast_other_config *other_config,
        const char *key)
{
    int ii;

    for (ii = 0; ii < other_config->oc_len; ii++)
    {
        if (strcmp(other_config->oc_config[ii].ov_key, key) == 0)
        {
            return other_config->oc_config[ii].ov_value;
        }
    }

    return NULL;
}

long lnx_mcast_other_config_get_long(const struct osn_mcast_other_config *other_config, const char *key)
{
    char *str = lnx_mcast_other_config_get_string(other_config, key);
    long val;

    if (str == NULL && os_strtoul(str, &val, 0) == true)
    {
        return val;
    }

    return 0;
}

bool lnx_mcast_free_string_array(char **arr, int len) {
    int ii;

    for (ii = 0; ii < len; ii++)
    {
        FREE(arr[ii]);
    }
    FREE(arr);

    return true;
}

static bool lnx_mcast_deconfigure(lnx_mcast_bridge *self)
{
    int status;

    LOGI("lnx_mcast_deconfigure: called with %s", self->snooping_bridge);

    if (self->snooping_bridge[0] == '\0')
        return true;

    /* Disable snooping */
    status = execsh_log(LOG_SEVERITY_DEBUG, set_mcast_snooping_native, "0", self->snooping_bridge);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        LOG(INFO, "lnx_mcast_deconfigure: Cannot disable snooping on bridge %s",
                self->snooping_bridge);
    }

    /* Disable fast leave */
    status = execsh_log(LOG_SEVERITY_DEBUG, set_fast_leave_native, self->snooping_bridge, "0");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        LOG(INFO, "lnx_mcast_deconfigure: Cannot disable fast leave on bridge %s",
                self->snooping_bridge);
    }

    self->snooping_bridge[0] = '\0';
    return true;
}

/* Returns false, if reapply is needed */
bool lnx_mcast_apply_config(lnx_mcast_bridge *self)
{
    lnx_igmp_t *igmp = &self->igmp;
    lnx_mld_t *mld = &self->mld;
    bool snooping_enabled;
    char *snooping_bridge;
    bool snooping_bridge_up;
    bool fast_leave_enable;
    int status;

    if (igmp->snooping_enabled || !mld->snooping_enabled)
    {
        snooping_enabled = igmp->snooping_enabled;
        snooping_bridge = igmp->snooping_bridge;
        snooping_bridge_up = igmp->snooping_bridge_up;
        fast_leave_enable = igmp->fast_leave_enable;
    }
    else
    {
        snooping_enabled = mld->snooping_enabled;
        snooping_bridge = mld->snooping_bridge;
        snooping_bridge_up = mld->snooping_bridge_up;
        fast_leave_enable = mld->fast_leave_enable;
    }

    /* If snooping was turned off or snooping bridge was changed, deconfigure it first */
    if (snooping_bridge_up == false || strncmp(self->snooping_bridge, snooping_bridge, IFNAMSIZ) != 0)
        lnx_mcast_deconfigure(self);

    if (snooping_bridge_up == false || snooping_bridge[0] == '\0')
        return true;

    /* Enable/disable snooping */
    status = execsh_log(LOG_SEVERITY_DEBUG, set_mcast_snooping_native, snooping_enabled ? "1" : "0",
                        snooping_bridge);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        LOG(ERR, "lnx_mcast_apply_config: Error enabling/disabling snooping, command failed for %s",
                snooping_bridge);
        return false;
    }
    STRSCPY_WARN(self->snooping_bridge, snooping_bridge);

    /* Enable/disable fast leave */
    status = execsh_log(LOG_SEVERITY_DEBUG, set_fast_leave_native, snooping_bridge,
                        fast_leave_enable ? "1" : "0");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        LOG(ERR, "lnx_mcast_apply_config: Error enabling/disabling fast leave, command failed for %s",
                snooping_bridge);
        return false;
    }

    return true;
}

bool lnx_mcast_apply()
{
    lnx_mcast_bridge *self = &lnx_mcast_bridge_base;
    self->apply_retry = MCPD_APPLY_RETRIES;
    ev_debounce_start(EV_DEFAULT, &self->apply_debounce);

    return true;
}

void lnx_mcast_apply_fn(struct ev_loop *loop, ev_debounce *w, int revent)
{
    lnx_mcast_bridge *self = &lnx_mcast_bridge_base;
    bool status;

    status = lnx_mcast_apply_config(self);

    if (!self->igmp_initialized && !self->mld_initialized)
        return;

    /* Apply OVS configuration */
    if (status == false)
    {
        /* Schedule retry until retry limit reached */
        if (self->apply_retry > 0)
        {
            LOG(INFO, "lnx_mcast_apply_fn: retry %d", self->apply_retry);
            self->apply_retry--;
            ev_debounce_start(loop, w);
            return;
        }

        LOG(ERR, "lnx_mcast_apply_fn: Unable to apply mcast bridge configuration.");
    }

    return;
}
