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

#include <stddef.h>
#include <stdlib.h>

#include "est_client.h"
#include "netutil.h"
#include "osp_pki.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "target.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

#define MAX_NUM_RETRIES           5
#define PKI_CERT_EXPIRY_CHECK_TO  (3600 * 24)     /* Check Cert expiry once everyday */
#define PKI_CERT_EXPIRY_LEAD_TIME (3600 * 24 * 4) /* 4 days before the expiry date */

static ovsdb_table_t table_PKI_Config;

static struct est_client_cfg est_cfg = {0};

static log_severity_t pkim_log_severity = LOG_SEVERITY_INFO;

static ev_timer pki_expiry_to;
static ev_async pki_expiry_async;
static ev_timer retry_after_to;
static time_t cert_expiry_time = 0;
static char cert_subject[2048] = {0};
static int num_retries = 0;
static int backoff_timeout = 1; /* Has to be 1 to calculate backoff */
static void pkim_get_est_cert(struct est_client_cfg *est_cfg);
static void pki_cert_expiry_check(bool force);
static void pki_cert_expiry_timer_fn(struct ev_loop *loop, ev_timer *timer, int event);
static void pki_cert_expiry_start(void);
static void pki_cert_expiry_sigusr2(int);

static bool pkim_update_ovsdb_status(char *status)
{
    char *filter[] = {"+", SCHEMA_COLUMN(PKI_Config, status), NULL};
    struct schema_PKI_Config config;

    MEMZERO(config);
    SCHEMA_SET_STR(config.status, status);

    /* Update OVSDB Table with Status */
    return ovsdb_table_update_where_f(&table_PKI_Config, NULL, &config, filter);
}

/* Checks the Certificate Expiry and Starts a Re-Enroll with the Server if
   the certificate is expired; if force is true, a renewal is forced */
static void pki_cert_expiry_check(bool force)
{
    time_t now = time(NULL);

    if (!cert_expiry_time)
    {
        LOGE("Expiry time error");
        return;
    }

    if (!force && difftime(cert_expiry_time, now) > PKI_CERT_EXPIRY_LEAD_TIME) return;

    est_cfg.update_response_cb = NULL;
    int ret = est_client_cert_renew(&est_cfg);
    if (ret == 0)
    {
        LOGI("Certificate renewal success");
        /* Update OVSDB table with Status as Re-Enrolled */
        if (pkim_update_ovsdb_status("Re-Enrolled"))
        {
            LOGT("PKI Config updated status as Re-Enrolled");
        }
        else
        {
            LOGE("PKI Config table update Error");
        }

        if (!osp_pki_setup())
        {
            LOGE("Error installing new certificates.");
        }
    }
    else if (ret == -1)
    {
        LOGI("Certificate renewal Connection Error");
        /* Update OVSDB table with Status as Re-Enroll Connection Error */
        if (pkim_update_ovsdb_status("Re-Enrolled Conn Error"))
            LOGD("PKI Config updated status");
        else
            LOGE("PKI Config table update Error");
    }
    else
    {
        LOGE("Certificate renewal Error: %d", ret);
    }

    return;
}

/* Checks the Certificate Expiry and Starts a Re-Enroll with the Server if
   the certificate is expired */
void pki_cert_expiry_timer_fn(struct ev_loop *loop, ev_timer *timer, int event)
{
    (void)loop;
    (void)timer;
    (void)event;

    pki_cert_expiry_check(false);
    return;
}

/* Force certificate renewal */
void pki_cert_expiry_event_fn(struct ev_loop *loop, ev_async *watcher, int event)
{
    (void)loop;
    (void)watcher;
    (void)event;

    LOG(NOTICE, "Forcing certificate renewal.");
    pki_cert_expiry_check(true);
    return;
}

void pki_cert_expiry_sigusr2(int signum)
{
    (void)signum;
    ev_async_send(EV_DEFAULT, &pki_expiry_async);
}

/* Initialize certificate expiry checks */
static void pki_cert_expiry_start(void)
{
    /* If timer is running stop it */
    if (ev_is_active(&pki_expiry_to))
    {
        LOGD("Cert Expiry check start timer");
        ev_timer_stop(EV_DEFAULT, &pki_expiry_to);
    }

    /* Start cert expiry check timer */
    ev_timer_init(&pki_expiry_to, pki_cert_expiry_timer_fn, 0.0, PKI_CERT_EXPIRY_CHECK_TO);
    ev_timer_start(EV_DEFAULT, &pki_expiry_to);

    ev_async_init(&pki_expiry_async, pki_cert_expiry_event_fn);
    ev_async_start(EV_DEFAULT, &pki_expiry_async);
}

static void pkim_retry_cert_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    LOGD("pkim_retry_cert_cb Retrying");
    if (num_retries > MAX_NUM_RETRIES)
    {
        LOGD("Maximum number of retries reached (%d > %d)", num_retries, MAX_NUM_RETRIES);
    }

    num_retries++;
    pkim_get_est_cert(&est_cfg);
}

static void pkim_get_est_cert(struct est_client_cfg *est_cfg)
{
    int ret = est_client_get_cert(est_cfg);
    if (ret == 0)
    {
        /* Reset retries if we were able to successfully get
           the certificate during retry */
        num_retries = 0;

        LOGI("Updated Client Certificate");
        if (!osp_pki_cert_info(&cert_expiry_time, cert_subject, sizeof(cert_subject)))
        {
            LOGI("Invalid certificate received");
        }
        else
        {
            est_cfg->subject = cert_subject;
        }

        if (cert_expiry_time <= 0)
        {
            LOGI("Received expired cert");
        }

        /* Update OVSDB table with Status as Enrolled */
        if (pkim_update_ovsdb_status("Enrolled"))
        {
            LOGD("PKI Config updated status as Enrolled");
        }
        else
        {
            LOGE("PKI Config table update Error");
        }

        pki_cert_expiry_start();

        if (!osp_pki_setup())
        {
            LOGE("Error installing new certificates.");
        }
    }
    else if (ret == -1)
    {
        if (!retry_after_to.active)
        {
            /* Start a backoff with a step of 5 min and max of an hour */
            int timeout = netutil_backoff_time(num_retries, 300, 3600);
            LOGI("Retrying in %d seconds", timeout);
            ev_timer_init(&retry_after_to, pkim_retry_cert_cb, timeout, 0);
            ev_timer_init(&retry_after_to, pkim_retry_cert_cb, 10, 0);
            ev_timer_start(EV_DEFAULT, &retry_after_to);
        }

        /* Update OVSDB table with Status as Re-Enrolled */
        if (!pkim_update_ovsdb_status("Connection Error"))
        {
            LOGE("PKI Config table update Error");
            return;
        }
    }
    else
    {
        LOGI("Error acquiring certificate.");
    }

    return;
}

void pkim_handle_error_cb(long retry_after, long http_rsp_code)
{
    /* Handle retry errors */
    switch (http_rsp_code)
    {
        case 500: /* Internal Server Error */
        case 502: /* Bad Gateway */
        case 503: /* Service Unavailable */
            if (retry_after != 0)
            {
                LOGI("Retrying after %ld seconds", retry_after);
                ev_timer_init(&retry_after_to, pkim_retry_cert_cb, retry_after, 0);
                ev_timer_start(EV_DEFAULT, &retry_after_to);
            }
            else
            {
                int timeout = netutil_backoff_time(num_retries, 5, 900);
                LOGI("Retrying in %d seconds", timeout);
                ev_timer_init(&retry_after_to, pkim_retry_cert_cb, timeout, 0);
                ev_timer_start(EV_DEFAULT, &retry_after_to);
            }
            break;

        default:
            LOGN("Unhandled HTTP error code: %ld\n", http_rsp_code);
            break;
    }
}

void update_pki_cfg(struct schema_PKI_Config *pki_config)
{
    LOGT("Server URL=%s AUTH=%s", pki_config->server_url, pki_config->auth_method);
    STRSCPY(est_cfg.server_url, pki_config->server_url);

    if (!strcmp(pki_config->auth_method, "BASIC"))
    {
        est_cfg.auth_method = AUTH_BASIC;
    }
    else
    {
        est_cfg.auth_method = AUTH_DIGEST;
    }
}

static void callback_PKI_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_PKI_Config *old,
        struct schema_PKI_Config *new)
{
    switch (mon->mon_type)
    {
        default:
            break;

        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            LOGT("PKI Config Table Update");
            bool get_cert = false;

            if (strlen(new->server_url) == 0) return;

            if (strcmp(est_cfg.server_url, new->server_url) == 0) return;

            get_cert = true;

            update_pki_cfg(new);

            if (strlen(old->server_url) == 0)
            {
                est_cfg.update_response_cb = pkim_handle_error_cb;

                /* If device specific certificate present, start expiry timer loop
                   and return */
                if (osp_pki_cert_info(&cert_expiry_time, cert_subject, sizeof(cert_subject)))
                {
                    LOGI("Client Certificate present");

                    est_cfg.subject = cert_subject;
                    /* Start Re-Enroll now */
                    pki_cert_expiry_start();
                    return;
                }
            }

            if (get_cert)
            {
                /* If Retrying, reset all retry attributes since
                   we are getting new certificate from new server.
                */
                if (num_retries)
                {
                    /* Will not reach here unless server url is
                       changed for testing. */
                    backoff_timeout = 1;
                    num_retries = 0;
                    ev_timer_stop(EV_DEFAULT, &retry_after_to);
                }
                LOGI("Server url changed, getting new certificate");
                pkim_get_est_cert(&est_cfg);
            }

            break;
    }
}

int main(int argc, char **argv)
{
    struct ev_loop *loop = EV_DEFAULT;
    time_t now = time(NULL);
    struct tm ref_time;

    /* Enable logging */
    target_log_open("PKIM", 0);
    LOGN("Starting PKI Client manager - PKIM");
    log_severity_set(pkim_log_severity);

    /*
     * Process the "boot" mode before the os_get_opt() below.
     *
     * No need for a help since this should never be called by the user
     * directly.
     */
    if (argc == 2 && strcmp(argv[1], "boot") == 0)
    {
        if (!osp_pki_setup())
        {
            LOGE("PKIM bootstrap failed.");
        }
        else
        {
            LOGI("PKIM bootstrap success.");
        }
        exit(0);
    }

    /* Parse command-line arguments */
    if (os_get_opt(argc, argv, &pkim_log_severity))
    {
        return -1;
    }

    /* Enable runtime severity updates */
    log_register_dynamic_severity(loop);

    backtrace_init();

    json_memdbg_init(loop);

    MEMZERO(ref_time);
    ref_time.tm_year = 2020 - 1900; /* Reference year to check the valid date */
    ref_time.tm_mday = 1;
    ref_time.tm_mon = 1;

    time_t valid_time = mktime(&ref_time);
    while (now < valid_time)
    {
        /* Wait until current time is valid */
        sleep(5);
        now = time(NULL);
    }

    /* Install signal handler for USR2 */
    struct sigaction sa;
    sa.sa_handler = pki_cert_expiry_sigusr2;
    sigemptyset(&(sa.sa_mask));
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR2, &sa, NULL) != 0)
    {
        LOGW("Error installing USR2 signal handler.");
    }

    /* Connect to OVSDB */
    if (!ovsdb_init_loop(loop, "PKIM"))
    {
        LOGE("Initializing PKIM "
             "(Failed to initialize OVSDB)");
        return -1;
    }

    OVSDB_TABLE_INIT_NO_KEY(PKI_Config);
    OVSDB_TABLE_MONITOR(PKI_Config, false);

    ev_run(loop, 0);

    if (!ovsdb_stop_loop(loop))
    {
        LOGE("Stopping PKIM (Failed to stop OVSDB)");
    }

    ev_default_destroy();

    LOGN("Exiting PKIM");

    return 0;
}
