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

#include <osw_ut.h>
#include <osw_drv_common.h>


OSW_UT(ow_steer_local_neigh_ut_typical_usage)
{
    struct ow_steer_local_neigh *self = &g_ow_steer_local_neigh;
    struct ow_steer_bm_observer *observer = &self->bm_observer;
    struct ow_steer_bm_group group = {
        .id = "example-group-id",
    };
    struct ow_steer_bm_bss bss = {
        .bssid = { .octet = { 0xd0, 0xab, 0x1e, 0xc0, 0xc0, 0xaa } },
        .group = &group,
    };
    struct ow_steer_bm_vif vif = {
        .vif_name = { .buf = "example-vif-name" },
        .bss = &bss,
        .group = &group,
    };
    ow_steer_local_neigh_init(self);

    observer->vif_added_fn(observer, &vif);
    OSW_UT_EVAL(ow_steer_local_neigh_lookup_group(group.id) != NULL);
    OSW_UT_EVAL(ow_steer_local_neigh_lookup_group_by_vif_name(vif.vif_name.buf) != NULL);
    OSW_UT_EVAL(ow_steer_local_neigh_lookup_neigh_in_group(group.id, vif.vif_name.buf) != NULL);

    struct ow_steer_local_neigh_vif_neigh *local_neigh = ow_steer_local_neigh_lookup_neigh_in_group(group.id, vif.vif_name.buf);
    bool is_neigh_complete = ow_steer_local_neigh_is_neigh_complete(local_neigh);
    OSW_UT_EVAL(is_neigh_complete == false);

    /* vif up */
    const struct osw_channel channel = { .control_freq_mhz = 2412 };
    const uint8_t op_class = 81;
    ow_steer_local_neigh_update_local_neigh_cache(group.id,
                                                  vif.vif_name.buf,
                                                  &bss.bssid,
                                                  &channel,
                                                  false,
                                                  0,
                                                  &op_class);
    is_neigh_complete = ow_steer_local_neigh_is_neigh_complete(local_neigh);
    OSW_UT_EVAL(is_neigh_complete == true);

    observer->vif_removed_fn(observer, &vif);
    OSW_UT_EVAL(ow_steer_local_neigh_lookup_group(group.id) == NULL);
    OSW_UT_EVAL(ow_steer_local_neigh_lookup_group_by_vif_name(vif.vif_name.buf) == NULL);
    OSW_UT_EVAL(ow_steer_local_neigh_lookup_neigh_in_group(group.id, vif.vif_name.buf) == NULL);
}
