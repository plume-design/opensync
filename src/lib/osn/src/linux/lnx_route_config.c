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
#include <string.h>
#include <ctype.h>

#include "osn_inet.h"
#include "os.h"
#include "memutil.h"

struct osn_route4_cfg
{
    // interface name string buffer, to be expanded dynamically to fit entire string
    char if_name[0];
};

osn_route4_cfg_t *lnx_route4_cfg_new(const char *if_name)
{
    int if_name_size = strlen(if_name) + 1;
    osn_route4_cfg_t *self = MALLOC(sizeof(*self) + if_name_size);
    strscpy(self->if_name, if_name, if_name_size);
    return self;
}

bool lnx_route4_cfg_del(osn_route4_cfg_t *self)
{
    FREE(self);
    return true;
}

const char* lnx_route4_cfg_name(const osn_route4_cfg_t *self)
{
    return self->if_name;
}

bool lnx_route_apply(osn_route4_cfg_t *self)
{
    (void)self;
    /* This implementation has no buffering for the routes, so apply
     * methods has nothing to do, all routes are already set in OS */
    return true;
}

static bool call_ip_route(char *cmd, int pos, size_t space, const char *if_name)
{
    int n = snprintf(cmd + pos, space, " dev %s", if_name);

    // check buffer overflow here, only if_name print is not size controlled
    // remaining parts have fixed length, which will fit in the buffer

    if((size_t)n >= space)
    {
        LOG(ERR, "route: %s() if_name string too long", __FUNCTION__);
        return false;
    }


    if (!is_input_shell_safe(if_name)) return false;

    int rc = cmd_log(cmd);
    return WEXIT_SUCCESS(rc);
}

static int build_ip_route_cmd(char *dst, size_t dst_size, const char *action, const osn_route4_t *route)
{
    int n = snprintf(dst, dst_size, "ip route %s %s", action, FMT_osn_ip_addr(route->dest));

    if (route->gw_valid)
    {
        n += snprintf(dst + n, dst_size - n, " via %s", FMT_osn_ip_addr(route->gw));
    }

    /* Use 'static' protocol, otherwise 'boot' is defaulted, which allows
     * routing daemon to delete all 'boot' routes as a temporary */
    n += snprintf(dst + n, dst_size - n, " protocol static");

    if (route->metric >= 0)
    {
        n += snprintf(dst + n, dst_size - n, " metric %d", route->metric);
    }

    if (route->pref_src_set)
    {
        n += snprintf(dst + n, dst_size - n, " src %s", FMT_osn_ip_addr(route->pref_src));
    }
    if (route->table > 0)
    {
        n += snprintf(dst + n, dst_size - n, " table %d", route->table);
    }

    return n;
}

static bool execute_ip_route(osn_route4_cfg_t *self, const char *action, const osn_route4_t *route)
{
    char cmd[256];
    int n = build_ip_route_cmd(cmd, sizeof(cmd), action, route);
    return call_ip_route(cmd, n, sizeof(cmd) - n, self->if_name);
}

bool lnx_route_add(osn_route4_cfg_t *self, const osn_route4_t *route)
{
    return execute_ip_route(self, "add", route);
}

bool lnx_route_remove(osn_route4_cfg_t *self, const osn_route4_t *route)
{
    return execute_ip_route(self, "del", route);
}

/**
 * @brief Finds device interface name which is preceeded with " dev " string
 * Name string is terminated, NULL is returned if name is not found
 * 
 * @param str input string
 * @return ptr to device name string or NULL when not found
 */
static char *find_dev_name(char *str)
{
    const char *fld = " dev ";
    char *pos = strstr(str, fld);
    if (NULL == pos) return NULL;

    char *name = pos + strlen(fld);
    char *end = name;
    while(*end != 0 && !isspace(*end)) { end++; }
    *end = '\0';

    return *name == '\0' ? NULL : name;
}

bool lnx_route_find_dev(osn_ip_addr_t addr, char *buf, size_t bufSize)
{
    /* Get interface name from linux routing table */
    char cmd[256];
    (void)snprintf(cmd, sizeof(cmd), 
        "ip -4 -o route get %s 2>&1",
        FMT_osn_ip_addr(addr));

    char outbuf[512];
    int rc = cmd_buf(cmd, outbuf, sizeof(outbuf));
    if (WEXIT_SUCCESS(rc))
    {
        const char *if_name = find_dev_name(outbuf);
        if (if_name != NULL)
        {
            return strscpy(buf, if_name, bufSize) > 0;
        }
    }

    return false;
}
