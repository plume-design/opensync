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

#ifndef OW_OVSDB_WPS_H
#define OW_OVSDB_WPS_H

#include <ovsdb.h>
#include <ovsdb_table.h>
#include <ovsdb_cache.h>

struct ow_ovsdb_wps_ops;

typedef void
ow_ovsdb_wps_changed_fn_t(void *fn_priv,
                          const char *vif_name);

struct ow_ovsdb_wps_changed;

typedef void
ow_ovsdb_wps_set_vconf_table_fn_t(struct ow_ovsdb_wps_ops *ops,
                                  ovsdb_table_t *table_Wifi_VIF_Config);

typedef struct ow_ovsdb_wps_changed *
ow_ovsdb_wps_add_changed_fn_t(struct ow_ovsdb_wps_ops *ops,
                              ow_ovsdb_wps_changed_fn_t *fn,
                              void *fn_priv);

typedef void
ow_ovsdb_wps_del_changed_fn_t(struct ow_ovsdb_wps_ops *ops,
                              struct ow_ovsdb_wps_changed *w);

typedef void
ow_ovsdb_wps_handle_vconf_update_fn_t(struct ow_ovsdb_wps_ops *ops,
                                      ovsdb_update_monitor_t *mon,
                                      struct schema_Wifi_VIF_Config *old,
                                      struct schema_Wifi_VIF_Config *new,
                                      ovsdb_cache_row_t *row);

typedef void
ow_ovsdb_wps_fill_vstate_fn_t(struct ow_ovsdb_wps_ops *ops,
                              struct schema_Wifi_VIF_State *vstate);

struct ow_ovsdb_wps_ops {
    ow_ovsdb_wps_set_vconf_table_fn_t *set_vconf_table_fn;
    ow_ovsdb_wps_add_changed_fn_t *add_changed_fn;
    ow_ovsdb_wps_del_changed_fn_t *del_changed_fn;
    ow_ovsdb_wps_handle_vconf_update_fn_t *handle_vconf_update_fn;
    ow_ovsdb_wps_fill_vstate_fn_t *fill_vstate_fn;
};

static inline void
ow_ovsdb_wps_op_set_vconf_table(struct ow_ovsdb_wps_ops *ops,
                                ovsdb_table_t *table)
{
    if (ops == NULL) return;
    if (WARN_ON(ops->set_vconf_table_fn == NULL)) return;

    ops->set_vconf_table_fn(ops, table);
}

static inline struct ow_ovsdb_wps_changed *
ow_ovsdb_wps_op_add_changed(struct ow_ovsdb_wps_ops *ops,
                            ow_ovsdb_wps_changed_fn_t *fn,
                            void *fn_priv)
{
    if (ops == NULL) return NULL;
    if (WARN_ON(ops->add_changed_fn == NULL)) return NULL;

    return ops->add_changed_fn(ops, fn, fn_priv);
}

static inline void
ow_ovsdb_wps_op_del_changed(struct ow_ovsdb_wps_ops *ops,
                            struct ow_ovsdb_wps_changed *c)
{
    if (ops == NULL) return;
    if (WARN_ON(ops->del_changed_fn == NULL)) return;

    ops->del_changed_fn(ops, c);
}

static inline void
ow_ovsdb_wps_op_handle_vconf_update(struct ow_ovsdb_wps_ops *ops,
                                    ovsdb_update_monitor_t *mon,
                                    struct schema_Wifi_VIF_Config *old,
                                    struct schema_Wifi_VIF_Config *new,
                                    ovsdb_cache_row_t *row)
{
    if (ops == NULL) return;
    if (WARN_ON(ops->handle_vconf_update_fn == NULL)) return;

    ops->handle_vconf_update_fn(ops, mon, old, new, row);
}

static inline void
ow_ovsdb_wps_op_fill_vstate(struct ow_ovsdb_wps_ops *ops,
                                    struct schema_Wifi_VIF_State *vstate)
{
    if (ops == NULL) return;
    if (WARN_ON(ops->fill_vstate_fn == NULL)) return;

    ops->fill_vstate_fn(ops, vstate);
}

#endif /* OW_OVSDB_WPS_H */
