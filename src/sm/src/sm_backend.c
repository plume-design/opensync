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

#include <ds.h>

#include "sm.h"

struct backend
{
    char *name;
    const sm_backend_funcs_t *funcs;

    ds_dlist_t node;
};

static ds_dlist_t backends = DS_DLIST_INIT(struct backend, node);

void sm_backend_register(const char *name,
                         const sm_backend_funcs_t *funcs)
{
    struct backend *backend = NULL;

    ds_dlist_foreach(&backends, backend) {
        if (strcmp(backend->name, name) == 0) {
            LOGE("%s: Backend already registered", name);
            return;
        }
    }

    backend = (struct backend *)calloc(1, sizeof(struct backend));
    if (!backend) {
        LOGE("%s: Failed to register (mem alloc failed)", name);
        goto free_backend;
    }

    backend->name = strdup(name);
    if (!backend->name) {
        LOGE("%s: Failed to register (mem alloc failed)", name);
        goto free_backend;
    }

    if (WARN_ON(!funcs->start) ||
        WARN_ON(!funcs->update) ||
        WARN_ON(!funcs->stop))
        goto free_backend;

    backend->funcs = funcs;

    ds_dlist_insert_tail(&backends, backend);
    LOGI("%s: Backend registered", name);

    return;

free_backend:
    if (backend)
        free(backend->name);

    free(backend);
}

void sm_backend_unregister(const char *name)
{
    struct backend *backend = NULL;

    ds_dlist_foreach(&backends, backend) {
        if (strcmp(backend->name, name) == 0)
            break;
    }

    if (!backend) {
        LOGW("%s: Backend not registered", name);
        return;
    }

    ds_dlist_remove(&backends, backend);

    free(backend->name);
    free(backend);

    LOGI("%s: Backend unregistered", name);
}

void
sm_backend_report_start(sm_report_type_t report_type,
                        const sm_stats_request_t *request)
{
    struct backend *backend = NULL;
    ds_dlist_foreach(&backends, backend)
        backend->funcs->start(report_type, request);
}

void
sm_backend_report_update(sm_report_type_t report_type,
                         const sm_stats_request_t *request)
{
    struct backend *backend = NULL;
    ds_dlist_foreach(&backends, backend)
        backend->funcs->update(report_type, request);
}

void
sm_backend_report_stop(sm_report_type_t report_type,
                       const sm_stats_request_t *request)
{
    struct backend *backend = NULL;
    ds_dlist_foreach(&backends, backend)
        backend->funcs->stop(report_type, request);
}
