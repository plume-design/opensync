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

#ifndef OTBR_AGENT_H_INCLUDED
#define OTBR_AGENT_H_INCLUDED

#include <stdbool.h>

#include "daemon.h"
#include "otbr_cli_util.h"

/**
 * Initialize OTBR agent daemon process
 *
 * @param[in] agent             Daemon context structure.
 * @param[in] on_agent_exit_cb  Callback function invoked when the daemon exits unexpectedly,
 *                              e.g. due to a crash, not as a result of calling
 *                              @ref otbr_agent_stop() or @ref otbr_agent_cleanup().
 *
 * @return true on success. Failure is logged internally.
 *
 * @note Call @ref otbr_agent_cleanup() after usage to cleanup resources.
 */
bool otbr_agent_init(daemon_t *agent, daemon_atexit_fn_t *on_agent_exit_cb) NONNULL(1);

/**
 * Start the OTBR agent process
 *
 * @param[in] agent          Daemon context structure.
 * @param[in] thread_iface   Thread network interface name.
 * @param[in] network_iface  Optional backbone network interface name.
 *
 * @return true on success. Failure is logged internally.
 */
bool otbr_agent_start(daemon_t *agent, const char *thread_iface, const char *network_iface) NONNULL(1, 2);

/**
 * Check if the agent was started using @ref otbr_agent_start()
 *
 * @param[in] agent  Daemon context structure.
 *
 * @returns true if the agent is running, false otherwise.
 */
bool otbr_agent_is_running(const daemon_t *agent) NONNULL(1);

/**
 * Stop the OTBR agent process
 *
 * Nothing is done if the agent is not running (can be called multiple times).
 *
 * @param[in] agent  Daemon context structure.
 */
void otbr_agent_stop(daemon_t *agent) NONNULL(1);

/**
 * Cleanup the OTBR agent daemon process resources
 *
 * This function stops the agent if it is running.
 * Nothing is done if the agent was not initialized (can be called multiple times).
 *
 * @param[in] agent  Daemon context structure.
 */
void otbr_agent_cleanup(daemon_t *agent) NONNULL(1);

#endif /* OTBR_AGENT_H_INCLUDED */
