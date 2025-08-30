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
#include "memutil.h"

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
                FREE(list->h_addr_list[i]);
                list->h_addr_list[i] = NULL;
            }
        }
        FREE(list->h_addr_list);
        list->h_addr_list = NULL;
    }
}

static char*
addr_family_to_str(int family)
{
    char *str = "Unknown";

    switch (family)
    {
        case AF_INET:
            str = "AF_INET";
            break;
        case AF_INET6:
            str = "AF_INET6";
            break;
        default:
            LOGI("%s(): Invalid family: %d", __func__, family);
            break;
    }

    return str;
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
            LOGI("ares: got address of host %s, req_type: %s, h addr type = %s timeouts: %d\n",
                 hostent->h_name, addr_family_to_str(addr->req_addr_type),
                 addr_family_to_str(hostent->h_addrtype), timeouts);

            if (addr->req_addr_type != hostent->h_addrtype)
                return;

            for (i = 0; hostent->h_addr_list[i]; ++i)
            {
                inet_ntop(hostent->h_addrtype, hostent->h_addr_list[i], buf, INET6_ADDRSTRLEN);
                LOGI("Addr%d:[%s] %s\n", i, addr_family_to_str(hostent->h_addrtype), buf);
            }

            cnt = i;
            addr->h_addr_list = (char **) MALLOC(sizeof(char*) * (cnt + 1));

            for (i = 0; i < cnt; i++)
            {
                addr->h_addr_list[i] = (char *) MALLOC(sizeof(char) * hostent->h_length);
                memcpy(addr->h_addr_list[i], hostent->h_addr_list[i], hostent->h_length);
            }
            addr->h_addr_list[i] = NULL;
            addr->h_length = cnt;
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

    ipv6 = g_state.link.ipv6.is_ip && addr->ipv6_addr_list.state != CM2_ARES_R_IN_PROGRESS;
    ipv4 = g_state.link.ipv4.is_ip && addr->ipv4_addr_list.state != CM2_ARES_R_IN_PROGRESS;

    if ((g_state.link.ipv4.resolve_retry && ipv6 && addr->ipv6_addr_list.state == CM2_ARES_R_RESOLVED) ||
        (g_state.link.ipv6.resolve_retry && ipv4 && addr->ipv4_addr_list.state == CM2_ARES_R_RESOLVED))
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

void cm2_set_ipv6_pref(cm2_dest_e dst)
{
    switch(dst)
    {
        case CM2_DEST_REDIR:
            g_state.addr_redirector.ipv6_pref = true;
            break;
        case CM2_DEST_MANAGER:
            g_state.addr_manager.ipv6_pref = g_state.addr_redirector.ipv6_pref;
            break;
        default:
            return;
    }
}

static bool
cm2_validate_target_addr(cm2_addr_list *list, int addr_type)
{
    LOGT("type: %s, ipv4.blocked = %d ipv6.blocked = %d ipv4.is_ip = %d, ipv6.is_ip = %d, list index = %d",
          addr_family_to_str(addr_type), g_state.link.ipv4.blocked, g_state.link.ipv6.blocked,
          g_state.link.ipv4.is_ip, g_state.link.ipv6.is_ip,
          list->h_cur_idx);

    if (addr_type == AF_INET && (!g_state.link.ipv4.is_ip || g_state.link.ipv4.blocked)) {
        LOGI("Ares: Skip ipv4 address. IP active: %s link blocked: %s ",
             g_state.link.ipv4.is_ip ? "true" : "false",
             g_state.link.ipv4.blocked ? "true" : "false");
        return false;
    }

    if (addr_type == AF_INET6 && (!g_state.link.ipv6.is_ip || g_state.link.ipv6.blocked)) {
        LOGI("Ares: Skip ipv6 address. IP active: %s link blocked: %s ",
             g_state.link.ipv6.is_ip ? "true" : "false",
             g_state.link.ipv6.blocked ? "true" : "false");

        return false;
    }

    if (!list->h_addr_list) {
        LOGI("Ares: Addr Type: %s, Empty addr list", addr_family_to_str(addr_type));
        return false;
    }

    if (!list->h_addr_list[list->h_cur_idx]) {
        LOGI("Ares: Addr Type: %s. Empty addr for current index: %d",
             addr_family_to_str(addr_type), list->h_cur_idx);
        return false;
    }

    if (list->h_addrtype != addr_type) {
        LOGI("Address type mismatch: %s, %s",
             addr_family_to_str(list->h_addrtype), addr_family_to_str(addr_type));
        return false;
    }

    return true;
}

static bool
cm2_write_target_addr(cm2_addr_t *addr)
{
    const char     *result;
    char           target[256];
    char           *buf;
    int            ret;

    if (addr->ipv6_pref)
    {
        LOGI("ares: translating IPv6");
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

        g_state.ipv6_manager_con = true;
    } else {
        LOGI("ares: translating IPv4");
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

        g_state.ipv6_manager_con = false;
    }

    ret = cm2_ovsdb_set_Manager_target(target);
    if (ret)
        LOGI("trying to connect to: %s : %s", cm2_curr_dest_name(), target);

    return ret;
}

static bool cm2_pick_next_addr(cm2_addr_t *addr)
{
    bool is_ipv4_valid = cm2_validate_target_addr(&addr->ipv4_addr_list, AF_INET);
    bool is_ipv6_valid = cm2_validate_target_addr(&addr->ipv6_addr_list, AF_INET6);

    /**
     * @brief Decide on the connection type for the next address
     *
     * If no address is valid, return.
     *
     * If the planned connection type matches with what's capable of, then we go for it;
     * otherwise, we see what's available. If IPv6 is valid, we try IPv6. Same logic goes for IPv4.
     *
     * In general IPv6 is still preferred over IPv4, as seen from the order of if condition.
     */
    LOGD("ares: ipv6_pref: %d is ipv4 valid: %d is ipv6 valid: %d", addr->ipv6_pref, is_ipv4_valid, is_ipv6_valid);
    if (is_ipv4_valid == false && is_ipv6_valid == false) {
        LOGI("ares: No address available.");
        return false;
    }

    if ((addr->ipv6_pref == true && is_ipv6_valid)
     || (addr->ipv6_pref == false && is_ipv4_valid)) {
        LOGI("ares: Next address is found.");
        return true;
    }

    // We try whatever is valid
    if (is_ipv6_valid) {
        addr->ipv6_pref = true;
        LOGI("ares: changed to ipv6, ipv6_pref %d", addr->ipv6_pref);
    } else if (is_ipv4_valid) {
        addr->ipv6_pref = false;
        LOGI("ares: changed to ipv4, ipv6_pref %d", addr->ipv6_pref);
    }
    return true;
}

bool cm2_write_current_target_addr(void)
{
    cm2_addr_t *addr = cm2_curr_addr();
    LOGD("ares: %s target addr index ipv6: %d/%d ipv4: %d/%d", __func__
                                                             , addr->ipv6_addr_list.h_cur_idx, addr->ipv6_addr_list.h_length
                                                             , addr->ipv4_addr_list.h_cur_idx, addr->ipv4_addr_list.h_length);

    if (!cm2_pick_next_addr(addr)) return false;

    return cm2_write_target_addr(addr);
}

bool cm2_write_next_target_addr(void)
{
    cm2_addr_t *addr = cm2_curr_addr();
    LOGD("ares: %s", __func__);
    cm2_move_next_target_addr();

    if (!cm2_pick_next_addr(addr)) {
        return false;
    }
    return  cm2_write_target_addr(addr);
}

void cm2_move_next_target_addr(void)
{
    cm2_addr_t *addr = cm2_curr_addr();
    LOGD("ares: %s target addr index ipv6: %d/%d ipv4: %d/%d, ipv6_pref %d"
        , __func__
        , addr->ipv6_addr_list.h_cur_idx, addr->ipv6_addr_list.h_length
        , addr->ipv4_addr_list.h_cur_idx, addr->ipv4_addr_list.h_length
        , addr->ipv6_pref);

    if (addr->ipv6_pref)
        addr->ipv6_addr_list.h_cur_idx++;
    else
        addr->ipv4_addr_list.h_cur_idx++;

    // Flip it so everytime we tend to try a different one
    addr->ipv6_pref = !addr->ipv6_pref;
}

bool cm2_is_addr_resolved(const cm2_addr_t *addr)
{
    LOGI("Resolved state: ipv4: %d ipv6: %d", addr->ipv4_addr_list.state, addr->ipv6_addr_list.state);
    return addr->ipv4_addr_list.state != CM2_ARES_R_IN_PROGRESS && addr->ipv6_addr_list.state != CM2_ARES_R_IN_PROGRESS;
}

static void cm2_ares_timeout_user_cb(void)
{
    WARN_ON(!target_device_wdt_ping());
}

int cm2_start_cares(void)
{
    return evx_init_ares(g_state.loop, &g_state.eares, cm2_ares_timeout_user_cb);
}

void cm2_stop_cares(void)
{
    evx_stop_ares(&g_state.eares);
}
