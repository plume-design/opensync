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
#include "memutil.h"

#include "osn_bridge.h"
#include "linux/lnx_bridge.h"
#include "linux/lnx_vlan.h"


bool osn_bridge_del(char *brname)
{
    LOGT("%s(): deleting bridge %s", __func__, brname);
    return lnx_bridge_del(brname);
}

bool osn_bridge_create(char *brname)
{
    LOGT("%s(): creating bridge %s", __func__, brname);
    return lnx_bridge_create(brname);
}

bool osn_bridge_del_port(char *brname, char *portname)
{
    return lnx_bridge_del_port(brname, portname);
}

bool osn_bridge_add_port(char *brname, char *portname)
{
    return lnx_bridge_add_port(brname, portname);
}

bool osn_bridge_set_hairpin(char *port, bool enable)
{
    return lnx_bridge_set_hairpin(port, enable);
}
