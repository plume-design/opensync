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

/**
 * Simple nslookup like utility designed to handle both ipv4 and ipv6 addresses
 * and lookup queries.
 */
#include <arpa/inet.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <resolv.h>
#include <errno.h>
#include "log.h"
#include "target.h"


#define DNS_PORT 53
bool ipv4 = false;

bool lookup_init(char *server)
{
    in_addr_t server_addr;
    struct in6_addr server_addr6;

    res_init();

    if (inet_pton(AF_INET, server, &server_addr))
    {
        _res.nscount                            = 1;
        _res.nsaddr_list[0].sin_port            = htons(DNS_PORT);
        _res.nsaddr_list[0].sin_family          = AF_INET;  // IPv4 only
        _res.nsaddr_list[0].sin_addr.s_addr     = server_addr;
        ipv4 = true;
    }
    else if (inet_pton(AF_INET6, server, &server_addr6))
    {
        if (_res._u._ext.nsaddrs[0] == NULL ||  _res._u._ext.nsaddrs[0]->sin6_family != AF_INET6)
	{
            LOGE("IPV6 nameserver is not available, can not use non-default nameserver %s\n", server);
            return false;
        }

        _res._u._ext.nscount                    = 1;
        _res._u._ext.nsaddrs[0]->sin6_addr      = server_addr6;
        _res._u._ext.nsaddrs[0]->sin6_port      = htons(DNS_PORT);
        _res._u._ext.nsaddrs[0]->sin6_family    = AF_INET6;  // IPv6 only
    }
    else
    {
        LOGE("Invalid nameserver IP address: %s\n", server);
        return false;
    }

    LOGD("Using non-default nameserver: %s\n", server);
    return true;
}

int lookup_hostname(char *hostname)
{
    int ret;
    struct addrinfo hints;
    struct addrinfo *res, *p;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family     = AF_UNSPEC;

    if ((ipv4) && (_res._u._ext.nsaddrs[0] != NULL))
    {
        if (_res._u._ext.nsaddrs[0]->sin6_family == AF_INET6)
            memset(_res._u._ext.nsaddrs[0], 0, sizeof(struct sockaddr_in6));
    }

    hints.ai_socktype   = SOCK_STREAM;
    hints.ai_flags      = AI_PASSIVE;

    res = NULL;
    ret = getaddrinfo(hostname, NULL, &hints, &res);
    if (ret != 0)
    {
        LOGE("Unable to resolve: %s [%s]\n", hostname, strerror(errno));
        goto error;
    }

    for (p = res; p != NULL; p = p->ai_next)
    {
        void *addr;
        char addr_str[1024];

        if (p->ai_family == AF_INET)
        {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
        }
        else
        {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
        }

        inet_ntop(p->ai_family, addr, addr_str, sizeof(addr_str));
        LOGD("Resolved address: %s\n", addr_str);
    }

error:
    if (res)
        freeaddrinfo(res);

    return ret;
}

int usage()
{
    LOGI("Usage: plookup HOST [NAMESERVER]\n");
    return 0;
}


int main(int argc, char *argv[])
{
    target_log_open("PLOOKUP", LOG_OPEN_STDOUT);

    log_severity_set(LOG_SEVERITY_INFO);

    int ret = 1;

    if (argc == 2 || argc == 3)
    {
        if (argc == 3)
        {
            lookup_init(argv[2]);
        }

        ret = lookup_hostname(argv[1]);
    }
    else
    {
        usage();
    }

    return ret;
}
