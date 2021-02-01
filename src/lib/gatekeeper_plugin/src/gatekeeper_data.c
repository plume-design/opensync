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

#include <stdbool.h>

#include "gatekeeper_data.h"
#include "gatekeeper_msg.h"
#include "gatekeeper.h"
#include "fsm_policy.h"
#include "log.h"


bool
gatekeeper_get_fqdn_req(struct gk_request_data *req_data)
{
    if (req_data == NULL) return false;

    return true;
}


bool
gatekeeper_get_ip_req(struct gk_request_data *req_data)
{
    if (req_data == NULL) return false;

    return true;
}


static void
gatekeeper_free_req(struct gk_request *gk_req)
{
    free(gk_req);
}


void
gatekeeper_free_pb(struct gk_packed_buffer *pb)
{
    gk_free_packed_buffer(pb);
}


struct gk_packed_buffer *
gatekeeper_get_req(struct fsm_session *session,
                   struct fsm_policy_req *req)
{
    struct gk_request_data req_data;
    struct gk_packed_buffer *pb;
    struct gk_request *gk_req;
    int req_type;
    bool rc;

    gk_req = calloc(1, sizeof(*gk_req));
    if (gk_req == NULL) return NULL;

    req_data.session = session;
    req_data.req = req;
    req_data.gk_req = gk_req;

    rc = true;
    req_type = fsm_policy_get_req_type(req);

    if (req_type == FSM_FQDN_REQ) rc = gatekeeper_get_fqdn_req(&req_data);
    if (req_type == FSM_IP_REQ) rc = gatekeeper_get_ip_req(&req_data);

    if (!rc) goto err;

    pb = gk_serialize_request(gk_req);
    return pb;

err:
    gatekeeper_free_req(gk_req);
    return NULL;
}
