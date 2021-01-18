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

#include <stdlib.h>

#include "log.h"

#include "osn_qos.h"

#define OSN_QOS_MARK_BASE   0x001f0000

struct osn_qos
{
    int     oq_qos_depth;
    int     oq_qos_id;
    int     oq_que_depth;
    int     oq_que_id;
};

osn_qos_t* osn_qos_new(const char *ifname)
{
    (void)ifname;

    osn_qos_t *self = calloc(1, sizeof(osn_qos_t));

    return self;
}

void osn_qos_del(osn_qos_t *self)
{
    free(self);
}

bool osn_qos_apply(osn_qos_t *self)
{
    if (self->oq_qos_depth != 0)
    {
        LOG(ERR, "qos: Invalid qdisc configuration.");
        return false;
    }

    if (self->oq_que_depth != 0)
    {
        LOG(ERR, "qos: Invalid queue configuration.");
        return false;
    }

    return true;
}

bool osn_qos_begin(osn_qos_t *self, struct osn_qos_other_config *other_config)
{
    (void)self;
    (void)other_config;

    if (self->oq_qos_id >= 255)
    {
        // See comment in osn_qos_queue_begin()
        LOG(ERR, "qos: Maximum number of qdiscs reached.");
        return false;
    }

    self->oq_qos_depth++;
    self->oq_qos_id++;

    return true;
}

bool osn_qos_end(osn_qos_t *self)
{
    if (self->oq_qos_depth <= 0)
    {
        LOG(ERR, "qos: qdiscs begin/end mismatch.");
        return false;
    }

    self->oq_qos_depth--;

    return true;
}

uint32_t osn_qos_queue_begin(
        osn_qos_t *self,
        int priority,
        int bandwidth,
        const struct osn_qos_other_config *other_config)
{
    (void)priority;
    (void)bandwidth;
    (void)other_config;

    uint32_t mark;

    if (self->oq_que_id >= 255)
    {
        // Needed because of the way return value (mark) is derived below.
        // A real implementation might handle this differently.
        LOG(ERR, "qos: Maximum number of queues reached.");
        return false;
    }

    self->oq_que_depth++;
    self->oq_que_id++;

    mark = OSN_QOS_MARK_BASE | (self->oq_qos_id << 8) | self->oq_que_id;

    return mark;
}

bool osn_qos_queue_end(osn_qos_t *self)
{
    if (self->oq_que_depth <= 0)
    {
        LOG(ERR, "qos: queue begin/end mismatch.");
        return false;
    }

    self->oq_que_depth--;

    return true;
}
