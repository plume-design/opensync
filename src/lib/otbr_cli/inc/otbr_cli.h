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

#ifndef OTBR_CLI_H_INCLUDED
#define OTBR_CLI_H_INCLUDED

#include <ev.h>
#include <stdbool.h>

#include "otbr_cli_error.h"
#include "otbr_cli_util.h"

enum otbr_cli_failure_reason_e
{
    OTBR_CLI_FAILURE_REASON_DAEMON_CRASH
};

/** @see @ref otbr_cli_init() */
typedef void otbr_cli_on_failure_fn_t(enum otbr_cli_failure_reason_e reason);

/** Represents a CLI command string - return value of @ref otbr_cli_cmd(), or a null-terminated string */
typedef const char *otbr_cli_cmd_t;

/**
 * Represents a response data - call @ref otbr_cli_response_free() to free the allocated memory after usage
 *
 * This list represent each line of the response text, with line endings removed and null-terminator added.
 * List is `NULL` if not parsed yet or invalid, but may be `NULL` even if status is `OT_ERROR_NONE`, if
 * the response is empty. On the other hand, it may be not-`NULL` even if status indicates an error, if
 * the response contains an error message.
 */
typedef struct
{
    char **lines; /**< Array of null-terminated response lines, may be `NULL` if `count` is 0 */
    size_t count; /**< Number of response lines in `lines` */
} otbr_cli_response_t;

/**
 * Initialize the OpenThread Border Router Agent CLI library
 *
 * This function must be called before using any other functions.
 * Calling this function twice without calling @ref otbr_cli_close()
 * in between is not allowed.
 *
 * @param[in] on_failure_cb  Optional callback function invoked in case of any
 *                           unrecoverable error in the library, requiring it
 *                           to be closed and initialized again.
 *                           The detailed failure reason is logged internally.
 *
 * @return true on success. Failure is logged internally.
 *
 * @note Call @ref otbr_cli_close() after usage cleanup resources.
 */
bool otbr_cli_init(otbr_cli_on_failure_fn_t *on_failure_cb);

/**
 * Start the OTBR agent daemon and establish socket connection to it
 *
 * If the agent is already running, it is stopped and restarted with the new parameters.
 *
 * @param[in] loop             Event loop to use for handling the socket connection.
 * @param[in] thread_if_name   Thread network interface name.
 * @param[in] network_if_name  Backbone network interface name, or `NULL`
 *                             to disable Border Routing feature.
 *
 * @return true on success. Failure is logged internally.
 *
 * @note Call @ref otbr_cli_stop() or @ref otbr_cli_close() after usage to cleanup resources.
 *
 * @note This only manages OTBR agent, it does not manage the Thread network interface or the Thread radio.
 */
bool otbr_cli_start(struct ev_loop *loop, const char *thread_if_name, const char *network_if_name) NONNULL(1, 2);

/**
 * Check if the agent was started using @ref otbr_cli_start()
 *
 * @returns true if the agent is started and connection established, false otherwise.
 */
bool otbr_cli_is_running(void);

/**
 * Build a CLI command and save it into the internal command buffer
 *
 * Although not required, null-terminator is always added at the end
 * of the command string for the convenience and easier debugging.
 *
 * @param[in] fmt  The command format string.
 * @param[in] ...  Optional arguments for the format string.
 *
 * @return pointer to the beginning of the command string in the internal command buffer.
 *
 * @see @ref otbr_cli_execute()
 */
otbr_cli_cmd_t otbr_cli_cmd(const char *fmt, ...) __attribute__((__format__(__printf__, 1, 2))) NONNULL(1);

/**
 * Send a CLI command to the agent and wait for response
 *
 * @param[in]  cmd       Command string to send. If not generated by @ref otbr_cli_cmd(), it must be null-terminated.
 * @param[out] response  Pointer on the cleared response structure which will be filled with the response data.
 *                       If `NULL`, the response data is ignored - but the response is still awaited and parsed
 *                       status code returned.
 * @param[in]  timeout   Command response timeout in seconds, or negative value (-1) to use the default timeout.
 *
 * @return `OT_ERROR_NONE` if command is successful. Failures are logged internally,
 *         except the final CLI command response (values greater than `OT_ERROR_NONE`).
 *
 * @note If `response` is not `NULL`, call @ref otbr_cli_response_free() after usage to cleanup resources.
 */
ot_error_t otbr_cli_execute(otbr_cli_cmd_t cmd, otbr_cli_response_t *response, float timeout) NONNULL(1);

/**
 * Check if this module is currently busy waiting for, or processing a @ref otbr_cli_execute() command
 *
 * @returns true if busy, false otherwise.
 */
bool otbr_cli_is_busy(void);

/**
 * Free the allocated response data
 *
 * Nothing is done if this response was already freed.
 *
 * @param[in] response  Response structure to reset (the pointer itself is not freed).
 */
void otbr_cli_response_free(otbr_cli_response_t *response);

/**
 * Close the socket and stop the OTBR agent daemon
 *
 * Nothing is done if the agent is not running.
 *
 * @note This only manages OTBR agent lifetime, it does not explicitly manage
 *      the Thread network interface or the Thread radio.
 */
void otbr_cli_stop(void);

/**
 * Cleanup the OTBR CLI library
 *
 * Nothing is done if the library has not been initialized yet.
 */
void otbr_cli_close(void);

/* *** Higher-level helper functions related to command execution *** */

/**
 * Send a CLI command to the agent and wait for response
 *
 * This is a wrapper for @ref otbr_cli_execute() which does not require the response data
 * and accepts only `OT_ERROR_NONE` as a successful status code.
 *
 * @param[in] cmd      Command string to send. If not generated by @ref otbr_cli_cmd(), it must be null-terminated.
 * @param[in] timeout  Command response timeout in seconds, or negative value (-1) to use the default timeout.
 *
 * @return true if command resulted in `OT_ERROR_NONE`. Failures are logged internally.
 */
bool otbr_cli_exec(otbr_cli_cmd_t cmd, float timeout) NONNULL(1);

/**
 * Send a CLI command to the agent and wait for response
 *
 * This is a wrapper for @ref otbr_cli_execute() which accepts only `OT_ERROR_NONE`
 * as a successful status code and expects at least certain number of response lines.
 *
 * @param[in]  cmd        Command string to send. If not generated by @ref otbr_cli_cmd(), it must be null-terminated.
 * @param[out] response   Pointer on the cleared response structure which will be filled with the response data.
 *                        Call @ref otbr_cli_response_free() after usage to cleanup resources.
 * @param[in]  min_count  Minimum number of expected response lines.
 * @param[in]  timeout    Command response timeout in seconds, or negative value (-1) to use the default timeout.
 *
 * @return true if command resulted in `OT_ERROR_NONE` and there are at least `min_count` lines in the response.
 *         Failures are logged internally.
 */
bool otbr_cli_get(otbr_cli_cmd_t cmd, otbr_cli_response_t *response, size_t min_count, float timeout) NONNULL(1, 2);

/**
 * Execute the command and get the returned numeric value
 *
 * @param[in]  cmd           Command to send to get the value.
 * @param[out] value         Pointer to the variable to store the value to (fixed‐width basic integer types).
 * @param[in]  sizeof_value  `sizeof(*value)`: 1, 2, 4, and 8 are supported.
 * @param[in]  base          (-)10 to parse (signed) decimal value, (-)16 to parse (signed) hexadecimal value.
 *
 * @return true if command execution was successful and the value was written. Failures are logged internally.
 *
 * @see @ref strtonum()
 */
bool otbr_cli_get_number(otbr_cli_cmd_t cmd, void *value, size_t sizeof_value, int base) NONNULL(1, 2);

/**
 * Execute the command and get the returned string value
 *
 * @param[in]  cmd          Command to send to get the value.
 * @param[out] buffer       Pointer to the buffer to copy the value to. Null-terminator is always added at the end.
 * @param[in]  buffer_size  Maximum number of characters to write to the buffer (including the null terminator).
 *
 * @return length of the string if command execution was successful, or
 *         -1 on failure (logged internally).
 */
ssize_t otbr_cli_get_string(otbr_cli_cmd_t cmd, char *buffer, size_t buffer_size) NONNULL(1, 2);

/**
 * Execute the command and get the returned array value
 *
 * @param[in]  cmd          Command to send to get the value.
 * @param[out] buffer       Pointer to the buffer to copy the value to.
 * @param[in]  buffer_size  Maximum number of bytes to write to the buffer.
 *
 * @return number of bytes written to the buffer,
 *         0 if command returned `OT_ERROR_NOT_FOUND`,
 *         -1 on failure (logged internally).
 */
ssize_t otbr_cli_get_array(otbr_cli_cmd_t cmd, uint8_t *buffer, size_t buffer_size) NONNULL(1, 2);

#endif /* OTBR_CLI_H_INCLUDED */
