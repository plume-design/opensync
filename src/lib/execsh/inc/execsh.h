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

#ifndef EXECSH_H_INCLUDED
#define EXECSH_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#include <ev.h>

#include "const.h"
#include "read_until.h"

#define EXECSH_SHELL_PATH       "/bin/sh"

enum execsh_io
{
    EXECSH_IO_STDOUT,
    EXECSH_IO_STDERR
};

#define EXECSH_PIPE_BUF         256

/**
 * This macro works wonderfully for embedding shell code into C.
 * It also syntactically checks that quotes and braces are balanced.
 *
 * Most importantly, you don't have to escape every single double quote.
 */
#define SHELL(...)  #__VA_ARGS__
#define _S          SHELL

typedef bool execsh_fn_t(void *ctx, enum execsh_io io_type, const char *buf);
typedef struct execsh_async execsh_async_t;

/*
 * This is the exit handler of the execsh_async_t object. It is invoked when
 * the process terminates.
 *
 * The execsh_async_t object in the handler can be considered stopped (as if
 * execsh_async_stop() was called) and is therefore safe to reuse to execute
 * new processes.
 */
typedef void execsh_async_fn_t(execsh_async_t *esa, int exit_status);

/*
 * This is the I/O handler of the process. It is invoked each time a line is
 * read from the stdout or stderr of the underlying process.
 *
 * It is safe to call `execsh_async_stop()` from within the callback to terminate
 * the process early if so desired.
 */
typedef void execsh_async_io_fn_t(execsh_async_t *esa, enum execsh_io io_type, const char *buf);

struct execsh_async
{
    bool                    esa_running;
    struct ev_loop         *esa_loop;
    pid_t                   esa_child_pid;
    const char            **esa_shell;
    ev_timer                esa_child_timer;
    ev_child                esa_child_ev;
    const char             *esa_pscript;
    execsh_async_fn_t      *esa_exit_fn;
    execsh_async_io_fn_t   *esa_io_fn;
    const char             *esa_stdin_buf;
    size_t                  esa_stdin_pos;
    int                     esa_stdin_fd;
    int                     esa_stdout_fd;
    int                     esa_stderr_fd;
    read_until_t            esa_stdout_ru;
    read_until_t            esa_stderr_ru;
    char                    esa_stdout_buf[EXECSH_PIPE_BUF];
    char                    esa_stderr_buf[EXECSH_PIPE_BUF];
    ev_io                   esa_stdin_ev;
    ev_io                   esa_stdout_ev;
    ev_io                   esa_stderr_ev;
    int                     esa_exit_code;
    bool                    esa_stop_requested;
};

/*
 * Initialize a execsh async object.
 */
void execsh_async_init(execsh_async_t *esa, execsh_async_fn_t *);

/*
 * Set various execsh async object parameters:
 *
 * loop - a custom libev loop to use
 * io_fn - a custom I/O callback to use
 */
void execsh_async_set(
        execsh_async_t *esa,
        struct ev_loop *loop,
        execsh_async_io_fn_t *io_fn);

/*
 * Set alternative shell
 *
 * shell - argv-style NULL-terminated array of string pointers; this is the
 *         command line that will be used to spawn the shell. Variable
 *         arguments are added to this list.
 */
void execsh_async_shell_set(execsh_async_t *esa, const char *shell[]);

/*
 * Start the script specified in `script` with the variable arguments in `vargs`
 * as its parameters.
 *
 * This function returns PID of a child process if it was started or was already
 * running, or -1 if the process failed to start.
 */
pid_t execsh_async_start_a(
        execsh_async_t *esa,
        const char *script,
        const char *argv[]);

/*
 * Stop the execsh_async watcher and terminated the running script.
 *
 * Note: If the script is still running, this will trigger an execsh_async_fn_t
 * callback. This function may block.
 */
void execsh_async_stop(execsh_async_t *esa);

#define execsh_async_start(esa, script, ...) \
    execsh_async_start_a((esa), (script), C_CVPACK(__VA_ARGS__))

/*
 * Execute a shell script specified by @p script synchronously,
 * redirect outputs to the logger.
 *
 * The script specified in @p script is piped to a forked shell process
 * which is executed by passing the variable length arguments as its
 * parameters.
 */
int execsh_fn_v(execsh_fn_t *fn, void *ctx, const char *script, va_list va);
int execsh_fn_a(execsh_fn_t *fn, void *ctx, const char *script, char *argv[]);

#define execsh_fn(fn, ctx, script, ...) \
    execsh_fn_a((fn), (ctx), (script), C_VPACK(__VA_ARGS__))

/*
 * Same as execsh_fn(), but redirect stderr/stdout to the logger instead.
 *
 * Example:
 * execsh_log(LOG_SEVERITY_INFO, ...);
 *
 */
int execsh_log_v(int severity, const char *script, va_list va);
int execsh_log_a(int severity, const char *script, char *argv[]);

#define execsh_log(severity, script, ...) \
    execsh_log_a((severity), (script), C_VPACK(__VA_ARGS__))

#define EXECSH_LOG(severity, script, ...) \
    execsh_log_a(LOG_SEVERITY_ ## severity, (script), C_VPACK(__VA_ARGS__))

#endif /* EXECSH_H_INCLUDED */
