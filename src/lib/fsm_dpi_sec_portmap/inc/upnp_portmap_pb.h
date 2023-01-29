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

#ifndef UPNP_PORTMAP_PB_H_INCLUDED
#define UPNP_PORTMAP_PB_H_INCLUDED

#include "fsm.h"
#include "upnp_portmap.h"
#include "upnp_portmap.pb-c.h"

Upnp__Portmap__ObservationPoint *upnp_portmap_alloc_observation_point(struct fsm_session *session);
void upnp_portmap_free_observation_point(Upnp__Portmap__ObservationPoint *data);

Upnp__Portmap__Portmap *upnp_portmap_alloc_portmap(struct mapped_port_t *port);
void upnp_portmap_free_portmap(Upnp__Portmap__Portmap *data);

Upnp__Portmap__Report *upnp_portmap_alloc_report(struct fsm_session *session);
void upnp_portmap_free_report(Upnp__Portmap__Report *report);

#endif /* UPNP_PORTMAP_PB_H_INCLUDED */
