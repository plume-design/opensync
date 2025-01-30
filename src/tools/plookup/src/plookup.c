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
#include <poll.h>
#include "kconfig.h"

#define DNS_PORT 53
#define LOCAL_HOST "127.0.0.1"
#define DEFAULT_TIMEOUT 5

struct name_server {
    const char *name;
    socklen_t len;
    union {
        struct sockaddr sa;
        struct sockaddr_in sin;
        struct sockaddr_in6 sin6;
    } u;
};

bool get_nameserver(struct name_server *ns, const char *addr)
{
    struct sockaddr_in sin = {0};
    struct sockaddr_in6 sin6 = {0};

    if (inet_pton(AF_INET, addr, &sin.sin_addr)) {
        sin.sin_family = AF_INET;
        sin.sin_port = htons(DNS_PORT);
        ns->u.sin = sin;
        ns->len = sizeof(sin);
        ns->name = addr;
        return true;
    } else if (inet_pton(AF_INET6, addr, &sin6.sin6_addr)) {
        sin6.sin6_family = AF_INET6;
        sin6.sin6_port = htons(DNS_PORT);
        ns->u.sin6 = sin6;
        ns->len = sizeof(sin6);
        ns->name = addr;
        return true;
    } else {
        LOGD("Invalid NS server address \"%s\": %s", addr, strerror(errno));
        return false;
    }
}

int send_query(struct name_server *ns, unsigned char *query, int qlen, unsigned char *reply, int rbuf_len, ssize_t *rlen)
{
    int fd;
    int retry = 2;
    int recvlen = 0;
    struct pollfd pfd = {0};
    struct sockaddr from = {0};
    socklen_t from_len = 0;
    unsigned int timeout = DEFAULT_TIMEOUT * 1000;

    from.sa_family = AF_INET;
    from_len = sizeof(struct sockaddr_in);

    fd = socket(from.sa_family, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;

    if (bind(fd, &from, from_len) < 0) {
        close(fd);
        return -1;
    }

    pfd.fd = fd;
    pfd.events = POLLIN;
    sendto(fd, query, qlen, MSG_NOSIGNAL, &ns->u.sa, ns->len);
    poll(&pfd, 1, timeout);
    while (1) {
        recvlen = recvfrom(fd, reply, rbuf_len, 0, &from, &from_len);
        if (recvlen < 0)
            break;

        switch (reply[3] & 15) {
            case 0:
            case 3:
                break;
            case 2:
                if (retry--)
                    sendto(fd, query, qlen, MSG_NOSIGNAL, &ns->u.sa, ns->len);
                /* fall through */
            default:
                continue;
        }

        *rlen = recvlen;

        close(fd);
        return 0;
    }

    close(fd);
    return -1;
}

int parse_reply(const unsigned char *msg, size_t len)
{
    ns_msg handle;
    ns_rr res_record;
    int i, rdlen;
	int ai_family = 0;
    char addr_str[INET6_ADDRSTRLEN];

    if (ns_initparse(msg, len, &handle) != 0)
        return -1;

    for (i = 0; i < ns_msg_count(handle, ns_s_an); i++) {
        if (ns_parserr(&handle, ns_s_an, i, &res_record) != 0)
            return -1;

        rdlen = ns_rr_rdlen(res_record);

        switch (ns_rr_type(res_record))
        {
            case ns_t_a:
                if (rdlen != 4) return -1;
	            ai_family = AF_INET;
                break;
            case ns_t_aaaa:
                if (rdlen != 16) return -1;
	            ai_family = AF_INET6;
                break;
            default:
                break;
        }
        if (inet_ntop(ai_family, ns_rr_rdata(res_record), addr_str, sizeof(addr_str)))
            LOGD("Resolved address: %s", addr_str);
    }

    return i;
}

bool lookup_hostname(char *hostname, struct name_server *ns, int type)
{
    ssize_t qlen = 0;
    ssize_t rlen = 0;
    unsigned char query[512] = {0};
    unsigned char reply[512] = {0};

    qlen = res_mkquery(QUERY, hostname, C_IN, type, NULL, 0, NULL, query, sizeof(query));
    if (qlen < 0) {
        LOGN("Failed to construct query for hostname:%s, type:%d [%s]",
            hostname, type, strerror(errno));
        return false;
    }

    if (send_query(ns, query, qlen, reply, sizeof(reply), &rlen) < 0) {
        LOGN("Failed to send query for hostname:%s, type:%d [%s]",
            hostname, type, strerror(errno));
        return false;
    }

    if (parse_reply(reply, rlen) < 0)
        return false;

    return true;
}

int usage()
{
    LOGI("Usage: plookup HOST [NAMESERVER]\n");
    return 0;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    struct name_server ns = {0};

    target_log_open("PLOOKUP", LOG_OPEN_STDOUT);

    log_severity_set(LOG_SEVERITY_INFO);
    if (argc == 2 || argc == 3) {
        if (argc == 3) {
            if (get_nameserver(&ns, argv[2]))
                LOGD("Using non-default nameserver: %s", argv[2]);
        } else {
            get_nameserver(&ns, LOCAL_HOST);
        }

        if (!lookup_hostname(argv[1], &ns, T_A)) {
            LOGN("Failed to obtain A record for %s", argv[1]);
            ret++;
        }

        if (!lookup_hostname(argv[1], &ns, T_AAAA)) {
            LOGN("Failed to obtain AAAA record for %s", argv[1]);
            ret++;
        }

        /* Both queries failed */
        if (ret == 2) {
            LOGE("Unable to resolve: %s [%s]", argv[1], strerror(errno));
            return -1;
        }

        return 0;
    } else {
        usage();
    }

    return ret;
}
