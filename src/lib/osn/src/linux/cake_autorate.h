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

#ifndef CAKE_AUTORATE_H_INCLUDED
#define CAKE_AUTORATE_H_INCLUDED

#include <stdbool.h>

#include "ds_dlist.h"
#include "ds_map_str.h"
#include "execsh.h"
#include "osn_types.h"

typedef struct cake_autorate cake_autorate_t;

struct cake_autorate
{
    char *DL_ifname;
    char *UL_ifname;

    bool DL_shaper_adjust;
    int DL_min_rate;
    int DL_base_rate;
    int DL_max_rate;

    bool UL_shaper_adjust;
    int UL_min_rate;
    int UL_base_rate;
    int UL_max_rate;

    ds_dlist_t reflectors; /* custom reflectors list */

    bool rand_reflectors; /* enable or disable randomization of reflectors at startup */
    int ping_interval;    /* Interval time for ping, milliseconds */

    int num_pingers; /* number of pingers to maintain */

    int active_thresh; /* threshold in Kbit/s below which dl/ul is considered idle */

    ds_map_str_t *other_config;

    execsh_async_t autorate_script;
};

bool cake_autorate_init(cake_autorate_t *self, const char *DL_ifname, const char *UL_ifname);

bool cake_autorate_DL_shaper_adjust_set(cake_autorate_t *self, bool shaper_adjust);

bool cake_autorate_DL_shaper_params_set(cake_autorate_t *self, int min_rate, int base_rate, int max_rate);

bool cake_autorate_UL_shaper_adjust_set(cake_autorate_t *self, bool shaper_adjust);

bool cake_autorate_UL_shaper_params_set(cake_autorate_t *self, int min_rate, int base_rate, int max_rate);

bool cake_autorate_reflector_add(cake_autorate_t *self, const osn_ipany_addr_t *ip_addr);

bool cake_autorate_reflector_list_add(cake_autorate_t *self, const osn_ipany_addr_t ip_addr_list[], int num);

void cake_autorate_reflector_list_clear(cake_autorate_t *self);

bool cake_autorate_reflectors_randomize_set(cake_autorate_t *self, bool randomize);

bool cake_autorate_reflectors_ping_interval_set(cake_autorate_t *self, int ping_interval);

bool cake_autorate_num_pingers_set(cake_autorate_t *self, int num_pingers);

bool cake_autorate_active_threshold_set(cake_autorate_t *self, int threshold);

bool cake_autorate_other_config_set(cake_autorate_t *self, const ds_map_str_t *other_config);

bool cake_autorate_apply(cake_autorate_t *self);

bool cake_autorate_fini(cake_autorate_t *self);

#endif /* CAKE_AUTORATE_H_INCLUDED */
