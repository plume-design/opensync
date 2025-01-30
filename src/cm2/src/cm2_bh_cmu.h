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

#ifndef CM2_BH_CMU_H_INCLUDED
#define CM2_BH_CMU_H_INCLUDED

#include <schema.h>
#include <ovsdb_update.h>

typedef struct cm2_bh_cmu cm2_bh_cmu_t;
typedef struct cm2_bh_cmu_vif cm2_bh_cmu_vif_t;
typedef struct cm2_bh_cmu_gre cm2_bh_cmu_gre_t;

void cm2_bh_cmu_vif_drop(cm2_bh_cmu_vif_t *vif);

cm2_bh_cmu_vif_t *cm2_bh_cmu_vif_alloc(cm2_bh_cmu_t *m, const char *vif_name);

void cm2_bh_cmu_vif_report_wvs_sta(cm2_bh_cmu_vif_t *vif, bool v);

void cm2_bh_cmu_vif_report_wvs_4addr(cm2_bh_cmu_vif_t *vif, bool v);

void cm2_bh_cmu_gre_drop(cm2_bh_cmu_gre_t *gre);

cm2_bh_cmu_gre_t *cm2_bh_cmu_gre_alloc(cm2_bh_cmu_t *m, const char *gre_name, const char *parent_name);

cm2_bh_cmu_t *cm2_bh_cmu_alloc(void);

cm2_bh_cmu_t *cm2_bh_cmu_from_list(const char *list);

void cm2_bh_cmu_drop(cm2_bh_cmu_t *m);

cm2_bh_cmu_vif_t *cm2_bh_cmu_lookup_vif(cm2_bh_cmu_t *m, const char *vif_name);

cm2_bh_cmu_gre_t *cm2_bh_cmu_lookup_gre(cm2_bh_cmu_t *m, const char *gre_name);

cm2_bh_cmu_gre_t *cm2_bh_cmu_lookup_gre_by_parent(cm2_bh_cmu_t *m, const char *parent_name);

void cm2_bh_cmu_WVS(
        cm2_bh_cmu_t *m,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_VIF_State *old_row,
        const struct schema_Wifi_VIF_State *new_row);

void cm2_bh_cmu_WMS(
        cm2_bh_cmu_t *m,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_Master_State *old_row,
        const struct schema_Wifi_Master_State *new_row);

void cm2_bh_cmu_CMU(
        cm2_bh_cmu_t *m,
        ovsdb_update_monitor_t *mon,
        const struct schema_Connection_Manager_Uplink *old_row,
        const struct schema_Connection_Manager_Uplink *new_row);

void cm2_bh_cmu_WIC(
        cm2_bh_cmu_t *m,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_Inet_Config *old_row,
        const struct schema_Wifi_Inet_Config *new_row);

#endif /* CM2_BH_CMU_H_INCLUDED */
