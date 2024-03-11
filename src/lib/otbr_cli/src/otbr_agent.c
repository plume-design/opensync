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

#include <stdio.h>

#include "log.h"
#include "otbr_agent.h"
#include "util.h"

#define LOG_PREFIX "[otbr-agent] "

bool otbr_agent_init(daemon_t *const agent, daemon_atexit_fn_t *const on_agent_exit_cb)
{
    ASSERT(agent->dn_exec == NULL, "Already initialized");

    if (!(
                /* Reset the daemon context and initialize it */
                daemon_init(agent, CONFIG_OTBR_CLI_AGENT_DAEMON_PATH, DAEMON_LOG_ALL) &&
                /* Use custom provided PID file as otbr-agent does not create any */
                daemon_pidfile_set(agent, CONFIG_OTBR_CLI_AGENT_DAEMON_PID_FILE, true) &&
                /* Catch/handle otbr-agent crashes */
                daemon_atexit(agent, on_agent_exit_cb)))
    {
        LOGE(LOG_PREFIX "Failed to initialize");
        otbr_agent_cleanup(agent);
        return false;
    }

    LOGD(LOG_PREFIX "Initialized");
    return true;
}

bool otbr_agent_start(daemon_t *const agent, const char *const thread_iface, const char *const network_iface)
{
    bool started = false;
    int debug_level;
    char str[128];

    if (daemon_is_started(agent, &started) && started)
    {
        LOGW(LOG_PREFIX "Already running, stopping first");
        if (!daemon_stop(agent))
        {
            return false;
        }
    }

    /* Add arguments, usage:
     * otbr-agent [-I interfaceName] [-B backboneIfName] [-d DEBUG_LEVEL] [-v] [--auto-attach[=0/1]=1] RADIO_URL
     * [RADIO_URL]
     */
    daemon_arg_reset(agent);

    /* -I interfaceName */
    snprintf(str, sizeof(str), "%s", thread_iface);
    daemon_arg_add(agent, "-I", str);

    /* -B backboneIfName */
    if (network_iface != NULL)
    {
        snprintf(str, sizeof(str), "%s", network_iface);
        daemon_arg_add(agent, "-B", str);
    }

    /* -d DEBUG_LEVEL (in strict range from 0 (OTBR_LOG_EMERG) to 7 (OTBR_LOG_DEBUG))
     *   Level from EMERG onwards are in the same order, so align on EMERG */
    debug_level = MAX(0, (int)log_severity_get() - LOG_SEVERITY_EMERG);
    debug_level = MIN(7, debug_level);
    snprintf(str, sizeof(str), "%d", debug_level);
    daemon_arg_add(agent, "-d", str);

    /* Verbose */
    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
    {
        daemon_arg_add(agent, "-v");
    }

    /* RADIO_URL (spinel+hdlc+uart://${PATH_TO_UART_DEVICE}?${Parameters} for real UART device) */
    if (strlen(CONFIG_OTBR_CLI_THREAD_RADIO_URL) > 1)
    {
        daemon_arg_add(agent, CONFIG_OTBR_CLI_THREAD_RADIO_URL);
    }
    else if (network_iface == NULL)
    {
        LOGE(LOG_PREFIX "Thread radio URL not provided");
    }

    /* [RADIO_URL] (Thread Radio Encapsulation Link) */
    if (network_iface != NULL)
    {
        snprintf(str, sizeof(str), "trel://%s", network_iface);
        daemon_arg_add(agent, str);
    }

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_DEBUG))
    {
        char cmd[256];
        char *p_cmd = cmd;

        p_cmd += snprintf(p_cmd, sizeof(cmd), "%s", agent->dn_exec);
        /* Skip the first argument which is the exec paths */
        for (int i = 1; i < agent->dn_argc; i++)
        {
            p_cmd += snprintf(p_cmd, sizeof(cmd) - (p_cmd - cmd), " %s", agent->dn_argv[i]);
        }

        LOGT(LOG_PREFIX "Starting '%s'", cmd);
    }

    if (!daemon_start(agent))
    {
        LOGE(LOG_PREFIX "Failed to start");
        return false;
    }

    LOGD(LOG_PREFIX "Started");
    return true;
}

bool otbr_agent_is_running(const daemon_t *const agent)
{
    bool running;

    return daemon_is_started(agent, &running) && running;
}

void otbr_agent_stop(daemon_t *const agent)
{
    if (otbr_agent_is_running(agent))
    {
        const bool status = daemon_stop(agent);
        LOGD(LOG_PREFIX "Stopped %s", status ? "successfully" : "with errors");
    }
    else
    {
        LOGD(LOG_PREFIX "Already stopped");
    }
}

void otbr_agent_cleanup(daemon_t *const agent)
{
    daemon_fini(agent);
    memset(agent, 0, sizeof(*agent));
    LOGD(LOG_PREFIX "Cleaned up");
}
