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

#include "osn_tunnel_iface.h"
#include "lnx_tunnel_iface.h"
#include "kconfig.h"
#include "execsh.h"
#include "log.h"
#include "util.h"
#include "const.h"

/*
 * Script for creating a VTI tunnel interface.
 *
 * Input parameters:
 *
 * $1 - Interface name
 * $2 - local endpoint IP address
 * $3 - remote endpoint IP address
 * $4 - mark (optional)
 */
static const char lnx_tunnel_iface_vti_create[] = _S(
    if [ -n "$4" ]; then
        ip link add "$1" type vti local "$2" remote "$3" key "$4";
    else
        ip link add "$1" type vti local "$2" remote "$3";
    fi;);

/*
 * Script for deleting a tunnel interface
 *
 * Input parameters:
 *
 * $1 - Interface name
 */
static const char lnx_tunnel_iface_delete[] = _S(
    if [ -e "/sys/class/net/$1" ]; then
        ip link del "$1";
    fi;);

static bool set_proc_sys_route_based_tweaks(lnx_tunnel_iface_t *self);

bool lnx_tunnel_iface_init(lnx_tunnel_iface_t *self, const char *ifname)
{
    memset(self, 0, sizeof(*self));

    self->ti_type = OSN_TUNNEL_IFACE_TYPE_NOT_SET;

    STRSCPY(self->ti_ifname, ifname);

    return true;
}

bool lnx_tunnel_iface_type_set(lnx_tunnel_iface_t *self, enum osn_tunnel_iface_type iftype)
{
    LOG(TRACE, "lnx_tunnel_iface: %s: iftype=%d", __func__, iftype);

    if (iftype <= OSN_TUNNEL_IFACE_TYPE_NOT_SET || iftype >= OSN_TUNNEL_IFACE_TYPE_MAX)
    {
        LOG(ERR, "lnx_tunnel_iface: %s: invalid type: %d", self->ti_ifname, iftype);
        return false;
    }
    self->ti_type = iftype;

    return true;
}

bool lnx_tunnel_iface_endpoints_set(
        lnx_tunnel_iface_t *self,
        osn_ipany_addr_t local_endpoint,
        osn_ipany_addr_t remote_endpoint)
{
    LOG(TRACE, "lnx_tunnel_iface: %s: local_endpoint="PRI_osn_ipany_addr", remote_endpoint="PRI_osn_ipany_addr,
            __func__, FMT_osn_ipany_addr(local_endpoint), FMT_osn_ipany_addr(remote_endpoint));

    self->ti_local_endpoint = local_endpoint;
    self->ti_remote_endpoint = remote_endpoint;

    return true;
}

bool lnx_tunnel_iface_key_set(lnx_tunnel_iface_t *self, int key)
{
    LOG(TRACE, "lnx_tunnel_iface: %s: key=%d", __func__, key);

    self->ti_key = key;
    return true;
}

bool lnx_tunnel_iface_enable_set(lnx_tunnel_iface_t *self, bool enable)
{
    LOG(TRACE, "lnx_tunnel_iface: %s: enable=%d", __func__, enable);

    self->ti_enable = enable;
    return true;
}

bool lnx_tunnel_iface_apply(lnx_tunnel_iface_t *self)
{
    char slocal[MAX(OSN_IP6_ADDR_LEN, OSN_IP_ADDR_LEN)];
    char sremote[MAX(OSN_IP6_ADDR_LEN, OSN_IP_ADDR_LEN)];
    char skey[C_INT32_LEN];
    int rc;

    LOG(TRACE, "lnx_tunnel_iface: %s", __func__);

    if (self->ti_type == OSN_TUNNEL_IFACE_TYPE_NOT_SET)
    {
        LOG(ERR, "lnx_tunnel_iface: %s: Cannot apply config: if_type not set", self->ti_ifname);
        return false;
    }

    /* Silently delete old interfaces, if there are any */
    rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_tunnel_iface_delete, self->ti_ifname);

    if (!self->ti_enable)
    {
        LOG(INFO, "lnx_tunnel_iface: %s: NOT enabled", self->ti_ifname);
        return true;
    }

    /* Create a tunnel interface depending on type */
    switch (self->ti_type)
    {
        case OSN_TUNNEL_IFACE_TYPE_VTI:
            if (self->ti_local_endpoint.addr_type != AF_INET || self->ti_remote_endpoint.addr_type != AF_INET)
            {
                LOG(ERR, "lnx_tunnel_iface: %s: type=VTI: local or remote address not set or not IPv4 type", self->ti_ifname);
                return false;
            }

            snprintf(slocal, sizeof(slocal), PRI_osn_ip_addr, FMT_osn_ip_addr(self->ti_local_endpoint.addr.ip4));
            snprintf(sremote, sizeof(sremote), PRI_osn_ip_addr, FMT_osn_ip_addr(self->ti_remote_endpoint.addr.ip4));

            if (self->ti_key != 0)
            {
                snprintf(skey, sizeof(skey), "%d", self->ti_key);
            }
            else
            {
                skey[0] = '\0';
            }

            rc = execsh_log(
                    LOG_SEVERITY_DEBUG,
                    lnx_tunnel_iface_vti_create,
                    self->ti_ifname,
                    slocal,
                    sremote,
                    skey);
            if (rc != 0)
            {
                LOG(ERR, "lnx_tunnel_iface: %s: Error creating VTI tunnel interface (local %s, remote %s, key '%s').",
                        self->ti_ifname, slocal, sremote, skey);
                return false;
            }
            LOG(INFO, "lnx_tunnel_iface: %s: tunnel interface created", self->ti_ifname);

            /* Apply route-based tunnels tweaks to the created interface: */
            if (kconfig_enabled(CONFIG_OSN_TUNNEL_IFACE_ROUTE_BASED_TWEAKS))
            {
                set_proc_sys_route_based_tweaks(self);
            }

            self->ti_applied = true;
            break;

        case OSN_TUNNEL_IFACE_TYPE_VTI6:
            LOG(ERR, "lnx_tunnel_iface: %s: VTI6 type not currently supported", self->ti_ifname);
            return false;
            break;

        default:
            LOG(ERR, "lnx_tunnel_iface: %s: Unknown tunnel interface type: %d", self->ti_ifname, self->ti_type);
            return false;
    }

    return true;
}

bool lnx_tunnel_iface_fini(lnx_tunnel_iface_t *self)
{
    int rc;

    if (!self->ti_applied)
    {
        return true;
    }

    /* Silently delete old interfaces, if there are any */
    rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_tunnel_iface_delete, self->ti_ifname);
    if (rc != 0)
    {
        LOG(WARN, "lnx_tunnel_iface: %s: Error deleting interface.", self->ti_ifname);
    }
    return true;
}

/*
 * In order to use a route-based tunnel effectively, we need to make
 * some /proc/sys settings for the tunnel interface.
 */
static bool set_proc_sys_route_based_tweaks(lnx_tunnel_iface_t *self)
{
    char shell_cmd[C_MAXPATH_LEN];
    int rc;

    snprintf(
            shell_cmd,
            sizeof(shell_cmd),
            "sysctl -w net.ipv4.conf.%s.disable_policy=1; "
            "sysctl -w net.ipv4.conf.%s.rp_filter=2;",
            self->ti_ifname,
            self->ti_ifname
            );

    rc = execsh_log(LOG_SEVERITY_DEBUG, shell_cmd);
    if (rc != 0)
    {
        LOG(WARN, "lnx_tunnel_iface: %s: Failed applying /proc/sys route-based tweaks", self->ti_ifname);
        return false;
    }
    return true;
}
