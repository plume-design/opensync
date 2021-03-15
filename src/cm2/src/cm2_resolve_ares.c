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

// cm2 address resolution
#include <arpa/inet.h>
#include <netdb.h>

#include "log.h"
#include "cm2.h"

static int
cm2_start_ares_resolve(struct evx_ares *eares_p)
{
    int cnt;

    LOGI("ares: channel state = %d", eares_p->chan_initialized);

    cnt = evx_ares_get_count_busy_fds(eares_p);
    if (cnt > 0) {
        LOGI("ares: fds are still busy [left = %d], skip creating new channel", cnt);
        return -1;
    }
    evx_start_ares(eares_p);

    return 0;
}

static void
cm2_util_free_addr_list(cm2_addr_list *list)
{
    int i;

    if (list->h_addr_list)
    {
        for (i = 0; !list->h_addr_list[i]; i++) {
            if (!list->h_addr_list[i]) {
                free(list->h_addr_list[i]);
                list->h_addr_list[i] = NULL;
            }
        }
        free(list->h_addr_list);
        list->h_addr_list = NULL;
    }
}

void cm2_free_addr_list(cm2_addr_t *addr)
{
    cm2_util_free_addr_list(&addr->ipv6_addr_list);
    cm2_util_free_addr_list(&addr->ipv4_addr_list);
}

static void
cm2_ares_host_cb(void *arg, int status, int timeouts, struct hostent *hostent)
{
    cm2_addr_list        *addr;
    char                 buf[INET6_ADDRSTRLEN];
    int                  cnt;
    int                  i;

    addr = (cm2_addr_list *) arg;

    LOGI("ares: cb: status[%d]: %s  Timeouts: %d\n", status, ares_strerror(status), timeouts);
    addr->state = CM2_ARES_R_FINISHED;

    switch(status) {
        case ARES_SUCCESS:
            LOGI("ares: got address of host %s, req_type: %d, h addr type = %d timeouts: %d\n",
                 hostent->h_name, addr->req_addr_type, hostent->h_addrtype, timeouts);

            if (addr->req_addr_type != hostent->h_addrtype)
                return;

            for (i = 0; hostent->h_addr_list[i]; ++i)
            {
                inet_ntop(hostent->h_addrtype, hostent->h_addr_list[i], buf, INET6_ADDRSTRLEN);
                LOGI("Addr%d:[%d] %s\n", i, hostent->h_addrtype, buf);
            }

            cnt = i;
            addr->h_addr_list = (char **) malloc(sizeof(char*) * (cnt + 1));

            for (i = 0; i < cnt; i++)
            {
                addr->h_addr_list[i] = (char *) malloc(sizeof(char) * hostent->h_length);
                memcpy(addr->h_addr_list[i], hostent->h_addr_list[i], hostent->h_length);
            }
            addr->h_addr_list[i] = NULL;
            addr->state = CM2_ARES_R_RESOLVED;
            addr->h_addrtype = hostent->h_addrtype;;
            addr->h_cur_idx = 0;
            break;
        case ARES_EDESTRUCTION:
            LOGI("ares: channel was destroyed");
            break;
        case ARES_ECONNREFUSED:
        case ARES_ETIMEOUT:
        case ARES_ECANCELLED:
            if (addr->req_addr_type == AF_INET6)
                g_state.link.ipv6.resolve_retry = true;
            else
                g_state.link.ipv4.resolve_retry = true;
            break;
        default:
            LOGI("ares: didn't get address: status = %d, %d timeouts\n", status, timeouts);
            return;
    }
    return;
}

bool cm2_resolve(cm2_dest_e dest)
{
    cm2_addr_t *addr;
    bool       ipv6;
    bool       ipv4;   

    addr = cm2_get_addr(dest);

    ipv6 = g_state.link.ipv6.is_ip && addr->ipv6_addr_list.state != CM2_ARES_R_IN_PROGRESS ? true : false;
    ipv4 = g_state.link.ipv4.is_ip && addr->ipv4_addr_list.state != CM2_ARES_R_IN_PROGRESS ? true : false;

    if ((g_state.link.ipv4.resolve_retry && addr->ipv6_addr_list.state == CM2_ARES_R_RESOLVED) ||
        (g_state.link.ipv6.resolve_retry && addr->ipv4_addr_list.state == CM2_ARES_R_RESOLVED))
    {
        LOGI("Skip resolve re-trying");
        g_state.link.ipv4.resolve_retry = false;
        g_state.link.ipv6.resolve_retry = false;
        return true;
    }

    g_state.link.ipv4.resolve_retry = false;
    g_state.link.ipv6.resolve_retry = false;

    if (addr->ipv4_addr_list.state == CM2_ARES_R_IN_PROGRESS ||
        addr->ipv6_addr_list.state == CM2_ARES_R_IN_PROGRESS)
    {
        LOGI("Waiting for uplinks: ipv6: [%d, %d], ipv4: [%d, %d]",
             g_state.link.ipv6.is_ip, addr->ipv6_addr_list.state,
             g_state.link.ipv4.is_ip, addr->ipv4_addr_list.state);
        return false;
    }

    addr->updated = false;
    if (!addr->valid)
        return false;

    LOGI("ares: resolving:'%s'", addr->resource);

    if (cm2_start_ares_resolve(&g_state.eares) < 0)
        return false;

    cm2_free_addr_list(addr);

    if (!g_state.eares.chan_initialized) {
        LOGI("ares: channel not initialized yet");
        return false;
    }

    /* IPv6 */
    if (ipv6) {
        LOGI("Resolving IPv6 addresses");
        addr->ipv6_addr_list.state = CM2_ARES_R_IN_PROGRESS;
        addr->ipv6_addr_list.req_addr_type = AF_INET6;
        ares_gethostbyname(g_state.eares.ares.channel, addr->hostname, AF_INET6, cm2_ares_host_cb, (void *) &addr->ipv6_addr_list);
    }

    /* IPv4 */
    if (ipv4) {
        LOGI("Resolving IPv4 addresses");
        addr->ipv4_addr_list.state = CM2_ARES_R_IN_PROGRESS;
        addr->ipv4_addr_list.req_addr_type = AF_INET;
        ares_gethostbyname(g_state.eares.ares.channel, addr->hostname, AF_INET, cm2_ares_host_cb, (void *) &addr->ipv4_addr_list);
    }

    return true;
}

void cm2_resolve_timeout(void)
{
    LOGI("ares: timeout calling");
    evx_stop_ares(&g_state.eares);
}

static bool
cm2_validate_target_addr(cm2_addr_list *list, int addr_type)
{
    if (addr_type == AF_INET && (!g_state.link.ipv4.is_ip || g_state.link.ipv4.blocked))
        return false;

    if (addr_type == AF_INET6 && (!g_state.link.ipv6.is_ip || g_state.link.ipv6.blocked))
        return false;

    if (!list->h_addr_list)
        return false;

    if (!list->h_addr_list[list->h_cur_idx])
        return false;

    if (list->h_addrtype != addr_type)
        return false;

    return true;
}

static bool
cm2_write_target_addr(cm2_addr_t *addr)
{
    const char     *result;
    char           target[256];
    char           *buf;
    int            ret;

    if (cm2_validate_target_addr(&addr->ipv6_addr_list, AF_INET6))
    {
        char buffer[INET6_ADDRSTRLEN] = "";

        buf = addr->ipv6_addr_list.h_addr_list[addr->ipv6_addr_list.h_cur_idx];
        result = inet_ntop(AF_INET6, buf, buffer, sizeof(buffer));
        if (result == 0)
        {
            LOGD("ares: translation to ipv6 address failed");
            return false;
        }

        snprintf(target, sizeof(target), "%s:[%s]:%d",
                 addr->proto,
                 buffer,
                 addr->port);

        addr->ipv6_cur = true;
        g_state.ipv6_manager_con = true;
    }
    else if (cm2_validate_target_addr(&addr->ipv4_addr_list, AF_INET))
    {
        char buffer[INET_ADDRSTRLEN] = "";

        buf = addr->ipv4_addr_list.h_addr_list[addr->ipv4_addr_list.h_cur_idx];
        result = inet_ntop(AF_INET, buf, buffer, sizeof(buffer));
        if (result == 0)
        {
            LOGD("ares: translation to ipv4 address failed");
            return false;
        }

        snprintf(target, sizeof(target), "%s:[%s]:%d",
                 addr->proto,
                 buffer,
                 addr->port);

        addr->ipv6_cur = false;
        g_state.ipv6_manager_con = false;
    }
    else
    {
        LOGI("ares: No address available");
        return false;
    }

    ret = cm2_ovsdb_set_Manager_target(target);
    if (ret)
        LOGI("trying to connect to: %s : %s", cm2_curr_dest_name(), target);

    return ret;
}

bool cm2_write_current_target_addr(void)
{
    cm2_addr_t *addr = cm2_curr_addr();
    return cm2_write_target_addr(addr);
}

bool cm2_write_next_target_addr(void)
{
    cm2_addr_t *addr;

    addr = cm2_curr_addr();
    if (addr->ipv6_cur)
        addr->ipv6_addr_list.h_cur_idx++;
    else
        addr->ipv4_addr_list.h_cur_idx++;

    return  cm2_write_target_addr(addr);
}

bool cm2_is_addr_resolved(const cm2_addr_t *addr)
{
    LOGI("Resolved state: ipv4: %d ipv6: %d", addr->ipv4_addr_list.state, addr->ipv6_addr_list.state);
    return addr->ipv4_addr_list.state != CM2_ARES_R_IN_PROGRESS && addr->ipv6_addr_list.state != CM2_ARES_R_IN_PROGRESS;
}
