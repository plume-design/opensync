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

#include <log.h>
#include <osw_diag.h>
#include <osw_etc.h>
#include <memutil.h>

#define LOG_PREFIX(fmt, ...) "osw: diag: " fmt, ## __VA_ARGS__
#define LOG_PREFIX_PIPE(pipe, fmt, ...) LOG_PREFIX("pipe=%p: " fmt, pipe, ## __VA_ARGS__)

#define OSW_DIAG_DBG_FILE_DEFAULT "/tmp/owm_dbg_logs_default"

struct osw_diag_pipe {
    FILE *dbg_file;
};

static const char *osw_diag_get_dbg_file_path(void)
{
    return osw_etc_get("OSW_DIAG_DBG_FILE") ?: OSW_DIAG_DBG_FILE_DEFAULT;
}

osw_diag_pipe_t *osw_diag_pipe_open(void)
{
    const char *dbg_file_path;
    osw_diag_pipe_t *pipe;

    dbg_file_path = osw_diag_get_dbg_file_path();
    pipe = MALLOC(sizeof(osw_diag_pipe_t));

    pipe->dbg_file = fopen(dbg_file_path, "a");
    if (pipe->dbg_file == NULL) {
        LOGE(LOG_PREFIX_PIPE(pipe, "Error opening debug file at path: %s"), dbg_file_path);
        FREE(pipe);
        return NULL;
    }

    return pipe;
}

osw_diag_pipe_t *osw_diag_pipe_prefix(osw_diag_pipe_t *pipe)
{
    ASSERT(false, "osw: diag: TODO %s", __FUNCTION__);
    return pipe;
}

void osw_diag_pipe_writef(osw_diag_pipe_t *pipe, const char *format, ...)
{
    if (pipe == NULL) {
        LOGE(LOG_PREFIX_PIPE(pipe, "pipe is NULL, ignoring write"));
        return;
    }

    if (pipe->dbg_file == NULL) {
        ASSERT(false, "osw: diag: unreachable code");
        return;
    }

    va_list args;
    va_start(args, format);
    vfprintf(pipe->dbg_file, format, args);
    va_end(args);

    fprintf(pipe->dbg_file, "\n");
}

void osw_diag_pipe_close(osw_diag_pipe_t *pipe)
{
    if (pipe == NULL) {
        LOGE(LOG_PREFIX_PIPE(pipe, "pipe is NULL, ignoring close"));
        return;
    }

    if (pipe->dbg_file == NULL) {
        ASSERT(false, "osw: diag: unreachable code");
        FREE(pipe);
        return;
    }

    fclose(pipe->dbg_file);
    FREE(pipe);
}
