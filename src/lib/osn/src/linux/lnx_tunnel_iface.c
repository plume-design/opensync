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
 * Script for creating a VTI6 tunnel interface.
 *
 * Input parameters:
 *
 * $1 - Interface name
 * $2 - local endpoint IP6 address
 * $3 - remote endpoint IP6 address
 * $4 - key (optional)
 */
static const char lnx_tunnel_iface_vti6_create[] = _S(
    if [ -n "$4" ]; then
        ip link add name "$1" type vti6 local "$2" remote "$3" key "$4";
    else
        ip link add name "$1" type vti6 local "$2" remote "$3";
    fi;);

/*
 * Script for creating an IP6TNL tunnel interface.
 *
 * Input parameters:
 *
 * $1 - Interface name
 * $2 - local endpoint IP address
 * $3 - remote endpoint IP address
 * $4 - mode
 * $5 - physical device to use for tunnel endpoint communication (optional)
 */
static const char lnx_tunnel_iface_ip6tnl_create[] = _S(
    if [ -n "$5" ]; then
        ip link add "$1" type ip6tnl local "$2" remote "$3" mode "$4" dev "$5";
    else
        ip link add "$1" type ip6tnl local "$2" remote "$3" mode "$4";
    fi;);

/*
 * Script for creating an *GRE* tunnel interface.
 *
 * Input parameters:
 *
 * $1 - Interface name
 * $2 - Interface type (gre, gretap, ip6gre, ip6gretap)
 * $3 - local endpoint IP address
 * $4 - remote endpoint IP address
 * $5 - key (optional)
 * $6 - physical device to use for tunnel endpoint communication (optional)
 */
static const char lnx_tunnel_iface_gre_create[] = _S(
    if [ -n "$5" ]; then
        opt_key="key $5";
    else
        opt_key="";
    fi;
    if [ -n "$6" ]; then
        opt_dev="dev $6";
    else
        opt_dev="";
    fi;
    ip link add "$1" type "$2" local "$3" remote "$4" $opt_key $opt_dev;
);

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
static bool set_proc_sys_route_based_tweaks_ipv6(lnx_tunnel_iface_t *self);

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

bool lnx_tunnel_iface_mode_set(lnx_tunnel_iface_t *self, enum osn_tunnel_iface_mode mode)
{
    LOG(TRACE, "lnx_tunnel_iface: %s: mode=%d", __func__, mode);

    if (mode < OSN_TUNNEL_IFACE_MODE_NOT_SET || mode >= OSN_TUNNEL_IFACE_MODE_MAX)
    {
        LOG(ERR, "lnx_tunnel_iface: %s: invalid mode: %d", self->ti_ifname, mode);
        return false;
    }
    self->ti_mode = mode;

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

bool lnx_tunnel_iface_dev_set(lnx_tunnel_iface_t *self, const char *dev_ifname)
{
    LOG(TRACE, "lnx_tunnel_iface: %s: dev_ifname=%s", __func__, dev_ifname);

    STRSCPY(self->ti_dev_ifname, dev_ifname);
    return true;
}


bool lnx_tunnel_iface_enable_set(lnx_tunnel_iface_t *self, bool enable)
{
    LOG(TRACE, "lnx_tunnel_iface: %s: enable=%d", __func__, enable);

    self->ti_enable = enable;
    return true;
}

char* lnx_tunnel_iface_gre_type_to_lnx_ip_link(enum osn_tunnel_iface_type if_type)
{
    switch (if_type) {
        case OSN_TUNNEL_IFACE_TYPE_IP4GRE: return "gre";
        case OSN_TUNNEL_IFACE_TYPE_IP4GRETAP: return "gretap";
        case OSN_TUNNEL_IFACE_TYPE_IP6GRE: return "ip6gre";
        case OSN_TUNNEL_IFACE_TYPE_IP6GRETAP: return "ip6gretap";
        default: return NULL;
    }
}

bool lnx_tunnel_iface_apply(lnx_tunnel_iface_t *self)
{
    char slocal[MAX(OSN_IP6_ADDR_LEN, OSN_IP_ADDR_LEN)];
    char sremote[MAX(OSN_IP6_ADDR_LEN, OSN_IP_ADDR_LEN)];
    char skey[C_INT32_LEN];
    char *mode = NULL;
    char *type_str = NULL;
    char *ti_type_str = NULL;
    int rc;

    LOG(TRACE, "lnx_tunnel_iface: %s", __func__);

    if (self->ti_type == OSN_TUNNEL_IFACE_TYPE_IP6TNL && kconfig_enabled(CONFIG_OSN_LINUX_MAPE_IP6TNL))
    {
        /*
         * This check should be temporary and removed when controller stops configuring MAP-E ip6tnl
         * interface via Tunnel_Interface. This was the first design, but it has limitations and
         * for MAP-E we had to switch to creating ip6tnl interfaces elsewhere (osn/lnx_map_mape).
         *
         * It would though still be good to keep this Tunnel_Interface API for creating ip6tnl
         * interfaces in case if needed for any other feature in the future.
         */
        LOG(WARN, "lnx_tunnel_iface: %s: Ignoring OVSDB Tunnel_Interface request to create ip6tnl interface", self->ti_ifname);
        return false;
    }

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
            if (self->ti_local_endpoint.addr_type != AF_INET6 || self->ti_remote_endpoint.addr_type != AF_INET6)
            {
                LOG(ERR, "lnx_tunnel_iface: %s: type=VTI6: local or remote address not set or not IPv6 type", self->ti_ifname);
                return false;
            }

            snprintf(slocal, sizeof(slocal), PRI_osn_ip6_addr, FMT_osn_ip6_addr(self->ti_local_endpoint.addr.ip6));
            snprintf(sremote, sizeof(sremote), PRI_osn_ip6_addr, FMT_osn_ip6_addr(self->ti_remote_endpoint.addr.ip6));

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
                    lnx_tunnel_iface_vti6_create,
                    self->ti_ifname,
                    slocal,
                    sremote,
                    skey);
            if (rc != 0)
            {
                LOG(ERR, "lnx_tunnel_iface: %s: Error creating VTI6 tunnel interface (local %s, remote %s, key '%s').",
                        self->ti_ifname, slocal, sremote, skey);
                return false;
            }
            LOG(INFO, "lnx_tunnel_iface: %s: vti6 tunnel interface created", self->ti_ifname);

            /* Apply route-based tunnels tweaks to the created interface: */
            if (kconfig_enabled(CONFIG_OSN_TUNNEL_IFACE_ROUTE_BASED_TWEAKS))
            {
                set_proc_sys_route_based_tweaks_ipv6(self);
            }

            self->ti_applied = true;
            break;

        case OSN_TUNNEL_IFACE_TYPE_IP6TNL:
            if (self->ti_local_endpoint.addr_type != AF_INET6 || self->ti_remote_endpoint.addr_type != AF_INET6)
            {
                LOG(ERR, "lnx_tunnel_iface: %s: type=IP6TNL: local or remote address not set or not IPv6 type", self->ti_ifname);
                return false;
            }

            snprintf(slocal, sizeof(slocal), PRI_osn_ip6_addr, FMT_osn_ip6_addr(self->ti_local_endpoint.addr.ip6));
            snprintf(sremote, sizeof(sremote), PRI_osn_ip6_addr, FMT_osn_ip6_addr(self->ti_remote_endpoint.addr.ip6));

            if (self->ti_mode == OSN_TUNNEL_IFACE_MODE_IPIP6) mode = "ipip6";
            else if (self->ti_mode == OSN_TUNNEL_IFACE_MODE_IP6IP6) mode = "ip6ip6";
            else mode = "any";

            rc = execsh_log(
                    LOG_SEVERITY_DEBUG,
                    lnx_tunnel_iface_ip6tnl_create,
                    self->ti_ifname,
                    slocal,
                    sremote,
                    mode,
                    self->ti_dev_ifname);
            if (rc != 0)
            {
                LOG(ERR, "lnx_tunnel_iface: %s: Error creating IP6TNL tunnel interface (local %s, remote %s, mode '%s', dev '%s').",
                        self->ti_ifname, slocal, sremote, mode, self->ti_dev_ifname);
                return false;
            }
            LOG(INFO, "lnx_tunnel_iface: %s: tunnel interface created", self->ti_ifname);

            self->ti_applied = true;
            break;

        case OSN_TUNNEL_IFACE_TYPE_IP4GRE:
            ti_type_str = "IP4GRE";
            goto L_gre_ipv4;

        case OSN_TUNNEL_IFACE_TYPE_IP4GRETAP:
            ti_type_str = "IP4GRETAP";
        L_gre_ipv4:
            if (self->ti_local_endpoint.addr_type != AF_INET || self->ti_remote_endpoint.addr_type != AF_INET)
            {
                LOG(ERR, "lnx_tunnel_iface: %s: type=%s: local or remote address not set or not IPv4 type", self->ti_ifname, ti_type_str);
                return false;
            }

            snprintf(slocal, sizeof(slocal), PRI_osn_ip_addr, FMT_osn_ip_addr(self->ti_local_endpoint.addr.ip4));
            snprintf(sremote, sizeof(sremote), PRI_osn_ip_addr, FMT_osn_ip_addr(self->ti_remote_endpoint.addr.ip4));

            goto L_gre_common;

        case OSN_TUNNEL_IFACE_TYPE_IP6GRE:
            ti_type_str = "IP6GRE";
            goto L_gre_ipv6;

        case OSN_TUNNEL_IFACE_TYPE_IP6GRETAP:
            ti_type_str = "IP6GRETAP";
        L_gre_ipv6:
            if (self->ti_local_endpoint.addr_type != AF_INET6 || self->ti_remote_endpoint.addr_type != AF_INET6)
            {
                LOG(ERR, "lnx_tunnel_iface: %s: type=%s: local or remote address not set or not IPv6 type", self->ti_ifname, ti_type_str);
                return false;
            }

            snprintf(slocal, sizeof(slocal), PRI_osn_ip6_addr, FMT_osn_ip6_addr(self->ti_local_endpoint.addr.ip6));
            snprintf(sremote, sizeof(sremote), PRI_osn_ip6_addr, FMT_osn_ip6_addr(self->ti_remote_endpoint.addr.ip6));

        L_gre_common:

            type_str = lnx_tunnel_iface_gre_type_to_lnx_ip_link(self->ti_type);
            if (!type_str)
            {
                LOG(ERR, "lnx_tunnel_iface: %s: Unknown ip link type for gre tunnel interface type: %d", self->ti_ifname, self->ti_type);
                return false;
            }

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
                    lnx_tunnel_iface_gre_create,
                    self->ti_ifname,
                    type_str,
                    slocal,
                    sremote,
                    skey,
                    self->ti_dev_ifname);
            if (rc != 0)
            {
                LOG(ERR, "lnx_tunnel_iface: %s: Error creating GRE tunnel interface (local %s, remote %s, type %s, key '%s' dev '%s').",
                        self->ti_ifname, slocal, sremote, type_str, skey, self->ti_dev_ifname);
                return false;
            }
            LOG(INFO, "lnx_tunnel_iface: %s: tunnel interface created", self->ti_ifname);

            self->ti_applied = true;
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

static bool set_proc_sys_route_based_tweaks_ipv6(lnx_tunnel_iface_t *self)
{
    char shell_cmd[C_MAXPATH_LEN];
    int rc;

    snprintf(
            shell_cmd,
            sizeof(shell_cmd),
            "sysctl -w net.ipv6.conf.%s.disable_policy=1",
            self->ti_ifname
            );

    rc = execsh_log(LOG_SEVERITY_DEBUG, shell_cmd);
    if (rc != 0)
    {
        LOG(WARN, "lnx_tunnel_iface: %s: Failed applying /proc/sys route-based ipv6 tweaks", self->ti_ifname);
        return false;
    }
    return true;
}
