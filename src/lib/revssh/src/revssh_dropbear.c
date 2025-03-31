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
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>

#include "revssh.h"

#include "osn_types.h"
#include "daemon.h"
#include "memutil.h"
#include "execsh.h"
#include "const.h"
#include "util.h"
#include "log.h"
#include "ev.h"
#include "evx.h"

/**
 *
 * @brief Reverse SSH implementation using dropbear.
 *
 * @addtogroup REVSSH
 *
 * @{
 */

#define REVSSH_DROPBEAR_TMP_AUTHORIZED_KEYS_FILE      CONFIG_REVSSH_TMP_DIR"/authorized_keys"
#define REVSSH_DROPBEAR_TMP_KEYFILE                   CONFIG_REVSSH_TMP_DIR"/keyfile"
#define REVSSH_DROPBEAR_SERV_PIDFILE                  CONFIG_REVSSH_TMP_DIR"/dropbear.pid"
#define REVSSH_DROPBEAR_CLIENT_PIDFILE                CONFIG_REVSSH_TMP_DIR"/dbclient.pid"

#define REVSSH_STATUS_REPORT_DEBOUNCE                 1.2  /* Seconds */

struct revssh
{
    char                      *rs_server_host;

    int                        rs_server_port;

    char                      *rs_server_user;

    char                      *rs_authorized_keys[REVSSH_AUTHORIZED_KEY_MAX];
    int                        rs_authorized_keys_num;

    osn_ipany_addr_t          *rs_remote_bind_addr;

    int                        rs_remote_bind_port;

    osn_ipany_addr_t          *rs_local_addr;

    int                        rs_local_port;

    int                        rs_session_max_time;

    int                        rs_idle_timeout;

    const char                *rs_keyfile;

    enum revssh_keytype        rs_tmpkey_type;
    int                        rs_tmpkey_bits;
    bool                       rs_tmpkey_generated;

    execsh_async_t             rs_ssh_server;

    execsh_async_t             rs_revssh_client;
    ev_debounce                rs_revssh_client_starter;

    enum revssh_tunnel_status  rs_tun_status;
    enum revssh_tunnel_status  rs_tun_status_prev;
    int                        rs_auth_fail_num;

    char                       rs_last_msg;

    revssh_status_fn_t        *rs_status_fn_cb;
    ev_debounce                rs_status_reporter;

    ev_timer                   rs_maxtime_guard;

    bool                       rs_session_orphaned;

    int                        rs_session_user_cnt;     /* Counter of logged in users. */

};

static bool revssh_tmpdir_create(revssh_t *self);
static bool revssh_tmpdir_remove(revssh_t *self);
static bool revssh_authorized_keys_setup(revssh_t *self);
static bool revssh_authorized_keys_rollback();
static bool revssh_node_key_get_gen(revssh_t *self, char *pubkey_out, size_t pubkey_out_len);
static bool revssh_tmpkey_generate(revssh_t *self);
static void revssh_tmpkey_cleanup(void);
static bool revssh_dropbear_server_start(revssh_t *self);
static bool revssh_dropbear_server_stop(revssh_t *self);
static bool revssh_dropbear_client_start(revssh_t *self, int delay);
static bool revssh_dropbear_client_stop(revssh_t *self);
static void revssh_tunnel_status_report(revssh_t *self, bool debounced);
static void revssh_tunnel_status_report_stop(revssh_t *self);
static void revssh_maxtime_guard_start(revssh_t *self);
static void revssh_maxtime_guard_stop(revssh_t *self);
static void revssh_stop_cleanup_all(revssh_t *self);

static void revssh_server_execsh_exit_fn(execsh_async_t *esa, int exit_status);
static void revssh_server_execsh_io_fn(execsh_async_t *esa, enum execsh_io io_type, const char *msg);
static void revssh_client_execsh_exit_fn(execsh_async_t *esa, int exit_status);
static void revssh_client_execsh_io_fn(execsh_async_t *esa, enum execsh_io io_type, const char *msg);
static void revssh_client_start_debounced(struct ev_loop *loop, struct ev_debounce *ev, int revent);
static void revssh_tunnel_status_report_debounced(struct ev_loop *loop, struct ev_debounce *ev, int revent);
static void revssh_maxtime_guard_evtimer(struct ev_loop *loop, ev_timer *watcher, int revents);

static bool revssh_pidfile_write(const char *pidfile_path, pid_t pid);
static bool revssh_pidfile_exists(const char *pidfile_path);
static bool revssh_pidfile_read(const char *pidfile_path, pid_t *pid);
static void revssh_pidfile_delete(const char *pidfile_path);
static bool revssh_tmp_authorized_key_exists(void);

/* This implementation supports only a singleton instance of revssh_t object -- a single
 * RevSSH session to be open at any given time. Note that multiple concurrent user reverse
 * SSH connections/sessions are possible with the same established RevSSH session. */
static int instance_cnt;

static bool revssh_init(revssh_t *self)
{
    memset(self, 0, sizeof(*self));

    self->rs_keyfile = CONFIG_REVSSH_DROPBEAR_KEYFILE;
    self->rs_server_port = REVSSH_DEFAULT_SERVER_PORT;

    self->rs_session_max_time = CONFIG_REVSSH_DEFAULT_SESSION_MAX_TIME * 60;
    self->rs_idle_timeout = CONFIG_REVSSH_DEFAULT_IDLE_TIMEOUT * 60;

    self->rs_remote_bind_addr = CALLOC(1, sizeof(osn_ipany_addr_t));
    osn_ipany_addr_from_str(self->rs_remote_bind_addr, REVSSH_DEFAULT_REMOTE_BIND_ADDR);

    self->rs_local_addr = CALLOC(1, sizeof(osn_ipany_addr_t));
    osn_ipany_addr_from_str(self->rs_local_addr, REVSSH_DEFAULT_LOCAL_ADDR);

    self->rs_local_port = REVSSH_DEFAULT_LOCAL_PORT;

    /* Create temporary directory for revssh resources: */
    if (!revssh_tmpdir_create(self))
    {
        FREE(self->rs_local_addr);
        return false;
    }

    /* Initialize ssh server execsh async handler: */
    execsh_async_init(&self->rs_ssh_server, revssh_server_execsh_exit_fn);
    execsh_async_set(&self->rs_ssh_server, NULL, revssh_server_execsh_io_fn);

    /* Initialize revssh client execsh async handler: */
    execsh_async_init(&self->rs_revssh_client, revssh_client_execsh_exit_fn);
    execsh_async_set(&self->rs_revssh_client, NULL, revssh_client_execsh_io_fn);

    /* Initialize revssh client auth failure delayed-retry starter: */
    ev_debounce_init2(
            &self->rs_revssh_client_starter,
            revssh_client_start_debounced,
            REVSSH_CLIENT_AUTH_RETRY_INTERVAL,
            REVSSH_CLIENT_AUTH_RETRY_INTERVAL);

    /* Initialize debouncer for RevSSH tunnel status reporting: */
    ev_debounce_init(&self->rs_status_reporter, revssh_tunnel_status_report_debounced, REVSSH_STATUS_REPORT_DEBOUNCE);

    /* Initialize max session time guard. After max session time (regardless if active or not),
     * the session is destroyed. */
    ev_timer_init(&self->rs_maxtime_guard, revssh_maxtime_guard_evtimer, self->rs_session_max_time, 0.0);

    return true;
}

revssh_t *revssh_new(void)
{
    revssh_t *self;

    if (instance_cnt != 0)
    {
        LOG(ERR, "revssh: Not creating a new revssh object, a singleton instance already exists");
        return false;
    }

    self = CALLOC(1, sizeof(revssh_t));

    if (!revssh_init(self))
    {
        LOG(ERR, "revssh: Error creating revssh object");
        FREE(self);
        return NULL;
    }

    instance_cnt++;
    return self;
}

/*
 * Stop all revssh services, cleanup temporary credentials and resources.
 */
static void revssh_stop_cleanup_all(revssh_t *self)
{
    if (self->rs_session_orphaned)
    {
        return;
    }
    self->rs_session_orphaned = true;

    LOG(NOTICE, "revssh: Breaking down and cleaning up the session");

    /* Stop session max time guard: */
    revssh_maxtime_guard_stop(self);

    /* Stop any pending tunnel-status-reporter jobs: */
    revssh_tunnel_status_report_stop(self);

    /* Stop the RevSSH client jobs: */
    revssh_dropbear_client_stop(self);

    /* Stop the local temporary dropbear server: */
    revssh_dropbear_server_stop(self);

    /* Rollback any temporary authorized_keys modifications: */
    revssh_authorized_keys_rollback();

    /* Cleanup temporary node keys if generated: */
    revssh_tmpkey_cleanup();

    /* Remove the dedicated revssh tmp directory completely: */
    revssh_tmpdir_remove(self);
}

static bool revssh_fini(revssh_t *self)
{
    int i;

    /* User initiated stopping of RevSSH session.
     *
     * One final and immediate (non-debounced) tunnel status report callback notification:
     */
    self->rs_tun_status = REVSSH_TUN_STATUS_DISCONNECTED;
    revssh_tunnel_status_report(self, false);

    /* Stop all revssh services, jobs and clients, cleanup all resources: */
    revssh_stop_cleanup_all(self);

    /* Now, free the revssh_t object resources: */
    FREE(self->rs_server_host);
    FREE(self->rs_server_user);
    for (i = 0; i < self->rs_authorized_keys_num; i++)
    {
        FREE(self->rs_authorized_keys[i]);
    }
    FREE(self->rs_remote_bind_addr);
    FREE(self->rs_local_addr);

    memset(self, 0, sizeof(*self));

    return true;
}

bool revssh_del(revssh_t *self)
{
    bool retval = true;

    if (!revssh_fini(self))
    {
        LOG(ERR, "revssh: Error destroying revssh tunnel object");
        retval = false;
    }

    instance_cnt--;
    FREE(self);
    return retval;
}

bool revssh_server_params_set(revssh_t *self, const char *host, int port, const char *user)
{
    if (host == NULL || user == NULL)
    {
        LOG(ERR, "revssh: Server host or server user configuration invalid");
        return false;
    }

    FREE(self->rs_server_host);
    FREE(self->rs_server_user);

    self->rs_server_host = STRDUP(host);

    if (port >= 0)
    {
        self->rs_server_port = port;
    }
    else
    {
        self->rs_server_port = REVSSH_DEFAULT_SERVER_PORT;
    }

    self->rs_server_user = STRDUP(user);

    LOG(INFO,
        "revssh: config: server params: host=%s, port=%d, user=%s",
        self->rs_server_host,
        self->rs_server_port,
        self->rs_server_user);

    return true;
}

bool revssh_authorized_keys_add(revssh_t *self, const char *pubkey)
{
    if (pubkey == NULL)
    {
        return false;
    }

    if (self->rs_authorized_keys_num >= REVSSH_AUTHORIZED_KEY_MAX)
    {
        LOG(ERR,
            "revssh: Cannot add pubkey to authorized_keys list, max keys (%d) reached.",
            REVSSH_AUTHORIZED_KEY_MAX);
        return false;
    }

    self->rs_authorized_keys[self->rs_authorized_keys_num++] = STRDUP(pubkey);

    LOG(INFO,
            "revssh: config: authorized_keys: add: %s",
            self->rs_authorized_keys[self->rs_authorized_keys_num - 1]);

    return true;
}

bool revssh_tunnel_params_set(
        revssh_t *self,
        osn_ipany_addr_t *remote_bind_addr,
        int remote_bind_port,
        osn_ipany_addr_t *local_addr,
        int local_port)
{
    if (remote_bind_addr != NULL)
    {
        FREE(self->rs_remote_bind_addr);
        self->rs_remote_bind_addr = MEMNDUP(remote_bind_addr, sizeof(*remote_bind_addr));
    }

    if (remote_bind_port < 0)
    {
        LOG(ERR, "revssh: Invalid remote bind port %d", remote_bind_port);
        return false;
    }
    self->rs_remote_bind_port = remote_bind_port;

    if (local_addr != NULL)
    {
        FREE(self->rs_local_addr);
        self->rs_local_addr = MEMNDUP(local_addr, sizeof(*local_addr));
    }

    if (local_port >= 0)
    {
        self->rs_local_port = local_port;
    }

    LOG(INFO,
        "revssh: config: tunnel params: remote_bind_addr=%s, remote_bind_port=%d"
        ", local_addr=%s, local_port=%d",
        FMT_osn_ipany_addr(*self->rs_remote_bind_addr),
        self->rs_remote_bind_port,
        FMT_osn_ipany_addr(*self->rs_local_addr),
        self->rs_local_port);

    return true;
}

bool revssh_timeout_set(revssh_t *self, int session_max_time, int idle_timeout)
{
    if (session_max_time > 0)
    {
        self->rs_session_max_time = session_max_time * 60;
    }
    if (idle_timeout > 0)
    {
        self->rs_idle_timeout = idle_timeout * 60;
    }

    LOG(INFO,
        "revssh: config: session_max_time=%d, idle_timeout=%d",
        self->rs_session_max_time, self->rs_idle_timeout);

    return true;
}

bool revssh_notify_status_callback_set(revssh_t *self, revssh_status_fn_t *status_fn_cb)
{
    self->rs_status_fn_cb = status_fn_cb;
    return true;
}

/* Create a directory in tmpfs for revssh temporary resources. */
static bool revssh_tmpdir_create(revssh_t *self)
{
    int rc;

    /* Create temporary directory for authorized_keys: */
    rc = execsh_log(LOG_SEVERITY_DEBUG, _S(mkdir "-p" "$1"), CONFIG_REVSSH_TMP_DIR);
    if (rc != 0)
    {
        LOG(ERR, "revssh: Unable to create dir: %s", CONFIG_REVSSH_TMP_DIR);
        return false;
    }
    return true;
}

/* Delete temporary revssh directory. */
static bool revssh_tmpdir_remove(revssh_t *self)
{
    int rc;

    rc = execsh_log(LOG_SEVERITY_DEBUG, _S(rm "-rf" "$1"), CONFIG_REVSSH_TMP_DIR);
    if (rc != 0)
    {
        LOG(ERR, "revssh: Unable to remove dir: %s", CONFIG_REVSSH_TMP_DIR);
        return false;
    }
    return true;
}

/*
 * Setup temporary authorized_keys file: it will have all the contents of the original one
 * plus public keys configured. The temporary keys file is setup in a way for easy rollback to
 * the original contents.
 */
static bool revssh_authorized_keys_setup(revssh_t *self)
{
    bool rv = false;
    int rc;
    int i;

    /* If dropbear authorized_keys does not exist, create an empty one: */
    execsh_log(
            LOG_SEVERITY_DEBUG,
            _S([ ! -e "$1" ] && touch "$1"),
            CONFIG_REVSSH_DROPBEAR_AUTHORIZED_KEYS_FILE);

    /* Copy current contents of dropbear authorized_keys to temporary authorized_keys: */
    rc = execsh_log(
            LOG_SEVERITY_DEBUG,
            _S(cp "$1" "$2"),
            CONFIG_REVSSH_DROPBEAR_AUTHORIZED_KEYS_FILE,
            REVSSH_DROPBEAR_TMP_AUTHORIZED_KEYS_FILE);
    if (rc != 0)
    {
        LOG(ERR,
            "revssh: Unable to copy %s to %s",
            CONFIG_REVSSH_DROPBEAR_AUTHORIZED_KEYS_FILE,
            REVSSH_DROPBEAR_TMP_AUTHORIZED_KEYS_FILE);
        goto end;
    }

    /* Bind-mount the temporary authorized_keys file to the path picked by dropbear: */
    rc = execsh_log(
            LOG_SEVERITY_DEBUG,
            _S(mount "--bind" "$1" "$2"),
            REVSSH_DROPBEAR_TMP_AUTHORIZED_KEYS_FILE,
            CONFIG_REVSSH_DROPBEAR_AUTHORIZED_KEYS_FILE);
    if (rc != 0)
    {
        LOG(ERR,
            "revssh: Unable to bind-mount %s to %s",
            REVSSH_DROPBEAR_TMP_AUTHORIZED_KEYS_FILE,
            CONFIG_REVSSH_DROPBEAR_AUTHORIZED_KEYS_FILE);
        goto end;
    }

    /* Append all server-side pubkeys to the temporary authorized_keys file: */
    for (i = 0; i < self->rs_authorized_keys_num; i++)
    {
        rc = execsh_log(
                LOG_SEVERITY_DEBUG,
                _S(echo "$1" >> "$2"),
                self->rs_authorized_keys[i],
                CONFIG_REVSSH_DROPBEAR_AUTHORIZED_KEYS_FILE);
        if (rc != 0)
        {
            LOG(ERR, "revssh: Unable add server-side pubkey num %d to temporary authorized_keys file", i);
            goto end;
        }
    }

    rv = true;
end:
    if (!rv)
    {
        revssh_authorized_keys_rollback();
    }
    return rv;
}

/* Rollback the authorized_keys file to the original one. */
static bool revssh_authorized_keys_rollback(void)
{
    bool rv = true;
    int rc;

    /* Unmount the temporary authorized_keys file: */
    rc = execsh_log(LOG_SEVERITY_DEBUG, _S(umount "$1"), CONFIG_REVSSH_DROPBEAR_AUTHORIZED_KEYS_FILE);
    if (rc != 0)
    {
        LOG(INFO,
            "revssh: Unable to unmount %s (might already be unmounted)",
            CONFIG_REVSSH_DROPBEAR_AUTHORIZED_KEYS_FILE);
        rv = false;
    }

    /* Remove the temporary authorized_keys file: */
    rc = execsh_log(LOG_SEVERITY_DEBUG, _S(rm "-f" "$1"), REVSSH_DROPBEAR_TMP_AUTHORIZED_KEYS_FILE);
    if (rc != 0)
    {
        LOG(ERR,
            "revssh: Unable to remove temporary authorized_keys file: %s",
            REVSSH_DROPBEAR_TMP_AUTHORIZED_KEYS_FILE);
        rv = false;
    }

    return rv;
}

/* RevSSH server at-exit callback. */
static void revssh_server_execsh_exit_fn(execsh_async_t *esa, int exit_status)
{
    revssh_t *self = CONTAINER_OF(esa, revssh_t, rs_ssh_server);

    LOG(NOTICE, "revssh: dropbear server: exited, exit_status=%d", exit_status);

    /* At dropbear server exit, we need to end the whole RevSSH session. */

    /* One final and immediate (non-debounced) tunnel status report callback notification: */
    revssh_tunnel_status_report(self, false);

    /* Break up the session and cleanup everything: */
    revssh_stop_cleanup_all(self);
}

/* RevSSH server output handler: */
static void revssh_server_execsh_io_fn(execsh_async_t *esa, enum execsh_io io_type, const char *msg)
{
    revssh_t *self = CONTAINER_OF(esa, revssh_t, rs_ssh_server);
    const char *err_str_addr_in_use = "Error listening: Address already in use";
    const char *str_pubkey_auth_succeess = "Pubkey auth succeeded for";
    const char *str_disconnect_received = "Disconnect received";
    const char *str_idle_timeout = "Idle timeout";

    if (strlen(msg) == 0)
    {
        return;
    }

    LOG(INFO, "revssh: dropbear server: '%s'", msg);

    if (strstr(msg, err_str_addr_in_use) != NULL) /* Address already in use */
    {
        self->rs_tun_status = REVSSH_TUN_STATUS_INIT_ERR;
    }
    else if (strstr(msg, str_pubkey_auth_succeess) != NULL) /* Pubkey auth success, user logged in */
    {
        self->rs_session_user_cnt++;

        /* RevSSH tunnel established + at least 1 user logged in: tunnel status == active: */
        self->rs_tun_status = REVSSH_TUN_STATUS_ACTIVE;
    }
    else if (strstr(msg, str_disconnect_received) != NULL || strstr(msg, str_idle_timeout) != NULL)
    {
        /* User disconnected / logged out OR User disconnected / idle timeout */
        if (--self->rs_session_user_cnt == 0)
        {
            /* RevSSH tunnel established, no user loged in: */
            self->rs_tun_status = REVSSH_TUN_STATUS_ESTABLISHED;
        }
    }

    /* Debounced tunnel status report: */
    revssh_tunnel_status_report(self, true);
}

/* Start temporary dropbear ssh server instance. */
static bool revssh_dropbear_server_start(revssh_t *self)
{
    pid_t server_pid;

    /* Start dropbear server:
     * -F: Do not fork into background, -s, -g: Disable password logins for all including root
     */
    server_pid = execsh_async_start(
            &self->rs_ssh_server,
            _S("$1" -F -s -g -p "$2":"$3" -I "$4" -P "$5"),
            CONFIG_REVSSH_DROPBEAR_SERVER,
            FMT_osn_ipany_addr(*self->rs_local_addr),
            FMT_int(self->rs_local_port),
            FMT_int(self->rs_idle_timeout),
            REVSSH_DROPBEAR_SERV_PIDFILE);

    if (server_pid == -1)
    {
        LOG(ERR, "revssh: Error starting dropbear server");
        return false;
    }

    LOG(NOTICE,
        "revssh: Dropbear server daemon started, listening on: %s:%d, pid=%d",
        FMT_osn_ipany_addr(*self->rs_local_addr),
        self->rs_local_port,
        server_pid);

    return true;
}

/* Stop temporary drobear ssh server instance. */
static bool revssh_dropbear_server_stop(revssh_t *self)
{
    execsh_async_stop(&self->rs_ssh_server);
    return true;
}

/* Debounced tunnel status reporting: */
static void revssh_tunnel_status_report_debounced(struct ev_loop *loop, struct ev_debounce *ev, int revent)
{
    revssh_t *self = CONTAINER_OF(ev, revssh_t, rs_status_reporter);

    revssh_tunnel_status_report(self, false);
}

/* Schedule tunnel status report: */
static void revssh_tunnel_status_report(revssh_t *self, bool debounced)
{
    if (debounced)
    {
        ev_debounce_start(EV_DEFAULT, &self->rs_status_reporter);
        return;
    }

    if (self->rs_tun_status_prev != self->rs_tun_status)
    {
        LOG(NOTICE,
            "revssh: tunnel status changed: %s --> %s",
            revssh_tunnel_status_tostr(self->rs_tun_status_prev),
            revssh_tunnel_status_tostr(self->rs_tun_status));

        /* Notify about the new status via registered callback: */
        if (self->rs_status_fn_cb != NULL)
        {
            struct revssh_status revssh_status = { 0 };
            revssh_status.rs_tun_status = self->rs_tun_status;

            self->rs_status_fn_cb(self, &revssh_status);
        }

        self->rs_tun_status_prev = self->rs_tun_status;
    }
}

/* Stop any pending tunnel status report: */
static void revssh_tunnel_status_report_stop(revssh_t *self)
{
    ev_debounce_stop(EV_DEFAULT, &self->rs_status_reporter);
}

/* RevSSH client at-exit callback. */
static void revssh_client_execsh_exit_fn(execsh_async_t *esa, int exit_status)
{
    revssh_t *self = CONTAINER_OF(esa, revssh_t, rs_revssh_client);
    bool session_destroy_cleanup = true;

    LOG(INFO, "revssh: dropbear RevSSH client: exited: exit_status=%d", exit_status);

    /*
     * At drobear client exit the further action depends on current status.
     * We may want to retry or we may want to give up.
     */

    /*
     * RevSSH client authentication failure handling:
     *
     * The logic is as follows: The client is allowed max REVSSH_CLIENT_AUTH_RETRY_MAX auth failures
     * and if auth failure it retries to connect every REVSSH_CLIENT_AUTH_RETRY_INTERVAL interval.
     *
     * This is to allow time for the user to add the node's public key to the RevSSH server's
     * authorized_keys list: When this is done, the revssh client would eventually succeed login in
     * into the RevSSH server and establishing a reverse ssh tunnel with remote TCP port forwarding.
     */
    if (self->rs_tun_status == REVSSH_TUN_STATUS_AUTH_FAILURE)
    {
        if (++self->rs_auth_fail_num < REVSSH_CLIENT_AUTH_RETRY_MAX)
        {
            /* For auth failure keep retrying to connect for max retry times: */
            self->rs_tun_status = REVSSH_TUN_STATUS_CONNECTING;

            LOG(INFO,
                "revssh: RevSSH client: Authentication failure. Retrying in %d seconds",
                REVSSH_CLIENT_AUTH_RETRY_INTERVAL);

            /* Retry after some time: */
            revssh_dropbear_client_start(self, REVSSH_CLIENT_AUTH_RETRY_INTERVAL);

            /* In this case we do not destroy/cleanup the RevSSH session, for now. */
            session_destroy_cleanup = false;
        }
        else
        {
            /* Max number of auth failure retries reached. Give up. */
            LOG(INFO, "revssh: RevSSH client: Authentication failure. Max retries reached. Giving up.");
        }
    }

    if (session_destroy_cleanup)
    {
        /* One final and immediate (non-debounced) tunnel status report callback notification: */
        revssh_tunnel_status_report(self, false);

        /* Break up the session and cleanup everything: */
        revssh_stop_cleanup_all(self);
    }
}

/* RevSSH client output (stdout/stderr) callback. Called for each line of RevSSH output. */
static void revssh_client_execsh_io_fn(execsh_async_t *esa, enum execsh_io io_type, const char *msg)
{
    revssh_t *self = CONTAINER_OF(esa, revssh_t, rs_revssh_client);
    const char *err_str_conn_fail = "exited: Connect failed";
    const char *err_str_algo_no_match = "exited: No matching algo hostkey";
    const char *err_str_exited_interrupted = "exited: Interrupted.";
    const char *err_str_password = "'s password:";
    const char *err_str_auth = "exited: No auth methods could be used";
    const char *err_str_idle_timeout = "exited: Idle timeout";
    const char *err_str_remote_forward_fail = "Remote TCP forward request failed";
    const char *str_auth_success = "Authentication succeeded.";
    const char *str_terminated = "Terminated";
    const char *str_exited_generic = "exited: ";

    if (strlen(msg) == 0)
    {
        return;
    }

    LOG(INFO, "revssh: dropbear client: '%s'", msg);

    if (strstr(msg, err_str_conn_fail) != NULL)
    {
        self->rs_tun_status = REVSSH_TUN_STATUS_CONNECT_FAILURE;
    }
    else if (strstr(msg, err_str_algo_no_match) != NULL) /* Pre-auth failure (key algos negotiation failure) */
    {
        self->rs_tun_status = REVSSH_TUN_STATUS_PREAUTH_FAILURE;
    }
    else if (strstr(msg, err_str_auth) != NULL) /* Authentication failure */
    {
        self->rs_tun_status = REVSSH_TUN_STATUS_AUTH_FAILURE;
    }
    else if (strstr(msg, err_str_password) != NULL) /* RevSSH server asks for a password -- Authentication failure */
    {
        /*
         * RevSSH server asking for a password is regarded as authentication failure as well.
         *
         * This would happen if a ssh server does not yet have our public key in its authorized_keys
         * list and is configured to allow password logins as well. OpenSync implementation does
         * not support password logins.
         *
         */
        self->rs_tun_status = REVSSH_TUN_STATUS_AUTH_FAILURE;
    }
    else if (strstr(msg, err_str_exited_interrupted) != NULL)
    {
        /*
         * This is more of a workaround handling for older Dropbear versions (v2019.78 for instance)
         * for the same case as above, i.e. when the ssh server does not yet have our public key
         * in its authorized_keys list and is configured to allow password logins as well.
         * On some (older) versions of Dropbear we don't see the "'s password:" on its stdout,
         * but rather only the "exited: Interrupted." string. Such cases need to be regarded as
         * "auth failure" as well.
         */
        self->rs_tun_status = REVSSH_TUN_STATUS_AUTH_FAILURE;
    }
    else if (strstr(msg, err_str_idle_timeout) != NULL)
    {
        self->rs_tun_status = REVSSH_TUN_STATUS_DISCONNECTED_IDLE;
    }
    else if (strstr(msg, err_str_remote_forward_fail) != NULL)
    {
        self->rs_tun_status = REVSSH_TUN_STATUS_REMOTE_FWD_FAILURE;
    }
    else if (strstr(msg, str_auth_success) != NULL)
    {
        /*
         * On "Authentication succeeded" msg, first assume the tunnel was established.
         *
         * If it really was established, this will be the only and last message printed.
         *
         * If it actually will be a failure (e.g. further remote TCP forward failure, etc), the
         * "established" status will get overwritten promptly afterwards.
         */
        self->rs_tun_status = REVSSH_TUN_STATUS_ESTABLISHED;
    }
    else if (strstr(msg, str_terminated) != NULL)
    {
        self->rs_tun_status = REVSSH_TUN_STATUS_DISCONNECTED;
    }
    else if (strstr(msg, str_exited_generic) != NULL)
    {
        /*
         * Unless status previously determined as "auth failure",
         * regard this as a generic "disconnected" status.
         */
        if (self->rs_tun_status != REVSSH_TUN_STATUS_AUTH_FAILURE)
        {
            self->rs_tun_status = REVSSH_TUN_STATUS_DISCONNECTED;
        }
    }

    /* Debounced tunnel status report: */
    revssh_tunnel_status_report(self, true);

    if (self->rs_tun_status == REVSSH_TUN_STATUS_REMOTE_FWD_FAILURE)
    {
        /* In this case: (address already in used at RevSSH server side, i.e bind error) the
         * dropbear client seems to stay connected and does not exit. We must kill/force it
         * to exit.
         */
        revssh_dropbear_client_stop(self);
    }
}

/* Start revssh dropbear client. Either now (delay==0) or after the specified number of seconds. */
static bool revssh_dropbear_client_start(revssh_t *self, int delay)
{
    char revssh_client_cmd[512];
    pid_t client_pid;

    /* Start with a delay: */
    if (delay > 0)
    {
        ev_debounce_start(EV_DEFAULT, &self->rs_revssh_client_starter);
        return true;
    }

    /* Or start immediately: */

    /*
     * drobear client: connect to ssh server with remote port forwarding: Started in the following way:
     *
     * dbclient -i <keyfile> -l <user> -N -T -R<bind_addr>:<port>:<local_addr>:<local_port> <server_host> \
     *         -y -p <server_port> -I <idle_timeout> -o UseSyslog=yes
     *
     * The -o UseSyslog=yes is important to get the "Authentication succeeded" msg to reliably
     * determine the tunnel/session established status. Note: For the UseSyslog extended option to
     * be available, Drobear must NOT be configured with --disable-syslog.
     */

    memset(revssh_client_cmd, 0, sizeof(revssh_client_cmd));
    snprintf(
            revssh_client_cmd,
            sizeof(revssh_client_cmd),
            "%s -i %s -l %s -N -T -R%s:%d:%s:%d %s -y -p %d -I %d -o UseSyslog=yes",
            CONFIG_REVSSH_DROPBEAR_CLIENT,
            self->rs_keyfile,
            self->rs_server_user,
            FMT_osn_ipany_addr(*self->rs_remote_bind_addr),
            self->rs_remote_bind_port,
            FMT_osn_ipany_addr(*self->rs_local_addr),
            self->rs_local_port,
            self->rs_server_host,
            self->rs_server_port,
            self->rs_idle_timeout);

    client_pid = execsh_async_start(&self->rs_revssh_client, revssh_client_cmd);

    if (client_pid == -1)
    {
        LOG(ERR, "revssh: Error starting revssh client");
        return false;
    }

    if (!revssh_pidfile_write(REVSSH_DROPBEAR_CLIENT_PIDFILE, client_pid))
    {
        LOG(WARN, "revssh: Failed writing dropbear client pidfile");
    }

    LOG(INFO, "revssh: Started revssh client: pid=%d", client_pid);
    return true;
}

static void revssh_client_start_debounced(struct ev_loop *loop, struct ev_debounce *ev, int revent)
{
    revssh_t *self = CONTAINER_OF(ev, revssh_t, rs_revssh_client_starter);

    revssh_dropbear_client_start(self, 0);
}

/* Stop the revssh dropbear client. */
static bool revssh_dropbear_client_stop(revssh_t *self)
{
    /* Stop any pending debounce revssh client starter jobs: */
    ev_debounce_stop(EV_DEFAULT, &self->rs_revssh_client_starter);

    /* Stop any running revssh client async jobs: */
    execsh_async_stop(&self->rs_revssh_client);

    revssh_pidfile_delete(REVSSH_DROPBEAR_CLIENT_PIDFILE);

    return true;
}

/* Session max time guard timer expired handler: */
static void revssh_maxtime_guard_evtimer(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    revssh_t *self = CONTAINER_OF(watcher, revssh_t, rs_maxtime_guard);

    LOG(NOTICE, "revssh: Session max time (%d seconds) reached", self->rs_session_max_time);

    self->rs_tun_status = REVSSH_TUN_STATUS_DISCONNECTED_MAXTIME;

    /* One final and immediate (non-debounced) tunnel status report callback notification: */
    revssh_tunnel_status_report(self, false);

    /* Break up the session and cleanup everything: */
    revssh_stop_cleanup_all(self);
}

/* Start session max time guard. */
static void revssh_maxtime_guard_start(revssh_t *self)
{
    LOG(DEBUG, "revssh: Starting maxtime guard");

    ev_timer_set(&self->rs_maxtime_guard, self->rs_session_max_time, 0.0);
    ev_timer_start(EV_DEFAULT, &self->rs_maxtime_guard);
}

/* Stop session max time guard. */
static void revssh_maxtime_guard_stop(revssh_t *self)
{
    LOG(DEBUG, "revssh: Stopping maxtime guard");

    ev_timer_stop(EV_DEFAULT, &self->rs_maxtime_guard);
}

bool revssh_start(revssh_t *self)
{
    bool rv = false;

    if (self->rs_session_orphaned)
    {
        LOG(WARN,
            "revssh: Ignoring revssh start request as the previous session is already over. "
            "A new session must be initalized.");
        return false;
    }

    /* Validate configuration first: */
    if (self->rs_server_host == NULL || self->rs_server_port < 0 || self->rs_server_user == NULL)
    {
        LOG(ERR, "revssh: RevSSH server paramters not configured");
        return false;
    }
    if (self->rs_authorized_keys_num == 0)
    {
        LOG(ERR, "revssh: No authorized keys configured");
        return false;
    }
    if (self->rs_local_port < 0)
    {
        LOG(ERR, "revssh: ssh tunnel local port not configured");
        return false;
    }
    if (self->rs_remote_bind_addr == NULL || self->rs_remote_bind_port < 0 || self->rs_local_addr == NULL)
    {
        LOG(ERR, "revssh: reverse ssh tunnel parameters invalid");
        return false;
    }
    if (self->rs_tmpkey_type != REVSSH_KEYTYPE_NONE)
    {
        if (!self->rs_tmpkey_generated)
        {
            self->rs_tun_status = REVSSH_TUN_STATUS_INIT_ERR;

            LOG(ERR, "revssh: Temporary node key generation failed");
            revssh_tunnel_status_report(self, false);
            return false;
        }
    }

    /* Start the various components of a RevSSH session: */

    if (!revssh_authorized_keys_setup(self))
    {
        LOG(ERR, "revssh: Error setting up authorized keys list");
        goto end;
    }
    if (!revssh_dropbear_server_start(self))
    {
        LOG(ERR, "revssh: Error starting dropbear server");
        goto end;
    }
    if (!revssh_dropbear_client_start(self, 0))
    {
        LOG(ERR, "revssh: Error starting dropbear revssh client");
        goto end;
    }
    revssh_maxtime_guard_start(self);

    rv = true;
end:
    if (!rv)
    {
        revssh_stop_cleanup_all(self);
    }
    return rv;
}

/*
 * Get public key of the node keypair. This is either node's default keypair or
 * a temporary keypair generated just for this session if requested by config.
 */
static bool revssh_node_key_get_gen(revssh_t *self, char *pubkey_out, size_t pubkey_out_len)
{
    char cmd[C_MAXPATH_LEN];
    char buffer[2048];
    char pubkey[2048];
    char *line;
    FILE *f = NULL;
    bool rv = false;

    if (!is_input_shell_safe(self->rs_keyfile)) return false;

    if (self->rs_tmpkey_type != REVSSH_KEYTYPE_NONE && !self->rs_tmpkey_generated)
    {
        /* If tmp key to be generated (and not yet generated) */

        snprintf(
                cmd,
                sizeof(cmd),
                "%s -t %s -s %d -f %s",
                CONFIG_REVSSH_DROPBEAR_KEYTOOL,
                revssh_keytype_tostr(self->rs_tmpkey_type),
                self->rs_tmpkey_bits,
                self->rs_keyfile);
    }
    else
    {
        /* If node's default "static" key is to be used. */

        snprintf(
                cmd,
                sizeof(cmd),
                "%s -y -f %s",
                CONFIG_REVSSH_DROPBEAR_KEYTOOL,
                self->rs_keyfile);
    }

    LOG(TRACE, "revssh: Running cmd: %s", cmd);

    f = popen(cmd, "r");
    if (f == NULL)
    {
        LOG(ERR, "revssh: Error running cmd %s: %s", cmd, strerror(errno));
        return false;
    }

    while ((line = fgets(buffer, sizeof(buffer), f)) != NULL)
    {
        if (strncmp(line, "ssh-rsa", strlen("ssh-rsa")) == 0
                || strncmp(line, "ecdsa-", strlen("ecdsa-")) == 0
                || strncmp(line, "ssh-ed25519", strlen("ssh-ed25519")) == 0)
        {
            if (line[strlen(line) - 1] == '\n')
            {
                line[strlen(line) - 1] = '\0';
            }

            strscpy(pubkey, line, sizeof(pubkey));
            rv = true;
            break;
        }
    }

    if (rv)
    {
        if (self->rs_tmpkey_type != REVSSH_KEYTYPE_NONE && !self->rs_tmpkey_generated)
        {
            self->rs_tmpkey_generated = true;

            LOG(INFO,
                "revssh: Generated node temporary keypair: type: %s: size=%d, pubkey=%s",
                revssh_keytype_tostr(self->rs_tmpkey_type),
                self->rs_tmpkey_bits,
                pubkey);
        }

        if (pubkey_out != NULL)
        {
            strscpy(pubkey_out, pubkey, pubkey_out_len);
        }
    }

out:
    if (f != NULL)
    {
        pclose(f);
    }
    return rv;
}

static bool revssh_tmpkey_generate(revssh_t *self)
{
    bool rv;

    LOG(INFO, "revssh: Generating temporary keypair");

    rv = revssh_node_key_get_gen(self, NULL, 0);
    if (!rv)
    {
        LOG(ERR, "revssh: Error generating temporary keypair");
    }
    return rv;
}

static void revssh_tmpkey_cleanup(void)
{
    unlink(REVSSH_DROPBEAR_TMP_KEYFILE);
}

bool revssh_node_pubkey_get(revssh_t *self, char *pubkey, size_t len)
{
    return revssh_node_key_get_gen(self, pubkey, len);
}

bool revssh_tmpkeygen_set(revssh_t *self, enum revssh_keytype type, int bits)
{
    /* If default key bitsize is to be used: */
    if (bits == -1)
    {
        if (type == REVSSH_KEYTYPE_RSA)
            bits = REVSSH_DEFAULT_KEYSIZE_RSA;
        else if (type == REVSSH_KEYTYPE_ECDSA)
            bits = REVSSH_DEFAULT_KEYSIZE_ECDSA;
        else if (type == REVSSH_KEYTYPE_ED25519)
            bits = REVSSH_KEYSIZE_ED25519;
    }

    /* Validate parameters: */
    if (!(type == REVSSH_KEYTYPE_RSA || type == REVSSH_KEYTYPE_ECDSA || type == REVSSH_KEYTYPE_ED25519))
    {
        LOG(ERR, "revssh: tmp key gen: Type must be either RSA, ECDSA or Ed25519");
        return false;
    }
    if (type == REVSSH_KEYTYPE_RSA)
    {
        if (!(bits >= 512 && bits <= 4096) || (bits % 8) != 0)
        {
            LOG(ERR, "revssh: tmp key gen: type==RSA: key_bits must be >= 512 and <= 4096, and be a multiple of 8");
            return false;
        }
    }
    else if (type == REVSSH_KEYTYPE_ECDSA)
    {
        if (!(bits == 256 || bits == 384 || bits == 521))
        {
            LOG(ERR, "revssh: tmp key gen: type==ECDSA: key_bits must be one of: 256, 384 or 521");
            return false;
        }
    }
    else if (type == REVSSH_KEYTYPE_ED25519)
    {
        if (bits != 256)
        {
            LOG(ERR, "revssh: tmp key gen: type==Ed25519: key_bits must have a fixed size of 256");
            return false;
        }
    }

    self->rs_tmpkey_type = type;
    self->rs_tmpkey_bits = bits;

    LOG(INFO,
        "revssh: config: gen tmpkey: type=%s, bits=%d",
        revssh_keytype_tostr(self->rs_tmpkey_type),
        self->rs_tmpkey_bits);

    /* Set keyfile path to a temporary keyfile path, make sure it does not exist: */
    revssh_tmpkey_cleanup();
    self->rs_keyfile = REVSSH_DROPBEAR_TMP_KEYFILE;

    /* Generate the temporary key: */
    return revssh_tmpkey_generate(self);
}

const char *revssh_tunnel_status_tostr(enum revssh_tunnel_status status)
{
    const char *tunstatus_str[REVSSH_TUN_STATUS_MAX + 1] =
    {
        #define _STR(sym, str) [sym] = str,
        REVSSH_TUNSTATUS(_STR)
        #undef _STR
    };

    if (status < REVSSH_TUN_STATUS_MAX)
    {
        return tunstatus_str[status];
    }
    else
    {
        return tunstatus_str[REVSSH_TUN_STATUS_UNKNOWN];
    }
}

const char *revssh_keytype_tostr(enum revssh_keytype type)
{
    const char *keytype_str[REVSSH_KEYTYPE_MAX + 1] =
    {
        #define _STR(sym, str) [sym] = str,
        REVSSH_KEYTYPE(_STR)
        #undef _STR
    };

    if (type < REVSSH_KEYTYPE_MAX)
    {
        return keytype_str[type];
    }
    else
    {
        return keytype_str[REVSSH_KEYTYPE_NONE];
    }
}

enum revssh_keytype revssh_keytype_fromstr(const char *str)
{
    int keytype;
    const char *keytype_str[REVSSH_KEYTYPE_MAX + 1] =
    {
        #define _STR(sym, str) [sym] = str,
        REVSSH_KEYTYPE(_STR)
        #undef _STR
    };

    for (keytype = 0; keytype < REVSSH_KEYTYPE_MAX; keytype++)
    {
        if (strcmp(str, keytype_str[keytype]) == 0)
        {
            return keytype;
        }
    }
    return REVSSH_KEYTYPE_NONE;
}

static bool revssh_pidfile_write(const char *pidfile_path, pid_t pid)
{
    FILE *fp;
    bool rv = false;

    fp = fopen(pidfile_path, "w");
    if (fp == NULL)
    {
        LOG(ERR, "revssh: Error opening pidfile for writing: %s", pidfile_path);
        return false;
    }
    if (fprintf(fp, "%d\n", pid) <= 0)
    {
        LOG(ERR, "revssh: Error writing pid file: %s", pidfile_path);
        goto end;
    }
    fclose(fp);
    rv = true;
end:
    return rv;
}

static bool revssh_pidfile_exists(const char *pidfile_path)
{
    return access(pidfile_path, R_OK) == 0 ? true : false;
}

static bool revssh_tmp_authorized_key_exists(void)
{
    return access(REVSSH_DROPBEAR_TMP_AUTHORIZED_KEYS_FILE, R_OK) == 0 ? true : false;
}

static bool revssh_pidfile_read(const char *pidfile_path, pid_t *pid)
{
    FILE *fp;
    char buf[C_PID_LEN];
    bool rv = false;

    fp = fopen(pidfile_path, "r");
    if (fp == NULL)
    {
        LOG(ERR, "revssh: Error opening pidfile for reading: %s", pidfile_path);
        goto end;
    }
    if (fgets(buf, sizeof(buf), fp) == NULL)
    {
        goto end;
    }
    errno = 0;
    *pid = strtoull(buf, NULL, 10);
    if (errno != 0)
    {
        goto end;
    }

    rv = true;
end:
    if (fp != NULL) fclose(fp);
    if (!rv)
    {
        LOG(ERR, "revssh: Error reading pidfile: %s", pidfile_path);
    }
    return rv;
}

static void revssh_pidfile_delete(const char *pidfile_path)
{
    unlink(pidfile_path);
}

void revssh_cleanup_dangling_sessions(void)
{
    pid_t pid;

    if (revssh_pidfile_exists(REVSSH_DROPBEAR_CLIENT_PIDFILE))
    {
        LOG(NOTICE, "revssh: Dangling dropbear revssh client detected. Attempt to kill it");

        if (revssh_pidfile_read(REVSSH_DROPBEAR_CLIENT_PIDFILE, &pid))
        {
            /* Dropbear client is started via execsh_async that puts the shell and all its
             * children in a separate dedicated process group. Send signal to all processes in
             * the process group: */
            kill(-1 * pid, SIGTERM);

            /* dbclient does not remove the pid file itself. In fact, it does not even handle
             * pidfiles. This pidfile is exclusively our own record and responsibility. */
            revssh_pidfile_delete(REVSSH_DROPBEAR_CLIENT_PIDFILE);
        }
    }

    if (revssh_pidfile_exists(REVSSH_DROPBEAR_SERV_PIDFILE))
    {
        LOG(NOTICE, "revssh: Dangling dropbear revssh server instance detected. Attempt to kill it");

        if (revssh_pidfile_read(REVSSH_DROPBEAR_SERV_PIDFILE, &pid))
        {
            /* Dropbear server is also started via execsh_async, however it manages the
             * pidfile itself and the PID in it is directly the PID of dropbear process, not shell
             * that started it. Simply kill with SIGTERM. */
            kill(pid, SIGTERM);
        }
    }

    if (revssh_tmp_authorized_key_exists())
    {
        LOG(NOTICE, "revssh: Dangling temporary authorized_keys file detected. Rollback.");

        revssh_authorized_keys_rollback();
    }
}
