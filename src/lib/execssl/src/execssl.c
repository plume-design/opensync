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

#include <string.h>

#include "ds.h"
#include "execsh.h"
#include "execssl.h"
#include "log.h"
#include "memutil.h"
#include "arena.h"

/* OpenSSL execution context */
struct execssl_ctx
{
    execsh_async_t os_esa;
    char *os_stdout;
    char *os_stdout_e;
    int os_retval;
};

/*
 * Prevent the compiler from optimizing out the call to memset()/bzero()
 */
static volatile void *(*__memset_volatile)(void *, int c, size_t n) = (void *)memset;
#define memset_secure(buf, len) __memset_volatile((buf), 0, (len))

/*
 * ======================================================================
 * OpenSSL wrapper functions
 * ======================================================================
 */
static execsh_async_fn_t execssl_fn;
static execsh_async_io_fn_t execssl_io;

void execssl_fn(execsh_async_t *esa, int exit_status)
{
    struct execssl_ctx *self = CONTAINER_OF(esa, struct execssl_ctx, os_esa);
    self->os_retval = exit_status;
}

/*
 * Capture stdout from openssl and append to a buffer. stderr is redirected to
 * the logger
 */
void execssl_io(execsh_async_t *esa, enum execsh_io io_type, const char *buf)
{
    struct execssl_ctx *self = CONTAINER_OF(esa, struct execssl_ctx, os_esa);
    char *cbuf;
    size_t cbuf_len;

    switch (io_type)
    {
        case EXECSH_IO_STDOUT:
            cbuf_len = strlen(buf);
            cbuf = MEM_APPEND(&self->os_stdout, &self->os_stdout_e, cbuf_len + sizeof(char));
            memcpy(cbuf, buf, strlen(buf));
            cbuf[cbuf_len] = '\n';
            break;

        case EXECSH_IO_STDERR:
            LOG(INFO, "openssl shell: %s\n", buf);
            break;
    }
}

/*
 * Execute the openssl command with `argv` arguments. If `cstdin` is not NULL,
 * it is fed into the command's stdin. The stdout of the process is returned
 * as an allocated string buffer. NULL is returned in case the return code != 0
 *
 * The code is mostly based around libexecsh, except the default shell is
 * replaced by the openssl command. In this case this works well since execsh
 * is made to work around piping stuff to and back to a shell process. If we
 * replace the shell with openssl, it is just as useful passing data to openssl.
 *
 */
char *execssl_a(const char *cstdin, const char *argv[])
{
    struct execssl_ctx os = {0};
    char *cbuf;

    struct ev_loop *loop = ev_loop_new(EVFLAG_AUTO);

    if (cstdin == NULL) cstdin = "";

    execsh_async_init(&os.os_esa, execssl_fn);
    execsh_async_set(&os.os_esa, loop, execssl_io);
    execsh_async_shell_set(&os.os_esa, C_CVPACK("openssl"));
    execsh_async_start_a(&os.os_esa, cstdin, argv);

    ev_run(loop, 0);

    execsh_async_stop(&os.os_esa);
    ev_loop_destroy(loop);

    if (os.os_retval != 0)
    {
        LOG(ERR, "openssl returned error code: %d", os.os_retval);
        if (os.os_stdout != NULL)
        {
            memset_secure(os.os_stdout, strlen(os.os_stdout));
            FREE(os.os_stdout);
        }

        return NULL;
    }

    /* Pad buffer with an ending \0 */
    cbuf = MEM_APPEND(&os.os_stdout, &os.os_stdout_e, sizeof(char));
    *cbuf = '\0';

    return os.os_stdout;
}

/*
 * Arena allocator wrapper around execssl_a()
 */
char *execssl_arena_a(arena_t *arena, const char *cstdin, const char *argv[])
{
    char *out = execssl_a(cstdin, argv);
    if (out == NULL) return NULL;

    char *retval = arena_strdup(arena, out);
    FREE(out);

    return retval;
}
