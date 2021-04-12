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
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>

#include "log.h"         // logging routines
#include "json_util.h"   // json routines
#include "os.h"          // OS helpers
#include "target.h"      // target API
#include "network_metadata.h"  // network metadata API

#include "ltem_mgr.h"

int
ltem_check_dns(char *server, char *hostname)
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
