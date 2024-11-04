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

#ifndef CM2_BH_GRE_H_INCLUDED
#define CM2_BH_GRE_H_INCLUDED

#include <schema.h>
#include <ovsdb_update.h>

struct cm2_bh_gre;
struct cm2_bh_gre_vif;
struct cm2_bh_gre_tun;

typedef struct cm2_bh_gre cm2_bh_gre_t;
typedef struct cm2_bh_gre_vif cm2_bh_gre_vif_t;
typedef struct cm2_bh_gre_tun cm2_bh_gre_tun_t;

cm2_bh_gre_t *cm2_bh_gre_alloc(void);

cm2_bh_gre_t *cm2_bh_gre_from_list(const char *list);

void cm2_bh_gre_drop(cm2_bh_gre_t *m);

cm2_bh_gre_vif_t *cm2_bh_gre_vif_alloc(cm2_bh_gre_t *m, const char *vif_name);

void cm2_bh_gre_vif_drop(cm2_bh_gre_vif_t *vif);

cm2_bh_gre_vif_t *cm2_bh_gre_lookup_vif(cm2_bh_gre_t *m, const char *vif_name);

cm2_bh_gre_tun_t *cm2_bh_gre_tun_alloc(cm2_bh_gre_vif_t *vif, const char *tun_name);

void cm2_bh_gre_tun_drop(cm2_bh_gre_tun_t *tun);

cm2_bh_gre_tun_t *cm2_bh_gre_lookup_tun(cm2_bh_gre_t *m, const char *tun_name);

void cm2_bh_gre_WIS(
        cm2_bh_gre_t *m,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_Inet_State *old_row,
        const struct schema_Wifi_Inet_State *new_row);

#endif /* CM2_BH_GRE_H_INCLUDED */
