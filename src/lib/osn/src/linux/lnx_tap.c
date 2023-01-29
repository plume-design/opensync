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
#include "os.h"
#include "lnx_tap.h"

const char lnx_tap_port_create_cmd[] = _S(ip link add name "$1" type dummy);
const char lnx_tap_port_del_cmd[] = _S(ip link delete dev "$1");
const char lnx_tap_port_up_cmd[] = _S(ip link set dev "$1" up);
const char lnx_tap_port_down_cmd[] = _S(ip link set dev "$1" down);

static bool lnx_tap_interface_create(lnx_tap_t *self)
{
    int rc;

    rc = execsh_log(LOG_SEVERITY_NOTICE, lnx_tap_port_create_cmd, self->lt_ifname);
    if (rc != 0) return false;

    return true;
}

static bool lnx_tap_interface_delete(lnx_tap_t *self)
{
    int rc;

    rc = execsh_log(LOG_SEVERITY_NOTICE, lnx_tap_port_del_cmd, self->lt_ifname);
    if (rc != 0) return false;

    return true;
}

static void lnx_tap_interface_up(lnx_tap_t *self)
{
    execsh_log(LOG_SEVERITY_NOTICE, lnx_tap_port_up_cmd, self->lt_ifname);
}

static void lnx_tap_interface_down(lnx_tap_t *self)
{
    execsh_log(LOG_SEVERITY_NOTICE, lnx_tap_port_down_cmd, self->lt_ifname);
}

bool lnx_tap_init(lnx_tap_t *self, const char *ifname)
{
    int ret;
    memset(self, 0, sizeof(*self));

    ret = strscpy(self->lt_ifname, ifname, sizeof(self->lt_ifname));

    if ( ret < 0)
    {
        LOG(ERR, "%s(): %s: Interface name too long.", __func__, ifname);
        return false;
    }
    return true;
}

bool lnx_tap_fini(lnx_tap_t *self)
{
    TRACE();
    if (!self->lt_applied) return true;

    return lnx_tap_interface_delete(self);
}

bool lnx_tap_apply(lnx_tap_t *self)
{
    bool success;

    TRACE();
    self->lt_applied = true;

    /* delete the old interface, if it is present */
    lnx_tap_interface_down(self);
    lnx_tap_interface_delete(self);

    success = lnx_tap_interface_create(self);
    if (!success)
    {
        LOG(ERR, "%s(): Error creating TAP interface %s.", __func__, self->lt_ifname);
        return false;
    }
    lnx_tap_interface_up(self);

    return true;
}
