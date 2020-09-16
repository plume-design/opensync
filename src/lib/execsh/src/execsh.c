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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <ev.h>

#include "log.h"
#include "const.h"
#include "read_until.h"
#include "execsh.h"

static void execsh_closefrom(int fd);
static bool execsh_set_nonblock(int fd, bool enable);
static pid_t execsh_pspawn(const char *path, char *argv[], int fdin, int fdout, int fderr);
static bool __execsh_log(void *ctx, int type, const char *msg);

static void __execsh_fn_std_write(struct ev_loop *loop, ev_io *w, int revent);
static void __execsh_fn_std_read(struct ev_loop *loop, ev_io *w, int revent);

/* pipe() indexes */
#define P_RD    0       /* Read end */
#define P_WR    1       /* Write end */

/**
 * execsh_fn() this is the main execsh function -- all other functions are based on this one.
 */
struct __execsh
{
    const char      *pscript;
    execsh_fn_t     *fn;
    void            *ctx;
    int             stdin_fd;
    int             stdout_fd;
    read_until_t    stdout_ru;
    int             stderr_fd;
    read_until_t    stderr_ru;
};

int execsh_fn_v(execsh_fn_t *fn, void *ctx, const char *script, va_list __argv)
{
    int retval = -1;

    /* Build the argument list */
    char **argv = NULL;

    /*
     * Build the argument list
     */
    int argc = 0;
    int argm = 16;
    char *parg;

    argv = malloc(argm * sizeof(char *));
    if (argv == NULL)
    {
        LOG(ERR, "execsh_fn_v: Error allocating argv buffer");
        goto exit;
    }

    /* Add variable arguments */
    while ((parg = va_arg(__argv, char *)) != NULL)
    {
        if (argc + 1 > argm)
        {
            argm <<= 1;
            argv = realloc(argv, argm * sizeof(char *));
            if (argv == NULL)
            {
                LOG(ERR, "execsh_fn_v: Error re-allocating argv buffer");
                goto exit;
            }
        }

        argv[argc++] = parg;
    }

    /* Terminate it with NULL */
    argv[argc] = NULL;

    retval = execsh_fn_a(fn, ctx, script, argv);

exit:
    if (argv != NULL) free(argv);

    return retval;
}

int execsh_fn_a(execsh_fn_t *fn, void *ctx, const char *script, char *__argv[])
{
    struct __execsh es;
    int wstat;

    pid_t cpid = -1;
    int retval = -1;

    int pin[2] = { -1 };
    int pout[2] = { -1 };
    int perr[2] = { -1 };
    char **argv = NULL;
    struct ev_loop *loop = NULL;

    /**
     * Create STDIN/STDOUT/STDERR: common pipes
     */
    if (pipe(pin) != 0)
    {
        LOG(ERR, "execsh: Error creating STDIN pipes.");
        goto exit;
    }

    if (pipe(pout) != 0)
    {
        LOG(ERR, "execsh: Error creating STDOUT pipes.");
        goto exit;
    }

    if (pipe(perr) != 0)
    {
        LOG(ERR, "execsh: Error creating STDERR pipes.");
        goto exit;
    }

    /*
     * Build the argument list
     */
    int argc = 0;
    int argm = 16;
    char **parg;

    argv = malloc(argm * sizeof(char *));
    if (argv == NULL)
    {
        LOG(ERR, "execsh: Error allocating argv buffer");
        goto exit;
    }

    argv[argc++] = EXECSH_SHELL_PATH;
    argv[argc++] = "-e";                /* Abort on error */
    argv[argc++] = "-x";                /* Verbose */
    argv[argc++] = "-s";                /* Read script from ... */
    argv[argc++] = "-";                 /* ... STDIN */

    /* Add variable arguments */
    for (parg = __argv; *parg != NULL; parg++)
    {
        if (argc + 1 > argm)
        {
            argm <<= 1;
            argv = realloc(argv, argm * sizeof(char *));
            if (argv == NULL)
            {
                LOG(ERR, "execsh: Error re-allocating argv buffer");
                goto exit;
            }
        }

        argv[argc++] = *parg;
    }

    /* Terminate it with NULL */
    argv[argc] = NULL;

    /* Run the child */
    cpid = execsh_pspawn(EXECSH_SHELL_PATH, argv, pin[P_RD],  pout[P_WR], perr[P_WR]);
    if (cpid < 0)
    {
        LOG(ERR, "execsh: Error executing: %s", script);
        goto exit;
    }

    /* Close child ends of the pipe */
    close(pin[P_RD]); pin[P_RD] = -1;
    close(pout[P_WR]); pout[P_WR] = -1;
    close(perr[P_WR]); perr[P_WR] = -1;

    /* Callback context */
    char outbuf[EXECSH_PIPE_BUF];
    char errbuf[EXECSH_PIPE_BUF];

    es.fn = fn;
    es.ctx = ctx;
    es.pscript = script;
    es.stdin_fd = pin[P_WR];
    es.stdout_fd = pout[P_RD];
    es.stderr_fd = perr[P_RD];

    read_until_init(&es.stdout_ru, outbuf, sizeof(outbuf));
    read_until_init(&es.stderr_ru, errbuf, sizeof(errbuf));

    /*
     * Initialize and start watchers
     */
    ev_io win;
    ev_io wout;
    ev_io werr;

    loop = ev_loop_new(EVFLAG_AUTO);
    if (loop == NULL)
    {
        LOG(ERR, "execsh: Error creating loop, execsh() failed.");
        goto exit;
    }

    ev_io_init(&win, __execsh_fn_std_write, pin[P_WR], EV_WRITE);
    win.data = &es;
    execsh_set_nonblock(pin[P_WR], true);
    ev_io_start(loop, &win);

    ev_io_init(&wout, __execsh_fn_std_read, pout[P_RD], EV_READ);
    wout.data = &es;
    execsh_set_nonblock(pout[P_RD], true);
    ev_io_start(loop, &wout);

    ev_io_init(&werr, __execsh_fn_std_read, perr[P_RD], EV_READ);
    werr.data = &es;
    execsh_set_nonblock(perr[P_RD], true);
    ev_io_start(loop, &werr);

    /* Loop until all watchers are active */
    while (ev_run(loop, EVRUN_ONCE))
    {
        bool do_loop = false;

        do_loop |= ev_is_active(&win);
        do_loop |= ev_is_active(&wout);
        do_loop |= ev_is_active(&werr);

        if (!do_loop) break;
    }

    /* Wait for the process to terminate */
    if (waitpid(cpid, &wstat, 0) <= 0)
    {
        LOG(ERR, "execsh: Error waiting on child.");
        goto exit;
    }

    if (WIFSIGNALED(wstat))
    {
        LOG(ERR, "execsh: Process terminated by signal %d.", WTERMSIG(wstat));
        goto exit;
    }

    if (!WIFEXITED(wstat))
    {
        /*
         * Process was not terminated by signal and did not exit
         */
        LOG(ERR, "execsh: Unable to retrieve process status.");
        goto exit;
    }

    retval = WEXITSTATUS(wstat);

exit:
    if (loop != NULL) ev_loop_destroy(loop);

    if (argv != NULL) free(argv);

    int ii;
    for (ii = 0; ii < 2; ii++)
    {
        if (pin[ii] >= 0) close(pin[ii]);
        if (pout[ii] >= 0) close(pout[ii]);
        if (perr[ii] >= 0) close(perr[ii]);
    }

    return retval;
}

void __execsh_fn_std_write(struct ev_loop *loop, ev_io *w, int revent)
{
    struct __execsh *pes = w->data;

    if (!(revent & EV_WRITE)) return;

    ssize_t nwr;

    while ((nwr = write(w->fd, pes->pscript, strlen(pes->pscript))) > 0)
    {
        pes->pscript += nwr;

        /* Did we reach the end of the string? */
        if (pes->pscript[0] == '\0')
        {
            break;
        }
    }

    if (nwr == -1 && errno == EAGAIN) return;

    ev_io_stop(loop, w);

    close(w->fd);
}

void __execsh_fn_std_read(struct ev_loop *loop, ev_io *w, int revent)
{
    struct __execsh *pes = w->data;

    int type = 0;
    read_until_t *ru = NULL;

    if (!(revent & EV_READ)) return;

    if (w->fd == pes->stdout_fd)
    {
        type = EXECSH_PIPE_STDOUT;
        ru = &pes->stdout_ru;
    }
    else if (w->fd == pes->stderr_fd)
    {
        type = EXECSH_PIPE_STDERR;
        ru = &pes->stderr_ru;
    }

    if (type == 0 || ru == NULL) return;

    char *line;
    ssize_t nrd;

    while ((nrd = read_until(ru, &line, w->fd, "\n")) > 0)
    {
        /* Close the pipe if the callback returns false */
        if (!pes->fn(pes->ctx, type, line)) break;
    }

    if (nrd == -1 && errno == EAGAIN) return;

    ev_io_stop(loop, w);
    close(w->fd);
}

void execsh_closefrom(int fd)
{
    int ii;

    int maxfd = sysconf(_SC_OPEN_MAX);

    for (ii = fd; ii < maxfd; ii++) close(ii);
}

/**
 * Spawn a process:
 *
 *  - path: Full path to executable
 *  - fstdin, fstdout, fstderr: file descriptors that will be used for I/O
 *    redirection or -1 if /dev/null shall be used instead
 *  - argv: null-terminated variable list of char *
 */
pid_t execsh_pspawn(
        const char *path,
        char *argv[],
        int fdin,
        int fdout,
        int fderr)
{
    int fdevnull;
    pid_t child;
    int flog;
    int fdi;
    int rc;

    /* Remap fdin, fdout and fderr arguments to an array for convenience */
    int fdio[3] = { fdin, fdout, fderr };

    child = fork();
    if (child > 0)
    {
        return child;
    }
    else if (child < 0)
    {
        LOG(DEBUG, "execsh: Fork failed.");
        return -1;
    }

    // Point of no return -- below this point print messages to stderr
    flog = (fderr >= 0) ? fderr : 2;

    /*
     * In case there's a gap between file descriptors 0..2, fill it with
     * references to /dev/null
     */
    while ((fdevnull = open("/dev/null", O_RDWR)) < 3)
    {
        if (fdevnull < 0)
        {
            dprintf(flog, "execsh (post-fork): Error opening /dev/null: %s",
                          strerror(errno));
            fdevnull = -1;
            break;
        }
    }

    /*
     * Relocate file descriptors -- now that we're sure there's no holes between
     * 0..2, we can just dup() the file descriptors to acquire a descriptor >2
     *
     * This step is necessary to ensure that the descriptor assignment phase
     * (below) doesn't accidentally overwrite a file descriptor using dup2().
     */
    for (fdi = 0; fdi < ARRAY_LEN(fdio); fdi++)
    {
        if (fdio[fdi] < 0 || fdio[fdi] > 2) continue;

        fdio[fdi] = dup(fdio[fdi]);
        if (fdio[fdi] < 0)
        {
            dprintf(flog, "execsh (post-fork): Error cloning fd %d.\n", fdi);
        }
    }

    /*
     * Assign file descriptors; if a file descriptor is invalid <0, replace it
     * with a reference to /dev/null
     */
    for (fdi = 0; fdi < ARRAY_LEN(fdio); fdi++)
    {
        if (fdio[fdi] < 0)
        {
            rc = dup2(fdevnull, fdi);
        }
        else
        {
            rc = dup2(fdio[fdi], fdi);
        }

        if (rc < 0)
        {
            dprintf(flog, "execsh (post-fork): Error assigning file descriptor to %d: %s\n",
                         fdi, strerror(errno));
        }
    }

    // Close all other file descriptors
    execsh_closefrom(3);

    execv(path, argv);

    dprintf(flog, "execsh (post-fork): execv(\"%s\", ...) failed. Error: %s\n",
                  path, strerror(errno));

    _exit(255);
}

/**
 * Set the non-blocking flag
 */
static bool execsh_set_nonblock(int fd, bool enable)
{
    int opt = enable ? 1 : 0;

    if (ioctl(fd, FIONBIO, &opt) != 0) return false;

    return true;
}

bool __execsh_log(void *ctx, int type, const char *msg)
{
    int *severity = ctx;

    mlog(*severity, MODULE_ID,
            "%s %s",
            type == EXECSH_PIPE_STDOUT ? ">" : "|",
            msg);

    return true;
}

int execsh_log_v(int severity, const char *script, va_list va)
{
    return execsh_fn_v(__execsh_log, &severity, script, va);
}

int execsh_log_a(int severity, const char *script, char *argv[])
{
    return execsh_fn_a(__execsh_log, &severity, script, argv);
}

