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

#ifndef PM_HW_ACC_LOAD_H_INCLUDED
#define PM_HW_ACC_LOAD_H_INCLUDED

enum pm_hw_acc_load_mode
{
    /* Initiatal state, does not imply anything */
    PM_HW_ACC_LOAD_INIT,

    /* The system is relatively idle and inactive. Therefore
     * the suggestion is to _disable_ the accelerator.
     */
    PM_HW_ACC_LOAD_INACTIVE,

    /* The system had reached a level of activity high
     * enough and is not considered busy. The suggestion is
     * to keep the acceleration _enabled_ while in this state.
     */
    PM_HW_ACC_LOAD_ACTIVE,

    /* The system that had reached a level of activity high
     * enough has started to settle down. This state is
     * transient and if the system maintains low activity,
     * the PM_HW_ACC_LOAD_INACTIVE will be entered. If the
     * system resumes high activity, PM_HW_ACC_LOAD_ACTIVE
     * will be re-entered.
     */
    PM_HW_ACC_LOAD_DEACTIVATING,
};

struct pm_hw_acc_load;

typedef void pm_hw_acc_load_updated_fn_t(void *priv);

const char *pm_hw_acc_load_mode_to_cstr(const enum pm_hw_acc_load_mode mode);
struct pm_hw_acc_load *pm_hw_acc_load_alloc(pm_hw_acc_load_updated_fn_t *updated_fn, void *priv);
enum pm_hw_acc_load_mode pm_hw_acc_load_mode_get(const struct pm_hw_acc_load *l);
void pm_hw_acc_load_drop(struct pm_hw_acc_load *l);

#endif /* PM_HW_ACC_LOAD_H_INCLUDED */
