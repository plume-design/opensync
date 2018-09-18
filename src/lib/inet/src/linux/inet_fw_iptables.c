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

#include <sys/wait.h>
#include <errno.h>

#include "log.h"
#include "const.h"
#include "util.h"
#include "execsh.h"

#include "inet_fw.h"

#define FW_RULE_IPV4                (1 << 0)
#define FW_RULE_IPV6                (1 << 1)
#define FW_RULE_ALL                 (FW_RULE_IPV4 | FW_RULE_IPV6)

#if !defined(CONFIG_USE_KCONFIG)

#define CONFIG_INET_FW_IPTABLES_PATH    "/usr/sbin/iptables"
#define CONFIG_INET_FW_IP6TABLES_PATH   "/usr/sbin/ip6tables"

#endif /* CONFIG_USE_KCONFIG */

struct __inet_fw
{
    char                    fw_ifname[C_IFNAME_LEN];
    bool                    fw_enabled;
    bool                    fw_nat_enabled;
};

/**
 * fw_rule_*() macros -- pass a dummy argv[0] value ("false") -- it will
 * be overwritten with the real path to iptables in the __fw_rule*() function.
 */
static bool fw_rule_add_a(inet_fw_t *self, int type, char *argv[]);
#define fw_rule_add(self, type, table, chain, ...) \
        fw_rule_add_a(self, type, C_VPACK("false", table, chain, __VA_ARGS__))

static bool fw_rule_del_a(inet_fw_t *self, int type, char *argv[]);
#define fw_rule_del(self, type, table, chain, ...) \
        fw_rule_del_a(self, type, C_VPACK("false", table, chain, __VA_ARGS__))

static bool fw_nat_start(inet_fw_t *self);
static bool fw_nat_stop(inet_fw_t *self);


/*
 * ===========================================================================
 *  Public interface
 * ===========================================================================
 */
bool inet_fw_init(inet_fw_t *self, const char *ifname)
{

    memset(self, 0, sizeof(*self));


    if (strscpy(self->fw_ifname, ifname, sizeof(self->fw_ifname)) < 0)
    {
        LOG(ERR, "fw: Interface name %s is too long.", ifname);
        return false;
    }
    /* Start by flushing NAT/LAN rules */
    (void)fw_nat_stop(self);

    return true;
}

bool inet_fw_fini(inet_fw_t *self)
{
    bool retval = inet_fw_stop(self);

    return retval;
}


inet_fw_t *inet_fw_new(const char *ifname)
{
    inet_fw_t *self = malloc(sizeof(inet_fw_t));

    if (!inet_fw_init(self, ifname))
    {
        free(self);
        return NULL;
    }

    return self;
}

bool inet_fw_del(inet_fw_t *self)
{
    bool retval = inet_fw_fini(self);
    if (!retval)
    {
        LOG(WARN, "nat: Error stopping FW on interface: %s", self->fw_ifname);
    }

    free(self);

    return retval;
}

/**
 * Start the FW service on interface
 */
bool inet_fw_start(inet_fw_t *self)
{
    if (self->fw_enabled) return true;

    if (!fw_nat_start(self))
    {
        LOG(WARN, "fw: NAT/LAN rules failed to apply to %s.", self->fw_ifname);
    }

    self->fw_enabled = true;

    return true;
}

/**
 * Stop the FW service on interface
 */
bool inet_fw_stop(inet_fw_t *self)
{
    bool retval = true;

    if (!self->fw_enabled) return true;

    retval &= fw_nat_stop(self);

    self->fw_enabled = false;

    return retval;
}

bool inet_fw_nat_set(inet_fw_t *self, bool enable)
{
    self->fw_nat_enabled = enable;

    return true;
}

bool inet_fw_state_get(inet_fw_t *self, bool *nat_enabled)
{
    *nat_enabled = self->fw_enabled && self->fw_nat_enabled;

    return true;
}

/*
 * ===========================================================================
 *  Private functions
 * ===========================================================================
 */

/**
 * Either enable NAT or LAN rules on the interface
 */
bool fw_nat_start(inet_fw_t *self)
{
    bool retval = true;

    if (self->fw_nat_enabled)
    {
        LOG(INFO, "fw: Installing NAT rules on interface %s.", self->fw_ifname);

        retval &= fw_rule_add(self,
                FW_RULE_IPV4, "nat", "NM_NAT",
                "-o", self->fw_ifname, "-j", "MASQUERADE");

        /* Plant miniupnpd rules for port forwarding via upnp */
        retval &= fw_rule_add(self,
                FW_RULE_IPV4, "nat", "NM_PORT_FORWARD",
                "-i", self->fw_ifname, "-j", "MINIUPNPD");
    }
    else
    {
        LOG(INFO, "fw: Installing LAN rules on interface: %s.", self->fw_ifname);

        retval &= fw_rule_add(self,
                FW_RULE_ALL, "filter", "NM_INPUT",
                "-i", self->fw_ifname, "-j", "ACCEPT");
    }

    return true;
}

/**
 * Flush all NAT/LAN rules
 */
bool fw_nat_stop(inet_fw_t *self)
{
    bool retval = true;

    LOG(INFO, "fw: Flushing NAT/LAN related rules on %s.", self->fw_ifname);

    /* Flush out NAT rules */
    retval &= fw_rule_del(self, FW_RULE_IPV4,
            "nat", "NM_NAT", "-o", self->fw_ifname, "-j", "MASQUERADE");
    retval &= fw_rule_del(self, FW_RULE_IPV4,
            "nat", "NM_PORT_FORWARD", "-i", self->fw_ifname, "-j", "MINIUPNPD");

    /* Flush out LAN rules */
    retval &= fw_rule_del(self, FW_RULE_ALL,
            "filter", "NM_INPUT", "-i", self->fw_ifname, "-j", "ACCEPT");

    return retval;
}

char fw_rule_add_cmd[] =
_S(
    IPTABLES="$1";
    tbl="$2";
    chain="$3";
    shift 3;
    if ! "$IPTABLES" -t "$tbl" -C "$chain" "$@" 2> /dev/null;
    then
        "$IPTABLES" -t "$tbl" -A "$chain" "$@";
    fi;
);

bool fw_rule_add_a(inet_fw_t *self,int type, char *argv[])
{
    int status;

    bool retval = true;

    if (type & FW_RULE_IPV4)
    {
        argv[0] = CONFIG_INET_FW_IPTABLES_PATH;
        status = execsh_log_a(LOG_SEVERITY_INFO, fw_rule_add_cmd, argv);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(WARN, "fw: IPv4 rule insertion failed on interface: %s -- %s",
                    self->fw_ifname,
                    fw_rule_add_cmd);
            retval = false;
        }
    }

    if (type & FW_RULE_IPV6)
    {
        argv[0] = CONFIG_INET_FW_IP6TABLES_PATH;
        status = execsh_log_a(LOG_SEVERITY_INFO, fw_rule_add_cmd,  argv);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(WARN, "fw: IPv6 rule deletion failed on interface: %s -- %s",
                    self->fw_ifname,
                    fw_rule_add_cmd);
            retval = false;
        }
    }

    return retval;
}

char fw_rule_del_cmd[] =
_S(
    IPTABLES="$1";
    tbl="$2";
    chain="$3";
    shift 3;
    if "$IPTABLES" -t "$tbl" -C "$chain" "$@" 2> /dev/null;
    then
        "$IPTABLES" -t "$tbl" -D "$chain" "$@";
    fi;
);

bool fw_rule_del_a(inet_fw_t *self, int type, char *argv[])
{
    int status;

    bool retval = true;

    if (type & FW_RULE_IPV4)
    {
        argv[0] = CONFIG_INET_FW_IPTABLES_PATH;
        status = execsh_log_a(LOG_SEVERITY_INFO, fw_rule_del_cmd, argv);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(WARN, "fw: IPv4 rule deletion failed on interface: %s -- %s",
                    self->fw_ifname,
                    fw_rule_del_cmd);
            retval = false;
        }
    }

    if (type & FW_RULE_IPV6)
    {
        argv[0] = CONFIG_INET_FW_IP6TABLES_PATH;
        status = execsh_log_a(LOG_SEVERITY_INFO, fw_rule_del_cmd,  argv);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(WARN, "fw: IPv6 rule deletion failed on interface: %s -- %s",
                    self->fw_ifname,
                    fw_rule_del_cmd);
            retval = false;
        }
    }

    return retval;
}

