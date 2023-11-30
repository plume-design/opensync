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

#include "osp_otbr.h"

bool osp_otbr_init(
        struct ev_loop *loop,
        osp_otbr_on_dataset_change_cb_t *on_dataset_change_cb,
        osp_otbr_on_network_topology_cb_t *on_network_topology_cb,
        osp_otbr_on_network_scan_result_cb_t *on_network_scan_result_cb)
{
    (void)loop;
    (void)on_dataset_change_cb;
    (void)on_network_topology_cb;
    (void)on_network_scan_result_cb;

    return true;
}

bool osp_otbr_start(const char *thread_iface, const char *network_iface, uint64_t *eui64, uint64_t *ext_addr)
{
    (void) thread_iface;
    (void) network_iface;

    *eui64 = 0;
    *ext_addr = 0;

    return true;
}

bool osp_otbr_set_report_interval(const int topology, const int discovery)
{
    (void) topology;
    (void) discovery;

    return true;
}

bool osp_otbr_create_network(const struct otbr_osp_network_params_s *params, struct osp_otbr_dataset_tlvs_s *dataset)
{
    (void) params;
    (void) dataset;

    return true;
}

bool osp_otbr_set_dataset(const struct osp_otbr_dataset_tlvs_s *dataset, bool active)
{
    (void) dataset;
    (void) active;

    return true;
}

bool osp_otbr_get_dataset(struct osp_otbr_dataset_tlvs_s *dataset, bool active)
{
    (void) dataset;
    (void) active;

    return true;
}

bool osp_otbr_set_thread_radio(bool enable)
{
    (void) enable;

    return true;
}

bool osp_otbr_stop(void)
{
    return true;
}

void osp_otbr_close(void)
{
    /* Nothing to do */
}
