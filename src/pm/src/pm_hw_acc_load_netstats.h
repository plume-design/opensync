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

#ifndef PM_HW_ACC_LOAD_NETSTATS_H_INCLUDED
#define PM_HW_ACC_LOAD_NETSTATS_H_INCLUDED

#include <stdint.h>

struct pm_hw_acc_load_netstats;

struct pm_hw_acc_load_netstats *pm_hw_acc_load_netstats_get_from_str(char *lines);
struct pm_hw_acc_load_netstats *pm_hw_acc_load_netstats_get(void);
void pm_hw_acc_load_netstats_drop(struct pm_hw_acc_load_netstats *stats);
void pm_hw_acc_load_netstats_compare(
        const struct pm_hw_acc_load_netstats *prev_stats,
        const struct pm_hw_acc_load_netstats *next_stats,
        uint64_t *max_tx_bytes,
        uint64_t *max_rx_bytes,
        uint64_t *max_tx_pkts,
        uint64_t *max_rx_pkts);

#endif /* PM_HW_ACC_LOAD_NETSTATS_H_INCLUDED */
