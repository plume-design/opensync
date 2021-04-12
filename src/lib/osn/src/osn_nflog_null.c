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

#include <asm/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/netfilter/nfnetlink_log.h>

#include <errno.h>
#include <unistd.h>
#include <ev.h>

#include "osn_nflog.h"
#include "osn_types.h"

/*
 * ===========================================================================
 *  Public API implementation
 * ===========================================================================
 */
osn_nflog_t *osn_nflog_new(int nl_group, osn_nflog_fn_t *fn)
{
    (void)nl_group;
    (void)fn;

    return (osn_nflog_t *)0xdeadbeef;
}

bool osn_nflog_start(osn_nflog_t *self)
{
    (void)self;
    return true;
}

void osn_nflog_stop(osn_nflog_t *self)
{
    (void)self;
}

void osn_nflog_del(osn_nflog_t *self)
{
    (void)self;
}
