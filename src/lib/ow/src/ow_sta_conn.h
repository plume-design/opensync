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

#ifndef OW_STA_CONN_H_INCLUDED
#define OW_STA_CONN_H_INCLUDED

#include <osw_drv.h>

/* Tracks STA VIF connection failures reported by drivers and
 * summarizes them into a single most-conclusive failure cause per
 * connection attempt window. A window ends when the link connects
 * or the configured network (list) changes.
 */

typedef struct ow_sta_conn ow_sta_conn_t;
typedef struct ow_sta_conn_observer ow_sta_conn_observer_t;

typedef void ow_sta_conn_changed_fn_t(void *priv, const char *vif_name);

ow_sta_conn_observer_t *ow_sta_conn_observer_alloc(ow_sta_conn_t *m, ow_sta_conn_changed_fn_t *fn, void *priv);
void ow_sta_conn_observer_drop(ow_sta_conn_observer_t *o);

/* Most conclusive failure of the current connection attempt window.
 * NULL when there is none, eg. the link is connected.
 */
const struct osw_drv_vif_sta_conn_failure *ow_sta_conn_get_failure(ow_sta_conn_t *m, const char *vif_name);

#endif /* OW_STA_CONN_H_INCLUDED */
