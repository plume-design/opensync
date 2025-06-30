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

#ifndef PM_HW_ACC_LOAD_CPUSTATS_H_INCLUDED
#define PM_HW_ACC_LOAD_CPUSTATS_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct pm_hw_acc_load_cpustats;
struct pm_hw_acc_load_cpuload;

struct pm_hw_acc_load_cpustats *pm_hw_acc_load_cpustats_get_from_str(char *lines, const size_t cpu_init);
struct pm_hw_acc_load_cpustats *pm_hw_acc_load_cpustats_get(const size_t cpu_init);
size_t pm_hw_acc_load_cpustats_get_len(struct pm_hw_acc_load_cpustats *cpu);
void pm_hw_acc_load_cpustats_drop(struct pm_hw_acc_load_cpustats *cpu);
void pm_hw_acc_load_cpustats_compare(
        const struct pm_hw_acc_load_cpustats *prev_stats,
        const struct pm_hw_acc_load_cpustats *next_stats,
        struct pm_hw_acc_load_cpuload *cpu);
void pm_hw_acc_load_cpustats_expand(
        struct pm_hw_acc_load_cpuload *cl,
        struct pm_hw_acc_load_cpustats *prev,
        const struct pm_hw_acc_load_cpustats *next);
bool pm_hw_acc_load_cpustats_need_more_space(
        const struct pm_hw_acc_load_cpustats *prev,
        const struct pm_hw_acc_load_cpustats *next);
unsigned pm_hw_acc_load_compute_max_cpu_util(const struct pm_hw_acc_load_cpuload *l);

struct pm_hw_acc_load_cpuload *pm_hw_acc_load_cpuload_alloc(void);
void pm_hw_acc_load_cpuload_drop(struct pm_hw_acc_load_cpuload *cpu);
void pm_hw_acc_load_cpuload_extend(struct pm_hw_acc_load_cpuload **cpu, const size_t new_len);
size_t pm_hw_acc_load_cpuload_get_len(struct pm_hw_acc_load_cpuload *cpu);

#endif /* PM_HW_ACC_LOAD_CPUSTATS_H_INCLUDED */
