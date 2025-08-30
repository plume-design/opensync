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

#include "log.h"
#include "const.h"
#include "read_until.h"
#include "execsh.h"
#include "memutil.h"

#define EXECSH_WAITPID_POLL     0.05    /* waitpid() poll interval in seconds */
#define EXECSH_WAITPID_MAX      10      /* Maximum retries when waiting for a process */

/* pipe() indexes */
#define P_RD    0       /* Read end */
#define P_WR    1       /* Write end */

/* Context structure used by execsh_fn_a() */
struct execsh_fn
{
    execsh_async_t  esf_esa;
    int             esf_exit_status;
    execsh_fn_t    *esf_fn;
    void           *esf_data;
};

static execsh_async_io_fn_t execsh_async_io_fn;
static void execsh_async_cleanup(execsh_async_t *esa);
static void execsh_async_io_check(execsh_async_t *esa);
static void execsh_async_std_read(struct ev_loop *loop, ev_io *w, int revent);
static void execsh_async_std_write(struct ev_loop *loop, ev_io *w, int revent);
static bool execsh_async_poll(execsh_async_t *esa);
static void execsh_async_set_wstatus(execsh_async_t *esa, int wstat, bool error);
static void execsh_async_child_timer(struct ev_loop *loop, ev_timer *w, int revent);
static void execsh_async_child_ev(struct ev_loop *loop, ev_child *w, int revent);
static execsh_async_fn_t execsh_fn_exit_fn;
static execsh_async_io_fn_t execsh_fn_io_fn;
static void execsh_closefrom(int fd);
static bool execsh_set_nonblock(int fd, bool enable);
static pid_t execsh_pspawn(const char *path, const char *argv[], int fdin, int fdout, int fderr);
static bool execsh_log_fn(void *ctx, enum execsh_io type, const char *msg);

const char *execsh_default_shell[] =
{
    EXECSH_SHELL_PATH,
    "-e",   /* Abort on error */
    "-x",   /* Verbose */
    "-s",   /* Read script from ... */
    "-",    /* ... STDIN */
    NULL
};

/*
 * ===========================================================================
 *  execsh async interface -- all other interfaces are based upon this one
 * ===========================================================================
 */

/*
 * Initialize an execsh_async_t object. `fn` is required and specified the
 * function which will be called when the process terminates.
 */
void execsh_async_init(execsh_async_t *esa, execsh_async_fn_t *fn)
{
    memset(esa, 0, sizeof(*esa));
    esa->esa_exit_fn = fn;
    ev_timer_init(
            &esa->esa_child_timer,
            execsh_async_child_timer,
            EXECSH_WAITPID_POLL,
            EXECSH_WAITPID_POLL);
    memset(&esa->esa_child_ev, 0, sizeof(esa->esa_child_ev));

    esa->esa_shell = execsh_default_shell;
    execsh_async_set(esa, NULL, NULL);
}

/*
 * Set execsh_async_t object loop or I/O handling function
 *
 * loop - The loop to use or NULL for default
 * io_fn - The I/O callback function to use or NULL for the default.
 */
void execsh_async_set(
        execsh_async_t *esa,
        struct ev_loop *loop,
        execsh_async_io_fn_t *io_fn)
{
    esa->esa_loop = loop == NULL ? EV_DEFAULT : loop;
    esa->esa_io_fn = io_fn == NULL ? execsh_async_io_fn : io_fn;
}

/*
 * Set alternative shell
 *
 * shell - An argv-style NULL-terminated array of pointers to strings
 */
void execsh_async_shell_set(execsh_async_t *esa, const char *shell[])
{
    esa->esa_shell = shell;
}

/*
 * Start the execsh script asynchronously. Upon process termination, the exit
 * callback will be called.
 *
 * Note: It is safe to reuse the execsh_async_t object from inside the exit
 * handler.
 *
 * This function returns PID of a child process if it was started or was
 * already running, or -1 if the process failed to start.
 */
pid_t execsh_async_start_a(
        execsh_async_t *esa,
        const char *script,
        const char *argv[])
{
    const char **pargs;
    const char **pargv;

    const char **args = NULL;
    void *args_e = NULL;
    int pin[2] = { -1 };
    int pout[2] = { -1 };
    int perr[2] = { -1 };
    bool retval = false;

    if (esa->esa_running)
    {
        LOG(WARN, "execsh_async: Attempting to start an already started process.");
        return esa->esa_child_pid;
    }

    esa->esa_exit_code = -1;

    read_until_init(
            &esa->esa_stdout_ru,
            esa->esa_stdout_buf,
            sizeof(esa->esa_stdout_buf));

    read_until_init(
            &esa->esa_stderr_ru,
            esa->esa_stderr_buf,
            sizeof(esa->esa_stderr_buf));

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

    esa->esa_stdin_pos = 0;
    esa->esa_stdin_buf = STRDUP(script);

    for (pargv = esa->esa_shell; *pargv != NULL; pargv++)
    {
        pargs = MEM_APPEND(&args, &args_e, sizeof(const char *));
        *pargs = *pargv;
    }

    /* Add variable arguments */
    for (pargv = argv; *pargv != NULL; pargv++)
    {
        pargs = MEM_APPEND(&args, &args_e, sizeof(const char *));
        *pargs = *pargv;
    }

    /* Terminate the list with NULL */
    pargs = MEM_APPEND(&args, &args_e, sizeof(const char *));
    *pargs = NULL;

    /* Run the child */
    esa->esa_child_pid = execsh_pspawn(args[0], args, pin[P_RD],  pout[P_WR], perr[P_WR]);
    if (esa->esa_child_pid < 0)
    {
        LOG(ERR, "execsh: Error executing: %s", script);
        goto exit;
    }

    /*
     * When using the default loop, libev may reap the child under our nose since
     * it install its own handles for SIGCHLD. To prevent this scenario from
     * happening, in addition to a timer, install a child handler
     */
    if (esa->esa_loop == EV_DEFAULT)
    {
        ev_child_init(&esa->esa_child_ev, execsh_async_child_ev, esa->esa_child_pid, 0);
        ev_child_start(esa->esa_loop, &esa->esa_child_ev);
    }

    /* Close child ends of the pipe */
    close(pin[P_RD]); pin[P_RD] = -1;
    close(pout[P_WR]); pout[P_WR] = -1;
    close(perr[P_WR]); perr[P_WR] = -1;

    esa->esa_stdin_fd = pin[P_WR];
    esa->esa_stdout_fd = pout[P_RD];
    esa->esa_stderr_fd = perr[P_RD];

    execsh_set_nonblock(esa->esa_stdout_fd, true);
    execsh_set_nonblock(esa->esa_stderr_fd, true);
    execsh_set_nonblock(esa->esa_stdin_fd, true);

    /*
     * Initialize and start watchers
     */
    ev_io_init(
            &esa->esa_stdin_ev,
            execsh_async_std_write,
            esa->esa_stdin_fd,
            EV_WRITE);

    ev_io_init(
            &esa->esa_stdout_ev,
            execsh_async_std_read,
            esa->esa_stdout_fd,
            EV_READ);

    ev_io_init(
            &esa->esa_stderr_ev,
            execsh_async_std_read,
            esa->esa_stderr_fd,
            EV_READ);

    esa->esa_stdin_ev.data = esa;
    esa->esa_stdout_ev.data = esa;
    esa->esa_stderr_ev.data = esa;

    ev_io_start(esa->esa_loop, &esa->esa_stdin_ev);
    ev_io_start(esa->esa_loop, &esa->esa_stdout_ev);
    ev_io_start(esa->esa_loop, &esa->esa_stderr_ev);

    esa->esa_running = true;
    retval = true;

exit:
    if (!retval)
    {
        esa->esa_exit_fn(esa, -1);
    }

    FREE(args);

    return esa->esa_running ? esa->esa_child_pid : -1;
}

/*
 * Poll the status of the execsh subprocess. Return true if the process
 * has exited, false if it is still running.
 */
bool execsh_async_poll(execsh_async_t *esa)
{
    int wstat;
    pid_t rc;

    bool error = false;

    if (!esa->esa_running) return true;

    rc = waitpid(esa->esa_child_pid, &wstat, WNOHANG);
    if (rc == 0)
    {
        return false;
    }
    else if (rc < 0)
    {
        LOG(ERR, "execsh: Unable to retrieve process status (pid %jd): %s",
                (intmax_t)esa->esa_child_pid,
                strerror(errno));
        error = true;
    }

    execsh_async_set_wstatus(esa, wstat, error);
    return true;
}

/*
 * Set the execsh process status
 */
void execsh_async_set_wstatus(execsh_async_t *esa, int wstat, bool error)
{
    esa->esa_exit_code = -1;

    if (!error)
    {
        if (WIFSIGNALED(wstat))
        {
            log_severity_t severity = esa->esa_stop_requested ? LOG_SEVERITY_INFO : LOG_SEVERITY_ERR;

            LOG_SEVERITY(severity, "execsh: Process terminated by signal %d.", WTERMSIG(wstat));
        }
        else if (WIFEXITED(wstat))
        {
            esa->esa_exit_code = WEXITSTATUS(wstat);
        }
    }

    ev_timer_stop(esa->esa_loop, &esa->esa_child_timer);
    ev_child_stop(esa->esa_loop, &esa->esa_child_ev);

    execsh_async_cleanup(esa);
    esa->esa_exit_fn(esa, esa->esa_exit_code);
}

/*
 * Stop the execsh script. This function may block if the script is still running
 * and it refuses to terminate gracefully.
 *
 * If the script is still running when this function is called, ask it nicely to
 * stop with SIGTERM. If the script refuses to stop, kill it with SIGKILL.
 */
void execsh_async_stop(execsh_async_t *esa)
{
    int retry;

    if (!esa->esa_running) return;

    for (retry = 0; retry < EXECSH_WAITPID_MAX; retry++)
    {
        if (execsh_async_poll(esa))
        {
            break;
        }

        if (retry == 0)
        {
            LOG(NOTICE, "execsh_async: Process %jd still alive. Killing with SIGTERM",
                    (intmax_t)esa->esa_child_pid);
            /* Kill with negative pid -PID: This way the termination signal is sent to every
             * process in the process group whose ID is -PID. */
            kill(-1 * esa->esa_child_pid, SIGTERM);
            esa->esa_stop_requested = true; // So that we later don't log the fatal signal with ERR, but with INFO
        }
        else if (retry == (EXECSH_WAITPID_MAX / 2))
        {
            LOG(WARN, "execsh_async: Process %jd still alive after SIGTERM. Killing with SIGKILL",
                    (intmax_t)esa->esa_child_pid);
            /* Kill with negative pid -PID: This way the termination signal is sent to every
             * process in the process group whose ID is -PID. */
            kill(-1 * esa->esa_child_pid, SIGKILL);
        }

        usleep(EXECSH_WAITPID_POLL*1000000);
    }

    if (!execsh_async_poll(esa))
    {
        LOG(ERR, "execsh_async: Unable to terminate process %jd.",
                (intmax_t)esa->esa_child_pid);
    }

    execsh_async_cleanup(esa);
}

/*
 * The default execsh I/O function. This function is used to log the script's
 * STDOUT and STDERR
 */
void execsh_async_io_fn(execsh_async_t *esa, enum execsh_io io_type, const char *msg)
{
    LOG(DEBUG, "execsh: %jd%s %s",
            (intmax_t)esa->esa_child_pid,
            io_type == EXECSH_IO_STDOUT ? ">" : "|",
            msg);
}

/*
 * Cleanup resources used by the execsh_async_t object, including watchers and
 * file descriptors.
 */
void execsh_async_cleanup(execsh_async_t *esa)
{
    if (!esa->esa_running) return;

    esa->esa_running = false;

    ev_timer_stop(esa->esa_loop, &esa->esa_child_timer);
    ev_child_stop(esa->esa_loop, &esa->esa_child_ev);

    ev_io_stop(esa->esa_loop, &esa->esa_stdin_ev);
    ev_io_stop(esa->esa_loop, &esa->esa_stdout_ev);
    ev_io_stop(esa->esa_loop, &esa->esa_stderr_ev);

    if (esa->esa_stdin_fd >= 0) close(esa->esa_stdin_fd);
    if (esa->esa_stdout_fd >= 0) close(esa->esa_stdout_fd);
    if (esa->esa_stderr_fd >= 0) close(esa->esa_stderr_fd);

    esa->esa_stdin_fd = -1;
    esa->esa_stdout_fd = -1;
    esa->esa_stderr_fd = -1;
    FREE(esa->esa_stdin_buf);
    esa->esa_stdin_buf = NULL;
}

/*
 * STDOUT/STDERR handler
 */
void execsh_async_std_read(struct ev_loop *loop, ev_io *w, int revent)
{
    enum execsh_io io_type;
    read_until_t *ru;
    ssize_t nrd;
    char *line;
    int *pfd;

    execsh_async_t *esa = w->data;

    if (!(revent & EV_READ))
    {
        return;
    }

    if (w->fd == esa->esa_stdout_fd)
    {
        io_type = EXECSH_IO_STDOUT;
        pfd = &esa->esa_stdout_fd;
        ru = &esa->esa_stdout_ru;
    }
    else if (w->fd == esa->esa_stderr_fd)
    {
        io_type = EXECSH_IO_STDERR;
        pfd = &esa->esa_stderr_fd;
        ru = &esa->esa_stderr_ru;
    }
    else
    {
        return;
    }

    while ((nrd = read_until(ru, &line, *pfd, "\r\n")) > 0)
    {
        esa->esa_io_fn(esa, io_type, line);
        if (!esa->esa_running) return;
    }

    if (nrd == -1 && errno == EAGAIN) return;

    ev_io_stop(loop, w);
    close(*pfd);
    *pfd = -1;

    execsh_async_io_check(esa);
}

/*
 * STDIN handler -- write the script to the process stdin
 */
void execsh_async_std_write(struct ev_loop *loop, ev_io *w, int revent)
{
    ssize_t nwr;
    const char *buf;

    execsh_async_t *esa = CONTAINER_OF(w, execsh_async_t, esa_stdin_ev);

    if (!(revent & EV_WRITE)) return;

    do
    {
        buf = esa->esa_stdin_buf + esa->esa_stdin_pos;
        nwr = write(
                esa->esa_stdin_fd,
                buf,
                strlen(buf));
        if (nwr <= 0) break;

        esa->esa_stdin_pos += nwr;
        if (nwr >= (ssize_t)strlen(buf)) break;
    }
    while (nwr >= 0);

    if (nwr == -1 && errno == EAGAIN) return;

    ev_io_stop(loop, w);
    close(esa->esa_stdin_fd);
    esa->esa_stdin_fd = -1;

    execsh_async_io_check(esa);
}

/*
 * Check if the process has completed all scheduled I/O operations
 */
void execsh_async_io_check(execsh_async_t *esa)
{
    if (esa->esa_stdin_fd >= 0) return;
    if (esa->esa_stderr_fd >= 0) return;
    if (esa->esa_stdout_fd >= 0) return;

    if (execsh_async_poll(esa)) return;

    /*
     * All the I/O with the child has been severed, but we could not yet reap
     * the process status. This is needed for non-default loop which do not
     * support ev_child.
     */
    ev_timer_start(esa->esa_loop, &esa->esa_child_timer);
}

/*
 * The SIGCHLD handler -- this works only on the default loop.
 */
void execsh_async_child_ev(struct ev_loop *loop, ev_child *w, int revent)
{
    (void)loop;
    (void)revent;

    execsh_async_t *esa = CONTAINER_OF(w, execsh_async_t, esa_child_ev);
    execsh_async_set_wstatus(esa, w->rstatus, false);
}

/*
 * Polling timer that is used as the alternative to the SIGCHLD above. Since
 * ev_child can be used only on the default loop, non-default loops need this
 * alternative to properly reap child processes.
 */
void execsh_async_child_timer(struct ev_loop *loop, ev_timer *w, int revent)
{
    (void)loop;
    (void)revent;

    execsh_async_t *esa = CONTAINER_OF(w, execsh_async_t, esa_child_timer);

    if (!execsh_async_poll(esa)) return;
}

/*
 * ===========================================================================
 *  execsh alternative APIs -- these use execsh_async_t as their core
 *  implementation
 * ===========================================================================
 */

/*
 * Main API for executing execsh scripts synchronously. The `fn` parameter
 * is used for I/O handling.
 *
 * This function returns the exit-code of the executed script.
 */
int execsh_fn_a(execsh_fn_t *fn, void *ctx, const char *script, char *argv[])
{
    struct execsh_fn esf;

    /*
     * Execute on its own loop -- do not use EV_DEFAULT as it may lead to all
     * sorts of re-entrnacy issues
     */
    struct ev_loop *loop = ev_loop_new(EVFLAG_AUTO);

    esf.esf_fn = fn;
    esf.esf_data = ctx;

    execsh_async_init(&esf.esf_esa, execsh_fn_exit_fn);
    execsh_async_set(&esf.esf_esa, loop, execsh_fn_io_fn);
    execsh_async_start_a(&esf.esf_esa, script, (const char **)argv);

    ev_run(loop, 0);

    execsh_async_stop(&esf.esf_esa);
    ev_loop_destroy(loop);

    return esf.esf_exit_status;
}

void execsh_fn_exit_fn(execsh_async_t *esa, int exit_status)
{
    struct execsh_fn *esf = CONTAINER_OF(esa, struct execsh_fn, esf_esa);
    esf->esf_exit_status = exit_status;
}

void execsh_fn_io_fn(execsh_async_t *esa, enum execsh_io io_type, const char *buf)
{
    struct execsh_fn *esf = CONTAINER_OF(esa, struct execsh_fn, esf_esa);
    esf->esf_fn(esf->esf_data, io_type, buf);
}

/*
 * Blocking API that takes a va_list as arguments -- this is just a wrapper for
 * execsh_fn_a()
 */
int execsh_fn_v(execsh_fn_t *fn, void *ctx, const char *script, va_list __argv)
{
    int retval;

    /* Build the argument list */
    char **argv = NULL;

    /*
     * Build the argument list
     */
    int argc = 0;
    int argm = 16;
    char *parg;

    argv = MALLOC(argm * sizeof(char *));

    /* Add variable arguments */
    while ((parg = va_arg(__argv, char *)) != NULL)
    {
        if (argc + 1 > argm)
        {
            argm <<= 1;
            argv = REALLOC(argv, argm * sizeof(char *));
        }

        argv[argc++] = parg;
    }

    /* Terminate it with NULL */
    argv[argc] = NULL;

    retval = execsh_fn_a(fn, ctx, script, argv);

    if (argv != NULL) FREE(argv);

    return retval;
}

bool execsh_log_fn(void *ctx, enum execsh_io io_type, const char *msg)
{
    int *severity = ctx;

    mlog(*severity, MODULE_ID,
            "%s %s",
            io_type == EXECSH_IO_STDOUT ? ">" : "|",
            msg);

    return true;
}

/*
 * Blocking API that executes the execsh scripts and logs STDOUT and STDERR
 * to the log
 */
int execsh_log_a(int severity, const char *script, char *argv[])
{
    return execsh_fn_a(execsh_log_fn, &severity, script, argv);
}

/*
 * va_list alternative to execsh_log_a()
 */
int execsh_log_v(int severity, const char *script, va_list va)
{
    return execsh_fn_v(execsh_log_fn, &severity, script, va);
}

/*
 * ===========================================================================
 *  Utility functions
 * ===========================================================================
 */

/*
 * Close all file descriptors above or equal to `fd`
 */
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
        const char *argv[],
        int fdin,
        int fdout,
        int fderr)
{
    char *envpath;
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

    /*
     * Set child process group ID to the PID of this child process.
     * All its child processes will inherit the PGID. Thus, later we would be able to kill
     * this process and all of its children by sendig a termination signal to the whole
     * process group by kill -<sigX> -PID.
     */
    setpgid(0, 0);

    /* Add the tools folder to the search path */
    envpath = getenv("PATH");
    char newpath[strlen(envpath) + strlen(":") + strlen(CONFIG_TARGET_PATH_TOOLS) + 1];
    snprintf(newpath, sizeof(newpath), "%s:%s", envpath, CONFIG_TARGET_PATH_TOOLS);
    setenv("PATH", newpath, 1);

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

    execvp(path, (char **)argv);

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

