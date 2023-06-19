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

#ifndef OW_WPS_H
#define OW_WPS_H

struct ow_wps_ops;
struct ow_wps_job;

enum ow_wps_job_role {
    OW_WPS_ROLE_ENROLLER,
};

enum ow_wps_job_method {
    OW_WPS_METHOD_PBC,
};

enum ow_wps_job_result {
    OW_WPS_JOB_UNSPECIFIED,
    OW_WPS_JOB_TIMED_OUT_INTERNALLY,
    OW_WPS_JOB_TIMED_OUT_EXTERNALLY,
    OW_WPS_JOB_CANCELLED,
    OW_WPS_JOB_INTERRUPTED,
    OW_WPS_JOB_OVERLAPPED,
    OW_WPS_JOB_SUCCEEDED,
};

typedef void
ow_wps_job_started_fn_t(struct ow_wps_ops *ops,
                        struct ow_wps_job *job);

typedef void
ow_wps_job_finished_fn_t(struct ow_wps_ops *ops,
                         struct ow_wps_job *job);

typedef struct ow_wps_job *
ow_wps_alloc_job_fn_t(struct ow_wps_ops *ops,
                      const char *vif_name,
                      enum ow_wps_job_role role,
                      enum ow_wps_job_method method);

typedef void
ow_wps_job_drop_fn_t(struct ow_wps_ops *ops,
                     struct ow_wps_job *job);

typedef void
ow_wps_job_set_priv_fn_t(struct ow_wps_ops *ops,
                         struct ow_wps_job *job,
                         void *priv);

typedef void *
ow_wps_job_get_priv_fn_t(struct ow_wps_ops *ops,
                         const struct ow_wps_job *job);

typedef enum ow_wps_job_result
ow_wps_job_get_result_fn_t(struct ow_wps_ops *ops,
                           const struct ow_wps_job *job);

typedef void
ow_wps_job_set_callbacks_fn_t(struct ow_wps_ops *ops,
                              struct ow_wps_job *job,
                              ow_wps_job_started_fn_t *started_fn,
                              ow_wps_job_finished_fn_t *finished_fn);

typedef void
ow_wps_job_set_creds_fn_t(struct ow_wps_ops *ops,
                          struct ow_wps_job *job,
                          const struct osw_wps_cred_list *creds);

typedef void
ow_wps_job_start_fn_t(struct ow_wps_ops *ops,
                      struct ow_wps_job *job);

typedef void
ow_wps_job_cancel_fn_t(struct ow_wps_ops *ops,
                       struct ow_wps_job *job);

struct ow_wps_ops {
    ow_wps_alloc_job_fn_t *alloc_job_fn;
    ow_wps_job_drop_fn_t *job_drop_fn;
    ow_wps_job_set_priv_fn_t *job_set_priv_fn;
    ow_wps_job_get_priv_fn_t *job_get_priv_fn;
    ow_wps_job_get_result_fn_t *job_get_result_fn;
    ow_wps_job_set_callbacks_fn_t *job_set_callbacks_fn;
    ow_wps_job_set_creds_fn_t *job_set_creds_fn;
    ow_wps_job_start_fn_t *job_start_fn;
    ow_wps_job_cancel_fn_t *job_cancel_fn;
};

static inline struct ow_wps_job *
ow_wps_op_alloc_job(struct ow_wps_ops *ops,
                    const char *vif_name,
                    enum ow_wps_job_role role,
                    enum ow_wps_job_method method)
{
    if (ops == NULL) return NULL;
    if (ops->alloc_job_fn == NULL) return NULL;

    return ops->alloc_job_fn(ops, vif_name, role, method);
}

static inline void
ow_wps_op_job_drop(struct ow_wps_ops *ops,
                     struct ow_wps_job *job)
{
    if (ops == NULL) return;
    if (ops->job_drop_fn == NULL) return;

    return ops->job_drop_fn(ops, job);
}

static inline void
ow_wps_op_job_set_priv(struct ow_wps_ops *ops,
                       struct ow_wps_job *job,
                       void *priv)
{
    if (ops == NULL) return;
    if (ops->job_set_priv_fn == NULL) return;

    return ops->job_set_priv_fn(ops, job, priv);
}

static inline void *
ow_wps_op_job_get_priv(struct ow_wps_ops *ops,
                       const struct ow_wps_job *job)
{
    if (ops == NULL) return NULL;
    if (ops->job_get_priv_fn == NULL) return NULL;

    return ops->job_get_priv_fn(ops, job);
}

static inline enum ow_wps_job_result
ow_wps_op_job_get_result(struct ow_wps_ops *ops,
                         const struct ow_wps_job *job)
{
    if (ops == NULL) return OW_WPS_JOB_UNSPECIFIED;
    if (ops->job_get_result_fn == NULL) return OW_WPS_JOB_UNSPECIFIED;

    return ops->job_get_result_fn(ops, job);
}

static inline void
ow_wps_op_job_set_callbacks(struct ow_wps_ops *ops,
                            struct ow_wps_job *job,
                            ow_wps_job_started_fn_t *started_fn,
                            ow_wps_job_finished_fn_t *finished_fn)
{
    if (ops == NULL) return;
    if (ops->job_set_callbacks_fn == NULL) return;

    return ops->job_set_callbacks_fn(ops, job, started_fn, finished_fn);
}

static inline void
ow_wps_op_job_set_creds(struct ow_wps_ops *ops,
                        struct ow_wps_job *job,
                        const struct osw_wps_cred_list *creds)
{
    if (ops == NULL) return;
    if (ops->job_set_creds_fn == NULL) return;

    return ops->job_set_creds_fn(ops, job, creds);
}

static inline void
ow_wps_op_job_start(struct ow_wps_ops *ops,
                    struct ow_wps_job *job)
{
    if (ops == NULL) return;
    if (ops->job_start_fn == NULL) return;

    return ops->job_start_fn(ops, job);
}

static inline void
ow_wps_op_job_cancel(struct ow_wps_ops *ops,
                     struct ow_wps_job *job)
{
    if (ops == NULL) return;
    if (ops->job_cancel_fn == NULL) return;

    return ops->job_cancel_fn(ops, job);
}

#endif /* OW_WPS_H */
