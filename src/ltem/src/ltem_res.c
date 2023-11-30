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

#include <stdio.h>
#include <string.h>
#include <resolv.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

#include "log.h"         // logging routines
#include "json_util.h"   // json routines
#include "os.h"          // OS helpers
#include "target.h"      // target API
#include "network_metadata.h"  // network metadata API

#include "ltem_mgr.h"

char *dns_server_list[] =
{
    "8.8.8.8",
    "9.9.9.9",
    "208.67.222.222",
    "1.1.1.1",
    "185.228.168.9",
    "76.76.19.19",
    "94.140.14.14",
    "84.200.69.80",
    "8.26.56.26",
    "205.171.3.65",
    "195.46.39.39",
    "216.146.35.35",
    "77.88.8.8",
    "74.82.42.42",
    "64.6.64.6",
    "76.76.2.0",
};

#define dns_server_list_size (sizeof(dns_server_list)/ sizeof(char *))

int next_dns_server;

int
ltem_check_dns(const char *server, char *hostname)
{
    int rc;
    int i;
    int save_nscount;
    struct in_addr save_addrs[MAXNS];
    struct sockaddr_in addr;
    struct addrinfo *result = NULL;
    struct addrinfo hint;

    memset(&hint, 0 , sizeof(hint));

    save_nscount = _res.nscount;
    for (i = 0; i < MAXNS; i++)
    {
        save_addrs[i] = _res.nsaddr_list[i].sin_addr;
    }

    res_init();
    addr.sin_family = AF_INET;
    inet_aton(server, &addr.sin_addr);
    _res.nscount = 1;
    _res.nsaddr_list[0].sin_family = addr.sin_family;
    _res.nsaddr_list[0].sin_addr = addr.sin_addr;
    _res.nsaddr_list[0].sin_port = htons(53);
    rc = getaddrinfo(hostname, NULL, &hint, &result);

    /* restore nsaddr_list */
    for (i = 0; i < save_nscount; i++)
    {
        _res.nsaddr_list[i].sin_addr = save_addrs[i];
        _res.nscount = save_nscount;
    }

    return rc;
}

const char *
ltem_get_next_dns_server(void)
{
    char *server;

    server = dns_server_list[next_dns_server++];
    if (next_dns_server == dns_server_list_size)
    {
        next_dns_server = 0;
    }

    return server;
}

int
ltem_dns_connect_check(char *if_name)
{
    int res;
    int s;
    const char *dns_server;
    struct sockaddr_in addr;
    struct ifreq ifr;
    time_t start, end;

    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    dns_server = ltem_get_next_dns_server();
    if (dns_server == NULL)
    {
        LOGI("%s: NULL dns_server", __func__);
        return -1;
    }
    inet_aton(dns_server, &addr.sin_addr);

    memset(&ifr, 0, sizeof(ifr));
    memcpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name));
    res = setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, (void *)&ifr, sizeof(ifr));
    if (res)
    {
        LOGI("%s: setsockopt failed: res=%d, err=%s", __func__, res, strerror(errno));
        close(s);
        return -1;
    }

    start = time(NULL);
    res = connect(s, (struct sockaddr *)&addr, sizeof(addr));
    end = time(NULL);
    if (res)
    {
        LOGI("%s: connect to %s failed: res=%d, err=%s, time[%ld]", __func__, dns_server, res, strerror(errno), end - start);
        close(s);
        return -1;
    }

    sleep(1);
    close(s);

    return 0;
}

int
ltem_wan_healthcheck(ltem_mgr_t *mgr)
{
    target_connectivity_check_t cstate = {0};
    target_connectivity_check_option_t opts;
    time_t start, end;
    bool ret;

    if (mgr->lte_config_info->if_name[0] == '\0') return 0;

    if (!mgr->lte_state_info->lte_failover_active && mgr->wan_state == LTEM_WAN_STATE_UP)
    {
        opts = (INTERNET_CHECK | FAST_CHECK | IPV4_CHECK);
        start = time(NULL);
        ret = target_device_connectivity_check(mgr->lte_route->wan_if_name, &cstate, opts);
        end = time(NULL);
        LOGI("%s: ping time[%ld]", __func__, end - start);
        if (!ret)
        {
            mgr->wan_failure_count++;
            mgr->wan_l3_reconnect_success = 0;
            LOGI("%s: Failed, count[%d]", __func__, mgr->wan_failure_count);
            return -1;
        }
        else
        {
            mgr->last_wan_healthcheck_success = time(NULL);
            mgr->wan_failure_count = 0;
            LOGI("%s: Success", __func__);
        }
    }

    return 0;
}
