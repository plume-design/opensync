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

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "ds_tree.h"

/**
 * Initialize l2switch subsystem
 * @return true on success
 */
bool osp_l2switch_init(void)
{
    return true;
}

/**
 * Create the vlan config for the iface
 * @param[in] ifname
 * @return true on success
 */
bool osp_l2switch_new(char *ifname)
{
    return true;
}

/**
 * Delete vlan configuration object for an interface
 */
void osp_l2switch_del(char *ifname)
{
    return;
}

/**
 * Set the port's vlanid
 * @return true on success
 */
bool osp_l2switch_vlan_set(char *ifname, const int32_t vlan, bool tagged)
{
    return true;
}

/**
 * Remove port's vlanid
 * @return true on success
 */
bool osp_l2switch_vlan_unset(char *ifname, const int32_t vlan)
{
    return true;
}

/**
 * Apply the vlan settings for the interface
 * @return true on success
 */
bool osp_l2switch_apply(char *ifname)
{
    return true;
}
