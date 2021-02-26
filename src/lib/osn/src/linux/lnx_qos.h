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

#ifndef LNX_QOS_H_INCLUDED
#define LNX_QOS_H_INCLUDED

#include "const.h"
#include "lnx_netlink.h"
#include "osn_qos.h"

typedef struct lnx_qos lnx_qos_t;

/* Single Queue instance */
struct lnx_qos_queue
{

    int                     qq_id;          /* QoS queue ID as allocated by lnx_qos_id_get() */
    int                     qq_priority;    /* Queue priority */
    int                     qq_bandwidth;   /* Queue bandwidth */
    char                   *qq_shared;      /* Shared value */
};

struct lnx_qos
{
    char                    lq_ifname[C_IFNAME_LEN];
    bool                    lq_qos_begin;
    bool                    lq_que_begin;
    struct lnx_qos_queue   *lq_queue;
    struct lnx_qos_queue   *lq_queue_e;
    lnx_netlink_t           lq_netlink;     /* Interface monitorting object */
    unsigned int            lq_ifindex;     /* Interface index */
};

bool lnx_qos_init(lnx_qos_t *self, const char *ifname);
void lnx_qos_fini(lnx_qos_t *self);

bool lnx_qos_apply(lnx_qos_t *self);
bool lnx_qos_begin(lnx_qos_t *self, struct osn_qos_other_config *other_config);
bool lnx_qos_end(lnx_qos_t *self);

bool lnx_qos_queue_begin(
        lnx_qos_t *self,
        int priority,
        int bandwidth,
        const char *tag,
        const struct osn_qos_other_config *other_config,
        struct osn_qos_queue_status *qqs);

bool lnx_qos_queue_end(lnx_qos_t *self);

#endif /* LNX_QOS_H_INCLUDED */
