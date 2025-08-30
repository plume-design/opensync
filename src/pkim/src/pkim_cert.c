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
#include <unistd.h>

#include "arena_util.h"
#include "ds.h"
#include "ds_tree.h"
#include "est_client.h"
#include "execssl.h"
#include "log.h"
#include "netutil.h"
#include "osp_pki.h"
#include "osp_unit.h"

#include "pkim_cert.h"
#include "pkim_ovsdb.h"

#define PKIM_CERT_LABEL_DEFAULT "default"
#define PKIM_CERT_LABEL(x)      ((strcmp((x), PKIM_CERT_LABEL_DEFAULT) == 0) ? NULL : (x))

/* Certificate expiration check period in seconds (1 hour by default) */
#define PKIM_CERT_RENEW_CHECK (60 * 60)

/* Time before expiration to start renewing certs in seconds, 60 days by default */
#define PKIM_CERT_RENEW_TIME (60 * 24 * 60 * 60)

/* Amount of time to wait if renewal is overdue in seconds, 1 hour by default */
#define PKIM_CERT_RENEW_WAIT (1 * 60 * 60)

/* EST request timeout in seconds */
#define PKIM_CERT_EST_TIMEOUT 180

/* Minimum retry wait time in seconds */
#define PKIM_CERT_EST_RETRY_MIN 300

/* Maximum retry wait time in seconds */
#define PKIM_CERT_EST_RETRY_MAX 3600

typedef void ev_timer_fn_t(struct ev_loop *loop, ev_timer *w, int revent);

struct pkim_cert
{
    arena_t *pc_arena;            /* Memory arena */
    ds_tree_node_t pc_tnode;      /* Tree node structure */
    arena_frame_t pc_state_frame; /* Per-state arena frame */
    const char *pc_label;         /* Certificate identifier */
    pkim_cert_state_t pc_state;   /* Current certificate state */
    time_t pc_expire_date;        /* Certificate expire date or 0 if not present */
    const char *pc_est_server;    /* EST server for certificate requets */
    int pc_est_retries;           /* Number of EST request retries */
    const char *pc_est_ca;        /* CA received by the EST server */
    const char *pc_est_crt;       /* CRT received by the EST server */
    time_t pc_retry_after;        /* The Retry-After value received by the EST server or 0 if none */
};

static char *pkim_cert_get_subject(arena_t *arena);
static bool pkim_cert_enroll(struct pkim_cert *self);
static bool pkim_cert_ca_verify(struct pkim_cert *self);
static bool pkim_cert_get_bootid(char *buf, size_t bufsz);
static uint32_t pkim_cert_get_stable_random(struct pkim_cert *self);
static est_request_fn_t pkim_cert_cacerts_fn;
static est_request_fn_t pkim_cert_enroll_fn;
static void pkim_cert_timeout_fn(struct ev_loop *loop, ev_timer *w, int revent);
static bool pkim_cert_timeout(struct pkim_cert *self, enum pkim_cert_action action, double timeout);
static char *pkim_cert_time_str(arena_t *arena, double time);
static uint32_t fnv1a(void *data, size_t sz);
static void defer_ev_timer_stop_fn(void *data);
static void defer_unlink_fn(void *data);

ds_tree_t pkim_cert_list = DS_TREE_INIT(ds_str_cmp, struct pkim_cert, pc_tnode);

/*
 * =============================================================================
 * Exported API
 * =============================================================================
 */

/*
 * Start managing certificate with label `label` and use EST server `est_server`
 * for certificate updates.
 */
bool pkim_cert_add(const char *label, const char *est_server)
{
    struct pkim_cert *pc;

    pc = ds_tree_find(&pkim_cert_list, label);
    if (pc != NULL)
    {
        LOG(WARN, "pkim: Certificate `%s` already being managed.", label);
        return false;
    }

    arena_t *pc_arena = arena_new(0);
    if (pc_arena == NULL)
    {
        LOG(ERR, "pkim: Error allocating arena for certificate: %s", label);
        return false;
    }
    pc = arena_calloc(pc_arena, sizeof(*pc));
    pc->pc_arena = pc_arena;

    if (label != NULL)
    {
        pc->pc_label = arena_strdup(pc_arena, label);
        if (pc->pc_label == NULL)
        {
            LOG(ERR, "pkim: Error creating certificate label: %s", label);
            arena_del(pc_arena);
            return false;
        }
    }

    pc->pc_est_server = arena_strdup(pc_arena, est_server);
    if (pc->pc_est_server == NULL)
    {
        LOG(ERR, "pkim: Error creating certificate server URL: %s", label);
        arena_del(pc_arena);
        return false;
    }

    ds_tree_insert(&pkim_cert_list, pc, pc->pc_label);

    LOG(NOTICE, "pkim: %s: Started managing certificate.", pc->pc_label);
    /* Kick-off the cert state machine */
    pkim_cert_state_do(&pc->pc_state, pkim_cert_do_START, NULL);

    return true;
}

/*
 * Stop manging certificate with label `label`.
 */
bool pkim_cert_del(const char *label)
{
    struct pkim_cert *pc = ds_tree_find(&pkim_cert_list, label);
    if (pc == NULL)
    {
        LOG(WARN, "pkim: Error deleting certificate with label `%s`, not found.", label == NULL ? "<default>" : label);
        return false;
    }

    ds_tree_remove(&pkim_cert_list, pc);
    arena_del(pc->pc_arena);

    LOG(NOTICE, "pkim: %s: Stopped managing certificate.", label);

    return true;
}

void pkim_cert_renew(const char *label)
{
    struct pkim_cert *pc = ds_tree_find(&pkim_cert_list, label);
    if (pc == NULL)
    {
        LOG(WARN, "pkim: Error renewing certificate with label `%s`, not found.", label == NULL ? "<default>" : label);
        return;
    }

    LOG(NOTICE, "pkim: %s: Forcing certificate renewal.", label);
    pkim_cert_state_do(&pc->pc_state, pkim_cert_exception_EST_RENEW, NULL);
}

/*
 * =============================================================================
 * PKIM certificate state handlers
 * =============================================================================
 */

/*
 * Stopgap state -- send it the do_START action to kick-off the state machine.
 */
enum pkim_cert_state pkim_cert_state_START(pkim_cert_state_t *state, enum pkim_cert_action action, void *data)
{
    LOG(DEBUG, "%s: %s", pkim_cert_state_str(state->state), pkim_cert_action_str(action));
    struct pkim_cert *self = CONTAINER_OF(state, struct pkim_cert, pc_state);
    (void)state;
    (void)action;
    (void)data;

    if (action != pkim_cert_do_START) return 0;

    /* Clear status */
    pkim_ovsdb_status_set(self->pc_label, NULL);
    /* Save arena frame */
    self->pc_state_frame = arena_save(self->pc_arena);

    return pkim_cert_CRT_VERIFY;
}

/*
 * Verify current certificate. If it doesn't exist or is expired, move to the
 * EST_START state. Otherwise move to the IDLE state.
 */
enum pkim_cert_state pkim_cert_state_CRT_VERIFY(pkim_cert_state_t *state, enum pkim_cert_action action, void *data)
{
    LOG(DEBUG, "%s: %s", pkim_cert_state_str(state->state), pkim_cert_action_str(action));

    struct pkim_cert *self = CONTAINER_OF(state, struct pkim_cert, pc_state);

    if (!osp_pki_cert_info(PKIM_CERT_LABEL(self->pc_label), &self->pc_expire_date, NULL, 0))
    {
        self->pc_expire_date = 0;
    }

    if (time(NULL) < self->pc_expire_date)
    {
        LOG(NOTICE, "pkim: %s: Certificate present.", self->pc_label);
        return pkim_cert_IDLE;
    }

    LOG(NOTICE, "pkim: %s: Certificate not present or expired.", self->pc_label);
    /* Reset retry count before going into ENROLL state */
    self->pc_est_retries = 0;
    return pkim_cert_EST_START;
}

/*
 * Start of the EST enrollment process: Reset the retry counter and save the
 * current arena frame.
 */
enum pkim_cert_state pkim_cert_state_EST_START(pkim_cert_state_t *state, enum pkim_cert_action action, void *data)
{
    LOG(DEBUG, "%s: %s", pkim_cert_state_str(state->state), pkim_cert_action_str(action));
    struct pkim_cert *self = CONTAINER_OF(state, struct pkim_cert, pc_state);

    self->pc_est_retries = 0;

    pkim_ovsdb_status_set(self->pc_label, "enrolling");

    return pkim_cert_EST_RESET;
}

/*
 * Reset the current arena (free everything that was allocated) and start
 * the timeout counter
 */
enum pkim_cert_state pkim_cert_state_EST_RESET(pkim_cert_state_t *state, enum pkim_cert_action action, void *data)
{
    LOG(DEBUG, "%s: %s", pkim_cert_state_str(state->state), pkim_cert_action_str(action));
    struct pkim_cert *self = CONTAINER_OF(state, struct pkim_cert, pc_state);

    arena_restore(self->pc_state_frame);

    self->pc_retry_after = 0;

    if (!pkim_cert_timeout(self, pkim_cert_do_TIMEOUT, (double)PKIM_CERT_EST_TIMEOUT))
    {
        LOG(ERR, "pkim: %s: Error initializing timeout handler.", self->pc_label);
        return pkim_cert_EST_RETRY;
    }

    return pkim_cert_EST_CACERTS;
}

/*
 * Fetch certificate authority and move to EST_ENROLL if successful. Otherwise
 * move to EST_RETRY.
 */
enum pkim_cert_state pkim_cert_state_EST_CACERTS(pkim_cert_state_t *state, enum pkim_cert_action action, void *data)
{
    LOG(DEBUG, "%s: %s", pkim_cert_state_str(state->state), pkim_cert_action_str(action));
    struct pkim_cert *self = CONTAINER_OF(state, struct pkim_cert, pc_state);

    switch (action)
    {
        case pkim_cert_do_STATE_INIT:
            LOG(NOTICE, "pkim: %s: Fetching CA from remote: %s", self->pc_label, self->pc_est_server);
            if (!est_client_cacerts(self->pc_arena, EV_DEFAULT, self->pc_est_server, pkim_cert_cacerts_fn, self))
            {
                LOG(ERR, "pkim: %s: Error issuing cacerts request.", self->pc_label);
                break;
            }
            return 0;

        case pkim_cert_do_EST_SUCCESS:
            if (data == NULL)
            {
                LOG(ERR, "pkim: %s: Received CA is empty.", self->pc_label);
                break;
            }

            LOG(INFO, "pkim: %s: Acquired remote CA.", self->pc_label);
            self->pc_est_ca = data;
            return pkim_cert_EST_ENROLL;

        case pkim_cert_do_EST_ERROR:
            pkim_ovsdb_status_set(self->pc_label, "error_enroll");
            LOG(INFO, "pkim: %s: Error fetching CA.", self->pc_label);
            break;

        case pkim_cert_do_TIMEOUT:
            pkim_ovsdb_status_set(self->pc_label, "error_timeout");
            LOG(INFO, "pkim: %s: Timeout while fetching CA.", self->pc_label);
            break;

        default:
            return 0;
    }

    return pkim_cert_EST_RETRY;
}

/*
 * Create the subject line and generate a CSR. Proceed to issue a simpleenroll
 * or simplereenroll request (depending on the existence and expire date of the
 * current certificate).
 */
enum pkim_cert_state pkim_cert_state_EST_ENROLL(pkim_cert_state_t *state, enum pkim_cert_action action, void *data)
{
    LOG(DEBUG, "%s: %s", pkim_cert_state_str(state->state), pkim_cert_action_str(action));
    struct pkim_cert *self = CONTAINER_OF(state, struct pkim_cert, pc_state);

    switch (action)
    {
        case pkim_cert_do_STATE_INIT:
            /* Start the EST enroll request */
            if (!pkim_cert_enroll(self))
            {
                LOG(ERR, "pkim: %s: Error issuing enroll request.", self->pc_label);
                break;
            }
            return 0;

        case pkim_cert_do_EST_SUCCESS:
            LOG(NOTICE, "pkim: %s: Acquired new certificate.", self->pc_label);
            self->pc_est_crt = data;
            return pkim_cert_EST_DONE;

        case pkim_cert_do_EST_ERROR:
            LOG(ERR, "pkim: %s: EST request error occurred on server: %s", self->pc_label, self->pc_est_server);
            pkim_ovsdb_status_set(self->pc_label, "error_enroll");
            break;

        case pkim_cert_do_TIMEOUT:
            LOG(ERR,
                "pkim: %s: EST request timeout of %d seconds reached on server: %s",
                self->pc_label,
                PKIM_CERT_EST_TIMEOUT,
                self->pc_est_server);
            pkim_ovsdb_status_set(self->pc_label, "error_timeout");
            break;

        default:
            break;
    }

    return pkim_cert_EST_RETRY;
}

/*
 * Both the CA and enroll request completed. Verify and update the certificate.
 */
enum pkim_cert_state pkim_cert_state_EST_DONE(pkim_cert_state_t *state, enum pkim_cert_action action, void *data)
{
    LOG(DEBUG, "%s: %s", pkim_cert_state_str(state->state), pkim_cert_action_str(action));
    struct pkim_cert *self = CONTAINER_OF(state, struct pkim_cert, pc_state);

    if (action != pkim_cert_do_STATE_INIT) return 0;

    if (!pkim_cert_ca_verify(self))
    {
        LOG(ERR, "pkim: %s: Verification failed.", self->pc_label);
        pkim_ovsdb_status_set(self->pc_label, "error_cert");
        return pkim_cert_EST_RETRY;
    }

    LOG(NOTICE, "pkim: %s: Verification OK. Updating certificate.", self->pc_label);
    /* Save the client certificate to the device */
    if (!osp_pki_cert_update(PKIM_CERT_LABEL(self->pc_label), data))
    {
        LOG(ERR, "pkim: %s: Error updating certificate.", self->pc_label);
        pkim_ovsdb_status_set(self->pc_label, "error_device");
        return pkim_cert_EST_RETRY;
    }

    if (!osp_pki_cert_install(PKIM_CERT_LABEL(self->pc_label)))
    {
        LOG(ERR, "pkim: %s: Error installing new certificate.", self->pc_label);
        pkim_ovsdb_status_set(self->pc_label, "error_device");
        return pkim_cert_EST_RETRY;
    }

    arena_restore(self->pc_state_frame);

    return pkim_cert_CRT_VERIFY;
}

/*
 * Calculate an incremental random timeout and sleep for that amount. On timeout
 * move to the EST_RESET state.
 */
enum pkim_cert_state pkim_cert_state_EST_RETRY(pkim_cert_state_t *state, enum pkim_cert_action action, void *data)
{
    LOG(DEBUG, "%s: %s", pkim_cert_state_str(state->state), pkim_cert_action_str(action));

    struct pkim_cert *self = CONTAINER_OF(state, struct pkim_cert, pc_state);
    double retry_timeout = PKIM_CERT_EST_RETRY_MAX;
    char *timeout_origin = "unknown";

    switch (action)
    {
        case pkim_cert_do_STATE_INIT:
            arena_restore(self->pc_state_frame);

            if (self->pc_retry_after > 0 && self->pc_retry_after <= PKIM_CERT_EST_RETRY_MAX)
            {
                timeout_origin = "Retry-After";
                retry_timeout = (double)self->pc_retry_after;
                self->pc_est_retries = 0;
            }
            else
            {
                timeout_origin = "Backoff Timer";
                retry_timeout =
                        netutil_backoff_time(self->pc_est_retries, PKIM_CERT_EST_RETRY_MIN, PKIM_CERT_EST_RETRY_MAX);
                self->pc_est_retries++;
            }

            if (!pkim_cert_timeout(self, pkim_cert_do_TIMEOUT, retry_timeout))
            {
                LOG(ERR, "pkim: %s: Error initializing timeout handler.", self->pc_label);
                break;
            }

            LOG(INFO,
                "pkim: %s: Retrying EST enroll after %0.0f seconds, retry number %d (%s)",
                self->pc_label,
                retry_timeout,
                self->pc_est_retries,
                timeout_origin);
            return 0;

        case pkim_cert_do_TIMEOUT:
            LOG(DEBUG, "pkim: %s: Retry timeout reached.", self->pc_label);
            break;

        default:
            break;
    }

    return pkim_cert_EST_RESET;
}

/*
 * Certificate is installed and valid. Calculate a random timestamp between 10%
 * and 100% of PKIM_CERT_EST_RENEW before the expire date, wait for that amount
 * and renew the certificate.
 */
enum pkim_cert_state pkim_cert_state_IDLE(pkim_cert_state_t *state, enum pkim_cert_action action, void *data)
{
    ARENA_SCRATCH(scratch);

    LOG(DEBUG, "%s: %s", pkim_cert_state_str(state->state), pkim_cert_action_str(action));
    struct pkim_cert *self = CONTAINER_OF(state, struct pkim_cert, pc_state);
    enum pkim_cert_state retval = 0;

    double renew_date;
    double wait_time;

    switch (action)
    {
        case pkim_cert_do_STATE_INIT:
            /* Kick off expire check timer */
            if (!pkim_cert_timeout(self, pkim_cert_do_EXPIRE_CHECK, 0.0))
            {
                LOG(ERR, "pkim: %s: Error installing expire check timer.", self->pc_label);
            }
            return 0;

        case pkim_cert_do_EXPIRE_CHECK:
            arena_restore(self->pc_state_frame);
            /*
             * Renew date is calculated by taking the current certificate expiration
             * date and a random value between 10% and 100% of PKI_CERT_EST_RENEW_TIME
             */
            renew_date = (double)self->pc_expire_date;
            renew_date -= PKIM_CERT_RENEW_TIME * 0.1;
            renew_date -= (pkim_cert_get_stable_random(self) % PKIM_CERT_RENEW_TIME) * 0.9;

            wait_time = renew_date - (double)time(NULL);
            if (wait_time < 0.0)
            {
                pkim_ovsdb_status_set(self->pc_label, "overdue");
                wait_time = PKIM_CERT_RENEW_WAIT * 0.5;
                wait_time += (random() % PKIM_CERT_RENEW_WAIT) * 0.5;
                LOG(INFO,
                    "pkimt: %s: Certificate renew is overdue. Renewing in %s.",
                    self->pc_label,
                    pkim_cert_time_str(scratch, wait_time));
                pkim_cert_timeout(self, pkim_cert_do_TIMEOUT, wait_time);
                return 0;
            }

            pkim_ovsdb_status_set(self->pc_label, "success");

            LOG(NOTICE,
                "pkim: %s: Certificate renewal scheduled in %s.",
                self->pc_label,
                pkim_cert_time_str(scratch, wait_time));

            if (wait_time > PKIM_CERT_RENEW_CHECK)
            {
                wait_time = PKIM_CERT_RENEW_CHECK;
            }

            if (!pkim_cert_timeout(self, pkim_cert_do_EXPIRE_CHECK, wait_time))
            {
                LOG(ERR, "pkim: %s: Error installing EXPIRE_CHECK handler.", self->pc_label);
                break;
            }
            return 0;

        case pkim_cert_do_TIMEOUT:
            LOG(INFO, "pkim: %s: Renewing certificate.", self->pc_label);
            retval = pkim_cert_EST_START;
            break;

        default:
            break;
    }

    arena_restore(self->pc_state_frame);
    return retval;
}

enum pkim_cert_state pkim_cert_state_EXCEPTION(pkim_cert_state_t *state, enum pkim_cert_action action, void *data)
{
    LOG(DEBUG, "%s: %s", pkim_cert_state_str(state->state), pkim_cert_action_str(action));
    struct pkim_cert *self = CONTAINER_OF(state, struct pkim_cert, pc_state);
    (void)state;
    (void)action;
    (void)data;

    arena_restore(self->pc_state_frame);

    switch (action)
    {
        case pkim_cert_exception_EST_RENEW:
            LOG(INFO, "pkim: %s: Forcing re-enroll.", self->pc_label);
            return pkim_cert_EST_START;

        default:
            LOG(WARN, "pkim: %s: Unhandled exception: %s", self->pc_label, pkim_cert_action_str(action));
            break;
    }

    return 0;
}

/*
 * =============================================================================
 * Support functions
 * =============================================================================
 */

/*
 * Perform a simpleenroll/simplereenroll request against the EST server defined
 * in self->pc_est_server.
 */
bool pkim_cert_enroll(struct pkim_cert *self)
{
    ARENA_SCRATCH(scratch);

    bool retval = false;

    /* Get the device-specific subject line */
    char *csr_subject = pkim_cert_get_subject(self->pc_arena);
    if (csr_subject == NULL)
    {
        LOG(ERR, "pkim: %s: Error generating CSR subject.", self->pc_label);
        return false;
    }

    /* Generate the CSR using the subject line above */
    char *csr = osp_pki_cert_request(PKIM_CERT_LABEL(self->pc_label), csr_subject);
    if (csr == NULL || !arena_defer_free(self->pc_arena, csr))
    {
        LOG(ERR, "pkim: %s: Error generating certificate signing request.", self->pc_label);
        return false;
    }

    /*
     * If the certificate is expired or doesn't exist (pc_expire_date == 0),
     * perform a simpleenroll request. Otherwise do a simplereenroll request.
     */
    if (time(NULL) < self->pc_expire_date)
    {
        LOG(INFO, "pkim: %s: Performing REENROLL: %s", self->pc_label, self->pc_est_server);
        retval = est_client_simple_reenroll(
                self->pc_arena,
                EV_DEFAULT,
                self->pc_est_server,
                csr,
                pkim_cert_enroll_fn,
                self);
    }
    else
    {
        LOG(INFO, "pkim: %s: Performing ENROLL: %s", self->pc_label, self->pc_est_server);
        retval = est_client_simple_enroll(
                self->pc_arena,
                EV_DEFAULT,
                self->pc_est_server,
                csr,
                pkim_cert_enroll_fn,
                self);
    }

    if (!retval)
    {
        LOG(ERR, "pkim: %s: Error enrolling certificate: %s", self->pc_label, self->pc_est_server);
    }

    return retval;
}

/*
 * Get the device subject line that will be used for the certificate signing
 * request
 */
char *pkim_cert_get_subject(arena_t *arena)
{
    arena_frame_auto_t f = arena_save(arena);

    char buf[1024];

    char *subj = arena_strdup(arena, "");

    if (osp_unit_id_get(buf, sizeof(buf)))
    {
        subj = arena_scprintf(arena, subj, "/commonName=%s", buf);
    }

    if (osp_unit_serial_get(buf, sizeof(buf)))
    {
        subj = arena_scprintf(arena, subj, "/serialNumber=%s", buf);
    }

    if (osp_unit_model_get(buf, sizeof(buf)))
    {
        subj = arena_scprintf(arena, subj, "/title=%s", buf);
    }

    if (osp_unit_sku_get(buf, sizeof(buf)))
    {
        subj = arena_scprintf(arena, subj, "/surname=%s", buf);
    }

    if (osp_unit_hw_revision_get(buf, sizeof(buf)))
    {
        subj = arena_scprintf(arena, subj, "/supportedApplicationContext=%s", buf);
    }

    if (osp_unit_manufacturer_get(buf, sizeof(buf)))
    {
        subj = arena_scprintf(arena, subj, "/localityName=%s", buf);
    }

    if (osp_unit_vendor_name_get(buf, sizeof(buf)))
    {
        subj = arena_scprintf(arena, subj, "/organizationName=%s", buf);
    }

    if (osp_unit_vendor_part_get(buf, sizeof(buf)))
    {
        subj = arena_scprintf(arena, subj, "/givenName=%s", buf);
    }

    if (osp_unit_factory_get(buf, sizeof(buf)))
    {
        subj = arena_scprintf(arena, subj, "/stateOrProvinceName=%s", buf);
    }

    if (subj == NULL)
    {
        LOG(ERR, "est_util: Error allocating subject string.");
        return NULL;
    }

    f = arena_save(arena);

    return subj;
}

/*
 * Verify the certificate against the CA
 */
bool pkim_cert_ca_verify(struct pkim_cert *self)
{
    ARENA_SCRATCH(scratch);
    /*
     * We cannot pass both the CA and the CRT over stdin. Write the CA to a file
     * first and use that.
     */
    char *ca_path = arena_sprintf(scratch, "/tmp/ca.pem.%s", self->pc_label);
    if (ca_path == NULL || !arena_defer(scratch, defer_unlink_fn, ca_path))
    {
        LOG(ERR, "pkim: %s: Error creating CA file.", self->pc_label);
        return false;
    }

    FILE *ca_file = fopen(ca_path, "w");
    if (ca_file == NULL || !arena_defer_fclose(scratch, ca_file))
    {
        LOG(ERR, "pkim: %s: Error opening CA file.", self->pc_label);
        return false;
    }

    if (fwrite(self->pc_est_ca, strlen(self->pc_est_ca), 1, ca_file) != 1)
    {
        LOG(ERR, "pkim: %s: Error writing CA file.", self->pc_label);
        return false;
    }

    fflush(ca_file);

    if (execssl_arena(scratch, self->pc_est_crt, "verify", "-CAfile", ca_path) == NULL)
    {
        LOG(ERR, "pkim: %s: openssl verification of certificate against CA failed.", self->pc_label);
        return false;
    }

    return true;
}

bool pkim_cert_get_bootid(char *buf, size_t bufsz)
{
    ARENA_SCRATCH(scratch);

    if (access("/proc/sys/kernel/random/boot_id", R_OK) != 0)
    {
        LOG(WARN, "pkim: `boot_id` is unreadable or does not exist.");
        return false;
    }

    FILE *f = fopen("/proc/sys/kernel/random/boot_id", "r");
    if (f == NULL || !arena_defer_fclose(scratch, f))
    {
        LOG(WARN, "pkim: Error opening `boot_id` file.");
        return false;
    }

    if (fgets(buf, sizeof(buf), f) == NULL)
    {
        LOG(WARN, "pkmim: Error reading `boot_id` file.");
        return false;
    }

    return true;
}

/*
 * Generate a random number that remains stable until a reboot. Ensure the number
 * is not 0.
 *
 * Use a simple fnv-1a hash on the content of the
 * /proc/sys/kernel/random/boot_id file and the cert label.
 */
uint32_t pkim_cert_get_stable_random(struct pkim_cert *self)
{
    ARENA_SCRATCH(scratch);

    char buf[128] = {0};

    if (!pkim_cert_get_bootid(buf, sizeof(buf)))
    {
        snprintf(buf, sizeof(buf), "%d", getpid());
    }

    /*
     * Concatenate the bootid and the certificate label and calculate a FNV-1a
     * hash. This should yield a pseudo random number that stays stable across
     * reboots and is unique for each certificate.
     */
    char *hstr = arena_sprintf(scratch, "%s.%s", self->pc_label, buf);
    return fnv1a(hstr, strlen(hstr));
}

/*
 * EST cacerts request callback
 */
void pkim_cert_cacerts_fn(const struct est_request_status ers, const char *data, void *ctx)
{
    struct pkim_cert *self = ctx;

    enum pkim_cert_action action = pkim_cert_do_EST_ERROR;

    LOG(DEBUG, "pkim: %s: EST cacerts request returned = %d", self->pc_label, ers.status);
    switch (ers.status)
    {
        case ER_STATUS_OK:
            action = pkim_cert_do_EST_SUCCESS;
            break;

        case ER_STATUS_ERROR:
            if (ers.ER_STATUS_ERROR.retry_after > 0) self->pc_retry_after = ers.ER_STATUS_ERROR.retry_after;
            break;
    }

    pkim_cert_state_do(&self->pc_state, action, (void *)data);
}

/*
 * EST simpleenroll/simplereenroll request callback
 */
void pkim_cert_enroll_fn(const struct est_request_status ers, const char *data, void *ctx)
{
    struct pkim_cert *self = ctx;

    enum pkim_cert_action action = pkim_cert_do_EST_ERROR;

    LOG(DEBUG, "pkim: %s: EST simpleenroll/simplereenroll request returned = %d", self->pc_label, ers.status);
    switch (ers.status)
    {
        case ER_STATUS_OK:
            action = pkim_cert_do_EST_SUCCESS;
            break;

        case ER_STATUS_ERROR:
            if (ers.ER_STATUS_ERROR.retry_after > 0) self->pc_retry_after = ers.ER_STATUS_ERROR.retry_after;
            break;
    }

    pkim_cert_state_do(&self->pc_state, action, (void *)data);
}

struct pkim_cert_timeout_ctx
{
    ev_timer ct_ev_timer;
    struct pkim_cert *ct_self;
    enum pkim_cert_action ct_action;
};

void pkim_cert_timeout_fn(struct ev_loop *loop, ev_timer *w, int revent)
{
    struct pkim_cert_timeout_ctx *pct = CONTAINER_OF(w, struct pkim_cert_timeout_ctx, ct_ev_timer);
    pkim_cert_state_do(&pct->ct_self->pc_state, pct->ct_action, NULL);
}

bool pkim_cert_timeout(struct pkim_cert *self, enum pkim_cert_action action, double timeout)
{
    struct pkim_cert_timeout_ctx *pct = arena_calloc(self->pc_arena, sizeof(*pct));
    if (pct == NULL)
    {
        return false;
    }

    pct->ct_self = self;
    pct->ct_action = action;
    ev_timer_init(&pct->ct_ev_timer, pkim_cert_timeout_fn, timeout, 0.0);
    ev_timer_start(EV_DEFAULT, &pct->ct_ev_timer);

    if (!arena_defer(self->pc_arena, defer_ev_timer_stop_fn, &pct->ct_ev_timer))
    {
        return false;
    }

    return true;
}

static char *pkim_cert_time_str(arena_t *arena, double ts)
{
    int d = abs((int)ts) / (24 * 60 * 60);
    int h = (abs((int)ts) / (60 * 60)) % 24;
    int m = (abs((int)ts) / 60) % 60;
    int s = abs((int)ts) % 60;

    char *prefix = ts < 0.0 ? "-" : "";

    char *retval;
    if (d == 0)
    {
        retval = arena_sprintf(arena, "%s%d hours, %d minutes and %d seconds", prefix, h, m, s);
    }
    else
    {
        retval = arena_sprintf(arena, "%s%d days, %d hours, %d minutes and %d seconds", prefix, d, h, m, s);
    }

    return retval != NULL ? retval : "<conversion_error>";
}

/*
 * Simple FNV-1a hash implementation
 */
uint32_t fnv1a(void *data, size_t sz)
{
    uint32_t hash = 0x811c9dc5;

    for (size_t ii = 0; ii < sz; ii++)
    {
        hash ^= ((uint8_t *)data)[ii];
        hash *= 0x01000193;
    }

    return hash;
}

/*
 * =============================================================================
 * Arena support functions
 * =============================================================================
 */
void defer_ev_timer_stop_fn(void *data)
{
    ev_timer *ev = data;
    LOG(DEBUG, "pkim: Stopping deferred ev_timer.");
    ev_timer_stop(EV_DEFAULT, ev);
}

void defer_unlink_fn(void *data)
{
    char *path = data;
    if (unlink(path) != 0)
    {
        LOG(WARN, "pkim: Error unlinking: %s\n", path);
    }
}
