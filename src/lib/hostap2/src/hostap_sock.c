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

/* libc */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <glob.h>

/* opensync */
#include <ds_dlist.h>
#include <memutil.h>
#include <util.h>
#include <log.h>

/* unit */
#include <hostap_sock.h>

/* private */
#define HOSTAP_SOCK_FD_INVALID -1
#define HOSTAP_SOCK_CLI_PREFIX "/tmp/wpa_sock_"
#define HOSTAP_SOCK_CLI_SUFFIX "%d_%d"

struct hostap_sock {
    char *ctrl_path;
    char *cli_path;
    int fd;
};

static bool
hostap_sock_msg_is_event(const char *msg)
{
    const char *prefix1 = "<";
    const char *prefix2 = "IFNAME=";
    const size_t prefix1_len = strlen(prefix1);
    const size_t prefix2_len = strlen(prefix2);
    const bool prefix1_match = (strncmp(msg, prefix1, prefix1_len) == 0);
    const bool prefix2_match = (strncmp(msg, prefix2, prefix2_len) == 0);
    return prefix1_match || prefix2_match;
}

static int
hostap_sock_set_nonblock(int fd)
{
    const int flags = fcntl(fd, F_GETFL);
    const bool fcntl_failed = (flags < 0);
    if (fcntl_failed) return flags;
    const int new_flags = flags | O_NONBLOCK;
    return fcntl(fd, F_SETFL, new_flags);
}

static int
hostap_sock_set_bufsize(int fd)
{
    /* FIXME: This needs to set a bigger socket buffer size
     * explicitly to reduce chances of poll-to-poll
     * overruns.
     */
    return 0;
}

static int
hostap_sock_bind(struct hostap_sock *sock)
{
    struct sockaddr_un local = {0};
    const struct sockaddr *addr = (struct sockaddr *)&local;
    size_t addr_len = sizeof(local);
    local.sun_family = AF_UNIX;

    static int counter = 0;
    int tries_left = 10;
    do {
        const size_t max_len = sizeof(local.sun_path) - 1;
        const int len = snprintf(local.sun_path + 1,
                                 max_len,
                                 HOSTAP_SOCK_CLI_PREFIX
                                 HOSTAP_SOCK_CLI_SUFFIX,
                                 (int)getpid(),
                                 counter);
        /* First byte \0 means it's an abstract socket that
         * does _not_ reside on the VFS. This has the
         * benefit of not needing to perform unlink() /
         * cleanup - which is a problem when considering a
         * program can abruptly crash, or close the socket,
         * but forget to unlink it. Abstract socket closing
         * is visible to the sender gracefully too.
         */
        local.sun_path[0] = '\0';
        const bool snprintf_failed = (len < 0);
        if (WARN_ON(snprintf_failed)) return -EINVAL;
        const size_t need_len = len;
        const bool truncated = (need_len >= max_len);
        if (WARN_ON(truncated)) return -ENAMETOOLONG;

        const int bind_err = bind(sock->fd, addr, addr_len);
        const bool bind_succeeded = (bind_err == 0);
        if (bind_succeeded) break;

        counter++;
        tries_left--;
    } while (tries_left > 0);

    const bool retry_failed = (tries_left == 0);
    if (retry_failed) return -EBUSY;

    FREE(sock->cli_path);
    sock->cli_path = STRDUP(local.sun_path);

    return 0;
}

static int
hostap_sock_connect(struct hostap_sock *sock)
{
    struct sockaddr_un remote = {0};
    const struct sockaddr *addr = (struct sockaddr *)&remote;
    const size_t addr_len = sizeof(remote);
    remote.sun_family = AF_UNIX;
    STRSCPY_WARN(remote.sun_path, sock->ctrl_path);
    return connect(sock->fd, addr, addr_len);
}

/* public */
int
hostap_sock_close(struct hostap_sock *sock)
{
    if (sock->fd == HOSTAP_SOCK_FD_INVALID) return -EALREADY;
    if (sock->cli_path == NULL) return -EINVAL;

    close(sock->fd);
    sock->fd = HOSTAP_SOCK_FD_INVALID;

    unlink(sock->cli_path);
    FREE(sock->cli_path);
    sock->cli_path = NULL;

    return 0;
}

int
hostap_sock_open(struct hostap_sock *sock)
{
    if (sock->fd != HOSTAP_SOCK_FD_INVALID) return -EALREADY;
    if (sock->ctrl_path == NULL) return -EINVAL;

    const int socket_err = socket(PF_UNIX, SOCK_DGRAM, 0);
    const bool socket_failed = (socket_err < 0);
    if (socket_failed) {
        return socket_err;
    }
    sock->fd = socket_err;

    const int bind_err = hostap_sock_bind(sock);
    const bool bind_failed = (bind_err != 0);
    if (WARN_ON(bind_failed)) {
        hostap_sock_close(sock);
        return bind_err;
    }

    const int connect_err = hostap_sock_connect(sock);
    const bool connect_failed = (connect_err != 0);
    if (connect_failed) {
        hostap_sock_close(sock);
        return connect_err;
    }

    const int nonblock_err = hostap_sock_set_nonblock(sock->fd);
    const bool nonblock_failed = (nonblock_err != 0);
    WARN_ON(nonblock_failed);

    const int bufsize_err = hostap_sock_set_bufsize(sock->fd);
    const bool bufsize_failed = (bufsize_err != 0);
    WARN_ON(bufsize_failed);

    return 0;
}

struct hostap_sock *
hostap_sock_alloc(const char *ctrl_path)
{
    struct hostap_sock *sock = CALLOC(1, sizeof(*sock));
    sock->ctrl_path = STRDUP(ctrl_path);
    sock->fd = HOSTAP_SOCK_FD_INVALID;
    return sock;
}

void
hostap_sock_free(struct hostap_sock *sock)
{
    if (sock == NULL) return;
    hostap_sock_close(sock);
    FREE(sock->ctrl_path);
    FREE(sock);
}

bool
hostap_sock_get_msg(struct hostap_sock *sock,
                    char **msg,
                    size_t *msg_len,
                    bool *is_event)
{
    assert(msg != NULL);
    /* FIXME: MSG_PEEK isn't suitable for high-throughput
     * messaging. This should be fine for a control
     * interface though.
     */
    const ssize_t buf_len = recv(sock->fd, NULL, 0, MSG_PEEK | MSG_TRUNC);
    const bool peek_failed = (buf_len < 0);
    if (peek_failed) return false;

    char *buf = MALLOC(buf_len + 1);
    buf[buf_len] = 0;
    const ssize_t recv_err = recv(sock->fd, buf, buf_len, 0);
    LOGT("hostap: sock: %s: recv: '%.*s' (%zu): %zd",
         sock->ctrl_path,
         (int)buf_len,
         buf,
         buf_len,
         recv_err);
    const bool recv_failed = (recv_err != buf_len);
    if (recv_failed) {
        FREE(buf);
        return false;
    }

    assert(recv_err == buf_len);

    ssize_t last = buf_len - 1;
    while (last >= 0) {
        if (buf[last] != '\n') break;
        buf[last] = 0;
        last--;
    }

    const ssize_t len = last + 1;

    if (msg_len != NULL) *msg_len = len;
    if (is_event != NULL) *is_event = hostap_sock_msg_is_event(buf);
    *msg = buf;
    return true;
}

bool
hostap_sock_wait_ready(struct hostap_sock *sock,
                       struct timeval *tv)
{
    const int fd = sock->fd;
    const int max_fd = fd + 1;
    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        const int select_err = select(max_fd, &rfds, NULL, NULL, tv);
        const bool again = (select_err < 0 && errno == EINTR)
                        || (select_err < 0 && errno == EAGAIN);
        const bool timed_out = (select_err == 0)
                            && (tv->tv_sec == 0)
                            && (tv->tv_usec == 0);
        const bool fatal = select_err < 0 && !again;
        const bool ready = !fatal && !again && !timed_out;
        if (fatal) break;
        if (timed_out) break;
        if (ready) return true;
        /* otherwise retry */
    }
    return false;
}

bool
hostap_sock_is_opened(const struct hostap_sock *sock)
{
    if (sock == NULL) return false;
    return sock->fd != HOSTAP_SOCK_FD_INVALID;
}

bool
hostap_sock_is_closed(const struct hostap_sock *sock)
{
    if (sock == NULL) return true;
    return sock->fd == HOSTAP_SOCK_FD_INVALID;
}

int
hostap_sock_get_fd(const struct hostap_sock *sock)
{
    if (sock == NULL) return HOSTAP_SOCK_FD_INVALID;
    return sock->fd;
}

const char *
hostap_sock_get_path(const struct hostap_sock *sock)
{
    if (sock == NULL) return NULL;
    return sock->ctrl_path;
}
