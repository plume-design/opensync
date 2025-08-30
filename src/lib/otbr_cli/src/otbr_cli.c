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

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>

#include "log.h"
#include "memutil.h"
#include "otbr_agent.h"
#include "otbr_cli.h"
#include "otbr_socket.h"
#include "otbr_timeout.h"
#include "util.h"

/** Default timeout (seconds) waiting for a command finish */
#define DEFAULT_COMMAND_TIMEOUT 1.0f

/** Response text indicating that the command was successfully executed */
#define CLI_RESPONSE_DONE "Done"
/** Error prefix used in the response indicating an error - full format is `"Error %u: %s"` */
#define CLI_RESPONSE_ERROR_PREFIX "Error "

/** OTBR agent daemon process context structure */
static daemon_t g_agent_daemon;
/** Callback function invoked in case of unrecoverable error */
static otbr_cli_on_failure_fn_t *g_on_failure_cb;
/** Socket used for communication with the OTBR agent */
static otbr_socket_t g_agent_socket;

/** Callback function invoked when the daemon exits unexpectedly (crashes) */
static bool on_agent_exit_cb(daemon_t *const agent)
{
    LOGE("%s exited unexpectedly", agent->dn_exec);

    if (g_on_failure_cb != NULL)
    {
        g_on_failure_cb(OTBR_CLI_FAILURE_REASON_DAEMON_CRASH);
    }

    return true;
}

bool otbr_cli_init(otbr_cli_on_failure_fn_t *const on_failure_cb)
{
    g_on_failure_cb = on_failure_cb;

    if (!otbr_agent_init(&g_agent_daemon, on_agent_exit_cb))
    {
        return false;
    }

    LOGI("OTBR CLI initialized");
    return true;
}

bool otbr_cli_start(struct ev_loop *loop, const char *const thread_if_name, const char *const network_if_name)
{
    if (otbr_cli_is_running())
    {
        LOGW("OTBR CLI already running, stopping first");
        otbr_cli_stop();
    }

    if (!(
                /* OTBR agent manages Thread interface */
                otbr_agent_start(&g_agent_daemon, thread_if_name, network_if_name) &&
                /* Socket is used for communication with the OTBR agent */
                otbr_socket_open(&g_agent_socket, loop, thread_if_name, 3, 0.1f)))
    {
        otbr_cli_stop();
        return false;
    }

    LOGI("OTBR CLI started");
    return true;
}

bool otbr_cli_is_running(void)
{
    return otbr_agent_is_running(&g_agent_daemon) && otbr_socket_is_open(&g_agent_socket);
}

const char *otbr_cli_cmd(const char *const fmt, ...)
{
    /* Take into account the leading and trailing '\n' characters, which ensure
     * that the complete command is parsed by the CLI and executed immediately */
    const int max_cmd_len = sizeof(g_agent_socket.command.buffer) - 3;
    char *const buffer = g_agent_socket.command.buffer;
    va_list args;
    int len;

    if (g_agent_socket.command.len != 0)
    {
        LOGW("Previous %d B command not sent", g_agent_socket.command.len);
        g_agent_socket.command.len = 0;
    }

    va_start(args, fmt);
    len = vsnprintf(&buffer[1], max_cmd_len, fmt, args);
    va_end(args);

    ASSERT(len >= 0, "Command '%s' malformed (%s)", fmt, strerror(errno));
    ASSERT(len < max_cmd_len, "Command '%s' too long (%d >= %d)", fmt, len, max_cmd_len);

    if ((len > 0) && (len < max_cmd_len))
    {
        LOGT("Command: '%.*s'", len, &buffer[1]); /*< Do not print newlines in debug */
        buffer[0] = '\n';
        len++; /*< Take the offset for the first '\n' into account */
        buffer[len++] = '\n';
    }
    else
    {
        LOGT("Command: empty");
        len = 0;
    }
    buffer[len] = '\0';
    g_agent_socket.command.len = len;

    return buffer;
}

/**
 * Check if a CLI response is complete - if this is the last line of the response, representing the status result
 *
 * @param[in]  line   Response line to check.
 * @param[out] error  Where to write the beginning of the error message located in `line`.
 *                    Only written this line is an error/status line or if the error message is not empty.
 *
 * @return `OT_ERROR_INVALID` if this line is not a status line, any other value if
 *         this is a status line representing a final line of a complete response.
 */
static ot_error_t NONNULL(1, 2) parse_response_status_line(const char *line, const char **const error)
{
    unsigned long err;
    char *err_ptr;

    /* When comparing Done, also compare null character as this shall be the only text in this line */
    if (memcmp(line, CLI_RESPONSE_DONE, sizeof(CLI_RESPONSE_DONE)) == 0)
    {
        return OT_ERROR_NONE;
    }

    /* Not "Done", maybe "Error %u: %s"? */
    if (strncmp(line, CLI_RESPONSE_ERROR_PREFIX, STRLEN(CLI_RESPONSE_ERROR_PREFIX)) != 0)
    {
        return OT_ERROR_INVALID;
    }

    errno = 0;
    err = strtoul(line + STRLEN(CLI_RESPONSE_ERROR_PREFIX), &err_ptr, 10);
    if ((errno == ERANGE) || (strncmp(err_ptr, ": ", STRLEN(": ")) != 0))
    {
        /* In theory, some data line could start with the error prefix, so don't log error here */
        LOGD("Failed to parse error in response '%s'", line);
        return OT_ERROR_INVALID;
    }
    if ((err <= OT_ERROR_NONE) || (err >= OT_NUM_ERRORS))
    {
        LOGE("Invalid error number %lu in response '%s'", err, line);
        return OT_ERROR_INVALID;
    }

    *error = err_ptr + STRLEN(": ");

    LOGT("Parsed error %lu '%s' from '%s'", err, *error, line);
    return (ot_error_t)err;
}

ot_error_t otbr_cli_execute(const otbr_cli_cmd_t cmd, otbr_cli_response_t *const response, float timeout)
{
    ot_error_t status;
    ssize_t len;

    ASSERT((response == NULL) || ((response->lines == NULL) && (response->count == 0)),
           "Response not initialized or freed");

    if (cmd != g_agent_socket.command.buffer)
    {
        /* This command was not built using otbr_cli_cmd, ensure proper delimiters */
        if (g_agent_socket.command.len != 0)
        {
            LOGW("Overwriting previous %d B command", g_agent_socket.command.len);
        }
        otbr_cli_cmd("%s", cmd);
    }

    if (timeout < 0)
    {
        timeout = DEFAULT_COMMAND_TIMEOUT;
    }

    /* Per current design, there shall be no async data received on the socket, only command responses */
    if (!socket_discard(&g_agent_socket))
    {
        LOGE("Discard failed");
        return OT_ERROR_COMMAND_ERROR;
    }

    if (!otbr_socket_write_cmd(&g_agent_socket))
    {
        return OT_ERROR_COMMAND_ERROR;
    }

    otbr_timeout_start(&g_agent_socket.timer, timeout);
    otbr_socket_reader_activate(&g_agent_socket, true);
    do
    {
        char line[OTBR_CLI_LINE_BUFFER_SIZE];
        const char *error_msg = NULL;

        len = otbr_socket_readline(&g_agent_socket, line, sizeof(line));
        if (len < 0)
        {
            /* The line was not received */
            status = OT_ERROR_COMMAND_TIMEOUT;
            break;
        }
        if (len == 0)
        {
            /* Empty lines are ignored */
            status = OT_ERROR_INVALID;
            continue;
        }

        status = parse_response_status_line(line, &error_msg);
        if ((response != NULL) && ((status == OT_ERROR_INVALID) || (error_msg != NULL)))
        {
            /* Not a status line, or an error status line (error messages are still added to the response data).
             * Add it to the response data, which is allocated on demand. */
            ARRAY_APPEND(
                    response->lines,
                    response->count,
                    (char *)((status == OT_ERROR_INVALID) ? STRNDUP(line, len) : STRDUP(error_msg)));
        }
    } while (status == OT_ERROR_INVALID);

    otbr_socket_reader_activate(&g_agent_socket, false);
    otbr_timeout_stop(&g_agent_socket.timer);

    return status;
}

bool otbr_cli_is_busy(void)
{
    return otbr_socket_is_reader_active(&g_agent_socket);
}

void otbr_cli_response_free(otbr_cli_response_t *const response)
{
    if (response != NULL)
    {
        ARRAY_FREE_ITEMS(response->lines, response->count);
    }
}

void otbr_cli_stop(void)
{
    otbr_socket_close(&g_agent_socket);
    otbr_agent_stop(&g_agent_daemon);
    LOGI("OTBR CLI stopped");
}

void otbr_cli_close(void)
{
    otbr_agent_cleanup(&g_agent_daemon);
    g_on_failure_cb = NULL;
    LOGI("OTBR CLI closed");
}

/* *** Higher-level helper functions related to command execution *** */

bool otbr_cli_exec(otbr_cli_cmd_t cmd, const float timeout)
{
    ot_error_t status;

    status = otbr_cli_execute(cmd, NULL, timeout);
    if (status != OT_ERROR_NONE)
    {
        LOGE("Failed to execute '%s' (%d)", cmd, status);
        return false;
    }
    return true;
}

/**
 * Get a single-line value via CLI interface
 *
 * This function is a wrapper around @ref otbr_cli_execute_c command, with
 * addition of response check, value copy and freeing the response data.
 *
 * @param[in]     cmd              Command to send to get a single line value.
 * @param[in]     timeout          Response timeout in seconds, or negative value (-1) to use the default timeout.
 * @param[in]     value            Pointer on at least `*value_len` or `value_len_exact` bytes long buffer, to copy the
 *                                 response value to.
 * @param[in,out] value_len        If not `NULL`, it shall hold the buffer length on input (maximum allowed value
 *                                 length), and will hold the actual value length on return (even if `value_len_exact`
 *                                 is provided). If `NULL`, the `value_len_exact` shall be provided.
 * @param[in]     value_len_exact  If not 0, the exact value length is expected and the function will fail if the
 *                                 actual value length is different.
 *
 * @note No null-terminator is added to the `value` buffer.
 *
 * @return `OT_ERROR_NONE` if command is successful, `OT_ERROR_PARSE` if value length is incorrect (even if command
 *         execution was successful), or any other error code on failure. Failures are logged internally,
 *         except the final CLI command response (values greater than `OT_ERROR_NONE`).
 */
static ot_error_t NONNULL(1, 3) otbr_cli_get_value(
        otbr_cli_cmd_t cmd,
        const float timeout,
        char *const value,
        size_t *value_len,
        size_t value_len_exact)
{
    otbr_cli_response_t rsp = {0};
    ot_error_t status;

    if (value_len == NULL)
    {
        ASSERT(value_len_exact > 0, "Some form of value length is required");
        value_len = &value_len_exact;
    }

    status = otbr_cli_execute(cmd, &rsp, timeout);
    if (status == OT_ERROR_NONE)
    {
        if (rsp.count == 1)
        {
            const size_t rsp_len = strlen(rsp.lines[0]);

            if (rsp_len > *value_len)
            {
                LOGE("'%s' response '%s' is too long (%d > %d)", cmd, rsp.lines[0], rsp_len, *value_len);
                status = OT_ERROR_PARSE;
            }
            else if ((value_len_exact != 0) && (rsp_len != value_len_exact))
            {
                LOGE("'%s' response '%s' unexpected length (%d != %d)", cmd, rsp.lines[0], rsp_len, value_len_exact);
                status = OT_ERROR_PARSE;
            }
            else
            {
                memcpy(value, rsp.lines[0], rsp_len);
            }
            *value_len = rsp_len;
        }
        else
        {
            LOGE("Expected 1 instead of %d lines of response to '%s'", rsp.count, cmd);
            status = OT_ERROR_PARSE;
        }
    }
    otbr_cli_response_free(&rsp);

    return status;
}

bool otbr_cli_get(otbr_cli_cmd_t cmd, otbr_cli_response_t *const response, const size_t min_count, const float timeout)
{
    ot_error_t status;

    status = otbr_cli_execute(cmd, response, timeout);
    if (status != OT_ERROR_NONE)
    {
        LOGE("Failed to execute '%s' (%d)", cmd, status);
        return false;
    }

    if (response->count < min_count)
    {
        LOGE("Expected at least %d lines of response to '%s', got %d", min_count, cmd, response->count);
        return false;
    }

    return true;
}

bool otbr_cli_get_number(otbr_cli_cmd_t cmd, void *const value, const size_t sizeof_value, const int base)
{
    char buffer[sizeof("+18446744073709551615")]; /*< UINT64_MAX in decimal */
    size_t value_len = sizeof(buffer) - 1;        /*< Reserve space for null-terminator */
    size_t value_len_exact;
    ot_error_t status;

    switch (base)
    {
        case 10:
            value_len_exact = 0;
            break;
        case 16:
            value_len_exact = sizeof_value * 2;
            break;
        default:
            LOGE("Unsupported base %d", base);
            return false;
    }

    status = otbr_cli_get_value(cmd, -1, buffer, &value_len, value_len_exact);
    if (status != OT_ERROR_NONE)
    {
        LOGE("Failed to get '%s' base-%d value (%d, %d; %d)", cmd, base, value_len, value_len_exact, status);
        return false;
    }
    buffer[value_len] = '\0';

    return strtonum(buffer, value, sizeof_value, base) != NULL;
}

ssize_t otbr_cli_get_string(otbr_cli_cmd_t cmd, char *const buffer, size_t buffer_size)
{
    ot_error_t status;

    ASSERT(buffer_size > 0, "Insufficient buffer size");
    buffer_size--; /*< Reserve space for null-terminator */

    status = otbr_cli_get_value(cmd, -1, buffer, &buffer_size, 0);
    if (status != OT_ERROR_NONE)
    {
        LOGE("Failed to get '%s' value (%d)", cmd, status);
        return -1;
    }

    buffer[buffer_size] = '\0';
    return (ssize_t)buffer_size;
}

ssize_t otbr_cli_get_array(otbr_cli_cmd_t cmd, uint8_t *const buffer, size_t buffer_size)
{
    size_t str_len = buffer_size * 2 + 1;
    char *str;
    ot_error_t status;
    ssize_t ret = -1;

    str = MALLOC(str_len);

    status = otbr_cli_get_value(cmd, -1, str, &str_len, 0);
    if (status == OT_ERROR_NONE)
    {
        ret = hex2bin(str, str_len, buffer, buffer_size);
        if ((ret * 2) != (int)str_len)
        {
            LOGE("Failed to parse '%s' value '%s' as max. %d B array (%d)", cmd, str, buffer_size, ret);
            ret = -1;
        }
    }
    else if (status == OT_ERROR_NOT_FOUND)
    {
        ret = 0;
    }
    else
    {
        LOGE("Failed to get '%s' value (%d)", cmd, status);
    }

    FREE(str);
    return ret;
}
