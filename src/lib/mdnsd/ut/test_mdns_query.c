/*
 * Copyright (c) 2003  Jeremie Miller <jer@jabber.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "mdnsd.h"

void mdnsd_conflict(char *name, int type, void *arg)
{
	ERR("conflicting name detected %s for type %d, dropping record ...", name, type);
}

/* Print an answer */
static int ans(mdns_answer_t *a, void *arg)
{
	int now;

	if (a->ttl == 0)
		now = 0;
	else
		now = a->ttl - time(0);

	switch (a->type) {
	case QTYPE_A:
		printf("A %s for %d seconds to ip %s\n", a->name, now, inet_ntoa(a->ip));
		break;

	case QTYPE_PTR:
		printf("PTR %s for %d seconds to %s\n", a->name, now, a->rdname);
		break;

	case QTYPE_SRV:
		printf("SRV %s for %d seconds to %s:%d\n", a->name, now, a->rdname, a->srv.port);
		break;

	default:
		printf("%d %s for %d seconds with %d data\n", a->type, a->name, now, a->rdlen);
	}

	return 0;
}

/* Create multicast 224.0.0.251:5353 socket */
static int msock(void)
{
	int s, flag = 1;
	struct sockaddr_in in;
	struct ip_mreq mc;

	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;
	in.sin_port = htons(5353);
	in.sin_addr.s_addr = 0;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		return 0;

#ifdef SO_REUSEPORT
	setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (char *)&flag, sizeof(flag));
#endif
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof(flag));
	if (bind(s, (struct sockaddr *)&in, sizeof(in))) {
		close(s);
		return 0;
	}

	mc.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
	mc.imr_interface.s_addr = htonl(INADDR_ANY);
	setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mc, sizeof(mc));

	flag = fcntl(s, F_GETFL, 0);
	flag |= O_NONBLOCK;
	fcntl(s, F_SETFL, flag);

	return s;
}

static int usage(int code)
{
	/* mquery 12 _http._tcp.local. */
	printf("usage: mquery [-t TYPE] [NAME]\n");
	return code;
}

int main(int argc, char *argv[])
{
	mdns_daemon_t *d;
	struct message m;
	unsigned long int ip;
	unsigned short int port;
	struct timeval *tv;
	ssize_t bsize;
	socklen_t ssize;
	unsigned char buf[MAX_PACKET_LEN];
	struct sockaddr_in from, to;
	fd_set fds;
	char *name = DISCO_NAME;
	int type = QTYPE_PTR;	/* 12 */
	int s, c;

	while ((c = getopt(argc, argv, "h?t:")) != EOF) {
		switch (c) {
		case 'h':
		case '?':
			return usage(0);

		case 't':
			type = atoi(optarg);
			break;

		default:
			return usage(1);
		}
	}

	if (optind < argc)
		name = argv[optind];

	d = mdnsd_new(1, 1000);
	if ((s = msock()) == 0) {
		printf("Failed creating multicast socket: %s\n", strerror(errno));
		return 1;
	}

	printf("Querying type %d for %s ...\n", type, name);
	mdnsd_query(d, name, type, ans, NULL);

	while (1) {
		tv = mdnsd_sleep(d);
		FD_ZERO(&fds);
		FD_SET(s, &fds);
		select(s + 1, &fds, 0, 0, tv);

		if (FD_ISSET(s, &fds)) {
			ssize = sizeof(struct sockaddr_in);
			while ((bsize = recvfrom(s, buf, MAX_PACKET_LEN, 0, (struct sockaddr *)&from, &ssize)) > 0) {
				memset(&m, 0, sizeof(struct message));
				message_parse(&m, buf);
				mdnsd_in(d, &m, (unsigned long int)from.sin_addr.s_addr, from.sin_port);
			}
			if (bsize < 0 && errno != EAGAIN) {
				printf("Failed reading from socket %d: %s\n", errno, strerror(errno));
				return 1;
			}
		}

		while (mdnsd_out(d, &m, &ip, &port)) {
			memset(&to, 0, sizeof(to));
			to.sin_family = AF_INET;
			to.sin_port = port;
			to.sin_addr.s_addr = ip;
			if (sendto(s, message_packet(&m), message_packet_len(&m), 0, (struct sockaddr *)&to,
				   sizeof(struct sockaddr_in)) != message_packet_len(&m)) {
				printf("Failed writing to socket: %s\n", strerror(errno));
				return 1;
			}
		}
	}

	mdnsd_shutdown(d);
	mdnsd_free(d);

	return 0;
}
