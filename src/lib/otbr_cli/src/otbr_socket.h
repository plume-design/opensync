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

#ifndef OTBR_SOCKET_H_INCLUDED
#define OTBR_SOCKET_H_INCLUDED

#include <ev.h>
#include <stdbool.h>

#include "otbr_cli_error.h"
#include "otbr_cli_util.h"

/** Maximum length of the CLI line */
#define OTBR_CLI_LINE_BUFFER_SIZE \
    1024 /*< OPENTHREAD_CONFIG_CLI_MAX_LINE_LENGTH is 640 by default, so 1024 B buffer shall suffice for normal use \
            cases */
/** OpenThread CLI line terminator string */
#define OTBR_CLI_RESPONSE_END_LINE "\r\n"

/** Represents a context used to communicate with the OTBR agent daemon */
typedef struct
{
    /** Event loop used for handling the connection */
    struct ev_loop *loop;
    /** Socket I/O watcher with socket file descriptor (-1 if not opened), used to send and receive messages */
    ev_io io;
    /** Timer used to handle timeouts when receiving command responses */
    ev_timer timer;
    /** Data used for creating and sending commands to the OTBR agent */
    struct
    {
        /** Buffer used for building and sending the command */
        char buffer[OTBR_CLI_LINE_BUFFER_SIZE];
        /** Current length of the command in the `buffer` */
        size_t len;
    } command;
    /** Data used for handling responses from the OTBR agent */
    struct
    {
        /** Buffer used for reading the response data */
        char buffer[OTBR_CLI_LINE_BUFFER_SIZE];
        /** Current length of the response in the `buffer` */
        size_t len;
        /** Flag indicating if the last read operation was successful */
        bool last_read_ok;
    } response;
} otbr_socket_t;

/**
 * Connect to OTBR agent socket
 *
 * @param[in,out] ctx           Socket context structure.
 * @param[in]     loop          Event loop used to monitor the socket.
 * @param[in]     thread_iface  Thread network interface name.
 * @param[in]     timeout       Connection timeout in seconds. If 0, connection attempt is done only once.
 * @param[in]     interval      Delay between connection attempts in seconds, if `timeout` is greater than 0.
 *
 * @return true on success. Failure is logged internally.
 *
 * @note Call @ref otbr_socket_close after usage to cleanup resources.
 */
bool otbr_socket_open(otbr_socket_t *ctx, struct ev_loop *loop, const char *thread_iface, float timeout, float interval)
        NONNULL(1, 2, 3);

/**
 * Check if the socket is connected to the OTBR agent daemon
 *
 * @param[in] ctx  Socket context structure.
 *
 * @return true if the socket is connected, false otherwise.
 */
bool otbr_socket_is_open(const otbr_socket_t *ctx) NONNULL(1);

/**
 * Write command from the internal command buffer to the socket directly
 *
 * @param[in] ctx  Socket context structure (must be connected).
 *
 * @return true on success. Failure is logged internally.
 */
bool otbr_socket_write_cmd(otbr_socket_t *ctx) NONNULL(1);

/**
 * Read all currently available data directly from the socket to the internal response buffer
 *
 * If there is more data available than the internal response buffer can hold, the remaining
 * data is not read and needs to be read later, after the buffer is processed.
 *
 * @param[in] ctx  Socket context structure (must be connected).
 *
 * @return true on success. Failure is logged internally.
 */
bool otbr_socket_read_rsp(otbr_socket_t *ctx) NONNULL(1);

/**
 * Discard all already received and currently available data from the socket
 *
 * This resets internal response buffer.
 *
 * @param[in,out]  ctx  Socket context structure.
 *
 * @return true on success. Failure is logged internally.
 */
bool socket_discard(otbr_socket_t *ctx) NONNULL(1);

/**
 * Start or stop the event loop IO watcher which receives data from the socket to the internal response buffer
 *
 * @param[in] ctx       Socket context structure (must be connected).
 * @param[in] activate  `true` to start the IO watcher, `false` to stop it.
 *
 * @return true if watcher state has changed, false if it was already in the requested state.
 */
bool otbr_socket_reader_activate(otbr_socket_t *ctx, bool activate) NONNULL(1);

/**
 * Check whether response IO watcher is active
 *
 * @param[in] ctx       Socket context structure (must be connected).
 *
 * @return true if the watcher is active, false otherwise.
 *
 * @see otbr_socket_reader_activate()
 */
bool otbr_socket_is_reader_active(const otbr_socket_t *ctx) NONNULL(1);

/**
 * Read a single CLI line from the socket
 *
 * @param[in]  ctx          Socket context structure (must be connected).
 * @param[out] buffer       Buffer to copy the line to (excluding line delimiter).
 *                          Null-terminator is always added at the end (even on failure).
 * @param[in]  buffer_size  Usable length of the buffer (at least 1 for the null-terminator).
 *
 * @note If reading multiple lines, use @ref otbr_socket_reader_activate before calling this function
 *       to start the IO watcher, and stop it after the last line is read.
 *
 * @note This function uses `ctx->timer` to spin the loop and handle the timeout. Caller is responsible
 *       for starting and stopping the timer.
 *
 * @return number of bytes read (excluding line delimiter and null-terminator), or -1 on failure (logged internally).
 */
ssize_t otbr_socket_readline(otbr_socket_t *ctx, char *buffer, size_t buffer_size) NONNULL(1, 2);

/**
 * Disconnect from OTBR agent socket and cleanup resources
 *
 * Nothing is done if the socket is not opened.
 *
 * @param[in,out] ctx  Socket context structure.
 */
void otbr_socket_close(otbr_socket_t *ctx) NONNULL(1);

#endif /* OTBR_SOCKET_H_INCLUDED */
