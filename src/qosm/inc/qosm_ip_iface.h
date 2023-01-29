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

#ifndef QOSM_IP_INTERFACE_INCLUDED
#define QOSM_IP_INTERFACE_INCLUDED

#include "qosm.h"

void
qosm_ip_iface_init(void);

struct qosm_ip_iface*
qosm_ip_iface_get(struct schema_IP_Interface *conf);

void
qosm_ip_iface_start(struct qosm_ip_iface *ipi);

void
qosm_ip_iface_del(struct schema_IP_Interface *conf);

void
qosm_ip_iface_update_ic(struct qosm_ip_iface *ipi,
                           struct schema_IP_Interface *conf);

void
qosm_init_debounce_cb(ev_debounce_fn_t *debounce_cb);

void
qosm_ip_iface_debounce_fn(struct ev_loop *loop, ev_debounce *w, int revent);

#endif //QOSM_IP_INTERFACE_INCLUDED
