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

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>

#include "lnx_map_mapt.h"
#include "lnx_map.h"

#include "memutil.h"
#include "execsh.h"
#include "os_nif.h"
#include "util.h"
#include "log.h"

/**
 * The MAP-T backend implementation using nat46.
 */

#define NAT46_CONTROL_IFACE   "/proc/net/nat46/control"

/* Write the specified configuration string to the nat46 configuration interface. */
static bool nat46_ctrl_iface_write(const char *cfgstr)
{
    FILE *f = NULL;
    bool rv = true;

    /* First, check if nat46 control interface exists: */
    if (access(NAT46_CONTROL_IFACE, F_OK) != 0)
    {
        LOG(ERR, "lnx_map_mapt: nat46 control interface %s does not exist", NAT46_CONTROL_IFACE);
        return false;
    }

    f = fopen(NAT46_CONTROL_IFACE, "w");
    if (f == NULL)
    {
        LOG(ERR, "lnx_map_mapt: Error opening %s for writing: %s", NAT46_CONTROL_IFACE, strerror(errno));
        return false;
    }

    if (fwrite(cfgstr, strlen(cfgstr), 1, f) != 1)
    {
        LOG(ERR, "lnx_map_mapt: Error writing to %s", NAT46_CONTROL_IFACE);
        rv = false;
    }

    fclose(f);
    return rv;
}

bool lnx_map_mapt_config_apply(lnx_map_t *self)
{
    char cfgstr[256];
    bool map_iface_exists;
    int rv;

    if (self->lm_type != OSN_MAP_TYPE_MAP_T)
    {
        return false;
    }

    /* Check if MAP config set: */
    if (self->lm_bmr == NULL)
    {
        LOG(ERR, "lnx_map_mapt: %s: Config not set or invalid. Cannot configure nat46",
                self->lm_if_name);
        return false;
    }

    /*
     * If config for this MAP-T MAP interface already applied (MAP interface exists),
     * then deconfigure the existing MAP-T config first:
     */
    if (!os_nif_exists((char *)self->lm_if_name, &map_iface_exists) || map_iface_exists)
    {
        LOG(DEBUG, "lnx_map_mapt: %s: MAP interface already exists. Deconfigure its MAP config first",
                self->lm_if_name);
        if (!lnx_map_mapt_config_del(self))
        {
            return false;
        }
    }

    /* Build configuration string: */
    rv = snprintf(
        cfgstr,
        sizeof(cfgstr),
        "add %s\nconfig %s local.v4 %s local.v6 %s local.style MAP local.ea-len %d"
        " local.psid-offset %d remote.v4 0.0.0.0/0 remote.v6 %s"
        " remote.style RFC6052 remote.ea-len 0 remote.psid-offset 0 debug 0\n",
                self->lm_if_name, self->lm_if_name,
                FMT_osn_ip_addr(self->lm_bmr->om_ipv4prefix),
                FMT_osn_ip6_addr(self->lm_bmr->om_ipv6prefix),
                self->lm_bmr->om_ea_len, self->lm_bmr->om_psid_offset,
                FMT_osn_ip6_addr(self->lm_bmr->om_dmr));

    if (rv == -1 || rv >= (int)sizeof(cfgstr))
    {
        LOG(ERR, "lnx_map_mapt: %s: Error building nat46 configuration string", self->lm_if_name);
        return false;
    }

    LOG(DEBUG, "lnx_map_mapt: %s: Configuring nat46 with config: '%s'", self->lm_if_name, cfgstr);

    /* Configure nat46: */
    if (!nat46_ctrl_iface_write(cfgstr))
    {
        LOG(ERR, "lnx_map_mapt: %s: Error writing to nat46 ctrl iface", self->lm_if_name);
        return false;
    }

    LOG(INFO, "lnx_map_mapt: %s: Configured nat46", self->lm_if_name);

    return true;
}

bool lnx_map_mapt_config_del(lnx_map_t *self)
{
    char cfgstr[256];
    int rv;

    /* Deconfiguration string: */
    rv = snprintf(cfgstr, sizeof(cfgstr), "del %s\n", self->lm_if_name);
    if (rv == -1 || rv >= (int)sizeof(cfgstr))
    {
        LOG(ERR, "lnx_map_mapt: %s: Error building nat46 deconfiguration string", self->lm_if_name);
        return false;
    }

    LOG(DEBUG, "lnx_map_mapt: %s: Deconfiguring nat46", self->lm_if_name);

    /* Deconfigure nat46: */
    if (!nat46_ctrl_iface_write(cfgstr))
    {
        LOG(ERR, "lnx_map_mapt: %s: Error writing to nat46 ctrl iface", self->lm_if_name);
        return false;
    }

    LOG(DEBUG, "lnx_map_mapt: %s: Deconfigured nat46", self->lm_if_name);

    return true;
}