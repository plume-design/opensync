/*
 * Copyright (c) 2018  Joachim Nilsson <troglobit@gmail.com>
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

#include <ifaddrs.h>
#include <netdb.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "test_mdnsd.h"
#include "util.h"

#ifndef IN_ZERONET
#define IN_ZERONET(addr) ((addr & IN_CLASSA_NET) == 0)
#endif

#ifndef IN_LOOPBACK
#define IN_LOOPBACK(addr) ((addr & IN_CLASSA_NET) == 0x7f000000)
#endif

#ifndef IN_LINKLOCAL
#define IN_LINKLOCALNETNUM 0xa9fe0000
#define IN_LINKLOCAL(addr) ((addr & IN_CLASSB_NET) == IN_LINKLOCALNETNUM)
#endif

/* Find default outbound *LAN* interface, i.e. skipping tunnels */
static char *getifname(char *ifname, size_t len)
{
	uint32_t dest, gw, mask;
	char buf[256], name[17];
	FILE *fp;
	int rc, flags, cnt, use, metric, mtu, win, irtt;
	int found = 0;

	fp = fopen("/proc/net/route", "r");
	if (!fp)
		return NULL;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		rc = sscanf(buf, "%16s %X %X %X %d %d %d %X %d %d %d\n",
			   name, &dest, &gw, &flags, &cnt, &use, &metric,
			   &mask, &mtu, &win, &irtt);
			
		if (rc < 10 || !(flags & 1)) /* IFF_UP */
			continue;

		if (dest != 0 || mask != 0)
			continue;

		if (!ifname[0] || !strncmp(ifname, "tun", 3)) {
			strscpy(ifname, name, len);
			found = 1;
			break;
		}
	}
	fclose(fp);

	if (found)
		return ifname;

	return NULL;
}

/* Check if valid address */
static int valid_addr(struct in_addr *ina)
{
	in_addr_t addr;

	addr = ntohl(ina->s_addr);
	if (IN_ZERONET(addr) || IN_LOOPBACK(addr) || IN_LINKLOCAL(addr))
		return 0;

	return 1;
}

/* Find IPv4 address of default outbound LAN interface */
int getaddr(char *iface, struct in_addr *ina)
{
	struct ifaddrs *ifaddr, *ifa;
	char ifname[17] = { 0 };
	char buf[20] = { 0 };
	int rc = -1;

	if (!iface)
		iface = getifname(ifname, sizeof(ifname));
	DBG("Default interface: %s", iface ? iface : "N/A");

	rc = getifaddrs(&ifaddr);
	if (rc)
		return -1;

	for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr)
			continue;

		if (ifa->ifa_flags & IFF_LOOPBACK)
			continue;

		if (!(ifa->ifa_flags & IFF_MULTICAST))
			continue;

		if (ifa->ifa_addr->sa_family != AF_INET)
			continue;

		if (iface && strcmp(iface, ifa->ifa_name))
			continue;

		rc = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
				 buf, sizeof(buf), NULL, 0, NI_NUMERICHOST);
		if (!rc) {
			if (!inet_aton(buf, ina))
				continue;
			if (!valid_addr(ina))
				continue;

			DBG("Found interface %s, address %s", ifa->ifa_name, buf);
			break;
		}
	}
	freeifaddrs(ifaddr);

	if (rc || IN_ZERONET(ntohl(ina->s_addr)))
		return -1;

	DBG("Using address: %s", inet_ntoa(*ina));

	return 0;
}
