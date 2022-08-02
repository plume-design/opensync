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

#include "ds.h"
#include "execsh.h"
#include "log.h"
#include "util.h"

#include "lnx_lte.h"

/**
 * Controls IPv6 support for a given network interface.
 *
 * @param[in]   if_name if_name is a network interface name for which IPv6
 *                      support needs to be controlled
 * @param[cmd]  cmd     Used to Disable(1) or Enable(0) IPv6 support
 *
 */
void lnx_lte_ip6_control(const char *ifname, int cmd)
{
    FILE *fp;
    char ipv6_file[128];

    snprintf(ipv6_file, sizeof(ipv6_file),
            "/proc/sys/net/ipv6/conf/%s/disable_ipv6", ifname);
    fp = fopen(ipv6_file, "w");
    if (fp == NULL)
    {
       LOGE( "lte: %s: Error opening the file ",
                ipv6_file);
       return;
    }

    LOGI("%s: IPv6 support for LTE(%s) is  %s", __func__, ifname,
                    cmd ? "Disabled" : "Enabled");
    fprintf(fp, "%d", cmd);
    fclose(fp);
}


bool lnx_lte_init(lnx_lte_t *self, const char *ifname)
{
    memset(self, 0, sizeof(*self));

    LOGI("%s: New LTE %s", __func__, ifname);
    if (strscpy(self->ll_ifname, ifname, sizeof(self->ll_ifname)) < 0)
    {
        LOG(ERR, "lte: %s: Interface name too long.", ifname);
        return false;
    }
    /* Disable IPv6 support for LTE interface*/
    lnx_lte_ip6_control(ifname, 1);
    self->ll_applied = true;
    return true;
}

bool lnx_lte_fini(lnx_lte_t *self)
{
    if (!self->ll_applied) return true;

    LOGI("%s: stop LTE daemon", __func__);
    daemon_fini(&self->ll_lted);
    return true;
}

