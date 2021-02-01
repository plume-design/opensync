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

#include "log.h"

#include "linux/upnp_server.h"
#include "linux/mupnp_server.h"
#include "linux/mupnp_cfg_wan.h"
#include "linux/mupnp_cfg_iptv.h"

upnp_server_t *upnp_server_new(enum upnp_srv_id id)
{
    upnp_server_t *object = NULL;
    switch(id)
    {
        case UPNP_ID_WAN:
            object = mupnp_server_new(mupnp_cfg_wan());
            break;
        case UPNP_ID_IPTV:
            object = mupnp_server_new(mupnp_cfg_iptv());
            break;
        
        // dummy case to generate incomplete enum switch error
        case UPNP_ID_COUNT:
            break;
    }

    if (NULL == object)
    {
        LOG(ERR, "upnp: Error creating UPNP server of id=%d.", id);
    }
    return object;
}

void upnp_server_del(upnp_server_t *self)
{
    mupnp_server_del(self);
}

bool upnp_server_attach_external(upnp_server_t *self, const char *ifname)
{
    return mupnp_server_attach_external(self, ifname);
}

bool upnp_server_attach_external6(upnp_server_t *self, const char *ifname)
{
    return mupnp_server_attach_external6(self, ifname);
}

bool upnp_server_attach_internal(upnp_server_t *self, const char *ifname)
{
    return mupnp_server_attach_internal(self, ifname);
}

bool upnp_server_detach(upnp_server_t *self, const char *ifname)
{
    return mupnp_server_detach(self, ifname);
}

bool upnp_server_start(upnp_server_t *self)
{
    return mupnp_server_start(self);
}

bool upnp_server_stop(upnp_server_t *self)
{
    return mupnp_server_stop(self);
}

bool upnp_server_started(const upnp_server_t *self)
{
    return mupnp_server_started(self);
}
