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

/*
 * ===========================================================================
 *  Wrapper for system's reboot command
 * ===========================================================================
 */
#include <sys/wait.h>

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "osp_reboot.h"
#if 1
#include "util.h"
#include "const.h"
#else
#define STRSCPY(x, y) strcpy(x, y)
#define os_atol(x, y) (*(y) = atol(x),true)
#define ARRAY_LEN(x) (ssize_t)(sizeof(x) / sizeof((x)[0]))
#define CONFIG_OSP_REBOOT_COMMAND "/bin/busybox reboot"
#define osp_unit_reboot_ex(type, reason, delay)     \
{                                                   \
    printf("REBOOT: %s: %s\n", preboot_type_str[type], reason == NULL ? "" : reason); \
}

#endif
/**
 * This is the option that will be used as the prefix for all other preboot
 * options. All other command line arguments will be passed to the system's
 * reboot command as-is.
 */
#define PREBOOT_OPT_PREFIX  "-R"

/**
 * Helper macro for dynamic realloc()
 */
#define PREBOOT_REALLOC(ptr, sz, n) \
    if ((n % 16) == 0) (ptr) = realloc((ptr), (sz) * ((n) + 16))

/*
 * Default actions
 */
static enum osp_reboot_type preboot_type = OSP_REBOOT_USER;
static char preboot_reason[512] = "Reboot via shell command";

/**
 * Mapping from reboot type to string
 */

const char* const preboot_type_str[] =
{
    [OSP_REBOOT_CANCEL] = "cancel",
    [OSP_REBOOT_UNKNOWN] = "unknown",
    [OSP_REBOOT_COLD_BOOT] = "coldboot",
    [OSP_REBOOT_POWER_CYCLE] = "powercycle",
    [OSP_REBOOT_WATCHDOG] = "watchdog",
    [OSP_REBOOT_CRASH] = "crash",
    [OSP_REBOOT_USER] = "user",
    [OSP_REBOOT_DEVICE] = "device",
    [OSP_REBOOT_HEALTH_CHECK] = "healthcheck",
    [OSP_REBOOT_UPGRADE] = "upgrade",
    [OSP_REBOOT_CLOUD] = "cloud"
};

/**
 * Issue a system reboot
 */
int preboot_system_reboot(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    return 0;
}

void preboot_usage(void)
{
static const char usage[] = \
    "preboot "PREBOOT_OPT_PREFIX"[PREBOOT_OPTION]... [OPTION]...\n"
    "Wrapper around the system's reboot command. It is used to store the reboot\n"
    "reason before issuing a reboot sequence.\n"
    "\n"
    "PREBOOT_OPTIONs will be parsed by this tool, while OPTIONS will passed to \n"
    "the system reboot command as-is.\n"
    "\n"
    "Available PREBOOT_OPTIONs:\n"
    "\n"
    "   "PREBOOT_OPT_PREFIX"help          Show this help screen\n"
    "   "PREBOOT_OPT_PREFIX"type=TYPE     Set the reboot type\n"
    "   "PREBOOT_OPT_PREFIX"reason=string Set the reboot reason (string)\n"
    "\n"
    "Note: preboot "PREBOOT_OPT_PREFIX"type=list will list all available reboot types.\n"
    PREBOOT_OPT_PREFIX"type=cancel will not actually issue a reboot.\n";

    fprintf(stderr, "%s\n", usage);
}

bool preboot_parse_subopts(const char *subopt, const char *value)
{
    if (strcmp(subopt, "help") == 0)
    {
        preboot_usage();
        exit(1);
    }
    else if (strcmp(subopt, "type") == 0)
    {
        int ii;

        if (value == NULL)
        {
            fprintf(stderr, "Error: type requires an argument.\n");
            return false;
        }

        /*
         * Special case where -Rtype=list lists all available
         * reboot types.
         */
        if (strcmp(value, "list") == 0)
        {
            for (ii = 0; ii < ARRAY_LEN(preboot_type_str); ii++)
            {
                if (preboot_type_str[ii] == NULL) continue;
                printf("%s\n", preboot_type_str[ii]);
            }
            exit(0);
        }

        for (ii = 0; ii < ARRAY_LEN(preboot_type_str); ii++)
        {
            if (preboot_type_str[ii] == NULL) continue;
            if (strcmp(preboot_type_str[ii], value) == 0) break;
        }

        if (ii >= ARRAY_LEN(preboot_type_str))
        {
            fprintf(stderr, "Error: Unknown reboot type: %s. "
                    "Use "PREBOOT_OPT_PREFIX"type=list to get a list of valid types.\n",
                    value);
            return false;
        }

        preboot_type = ii;
    }
    else if (strcmp(subopt, "reason") == 0)
    {
        if (value == NULL)
        {
            fprintf(stderr, "Error: reason requires an argument.\n");
            return false;
        }

        /* Set the reboot reason */
        STRSCPY(preboot_reason, value);
    }
    else
    {
        fprintf(stderr, "Unknown argument: "PREBOOT_OPT_PREFIX"%s\n", subopt);
        return false;
    }

    return true;
}

int main(int argc, char *argv[])
{
    char **pt_argv;
    char *subopt;
    char *subval;
    char *cmd;
    char *pcmd;
    int pt_argc;
    int status;
    int ii;
    pid_t cpid;

    size_t preflen = strlen(PREBOOT_OPT_PREFIX);
    /* The command to execute for reboot */
    char sys_reboot[] = CONFIG_OSP_REBOOT_COMMAND;

    pt_argc = 0;
    pt_argv = NULL;

    /*
     * Split CONFIG_OSP_REBOOT_COMMAND into space-delimited substrings and
     * append them to pt_argv
     */
    for (cmd = strtok_r(sys_reboot, " ", &pcmd);
            cmd != NULL;
            cmd = strtok_r(NULL, " ", &pcmd))
    {
        PREBOOT_REALLOC(pt_argv, sizeof(char *), pt_argc);
        pt_argv[pt_argc++] = cmd;
    }

    for (ii = 1; ii < argc; ii++)
    {
        PREBOOT_REALLOC(pt_argv, sizeof(char *), pt_argc);
        if (strncmp(argv[ii], PREBOOT_OPT_PREFIX, preflen) != 0)
        {
           /*
            * Collect arguments for pass-through
            */
            pt_argv[pt_argc++] = argv[ii];
            continue;
        }

        subopt = argv[ii] + preflen;
        subval = strchr(subopt, '=');
        if (subval != NULL)
        {
            *subval++ = '\0';
        }

        if (!preboot_parse_subopts(subopt, subval))
        {
            fprintf(stderr, "Use "PREBOOT_OPT_PREFIX"help for usage.\n");
            return 255;
        }
    }

    PREBOOT_REALLOC(pt_argv, sizeof(char *), pt_argc);
    pt_argv[pt_argc] = NULL;

    /* Record the reboot reason */
    osp_unit_reboot_ex(preboot_type, preboot_reason, -1);

    /* If a cancel was requested, exit right now as we do not want a reboot. */
    if (preboot_type == OSP_REBOOT_CANCEL)
    {
        printf("Previously recorded reboot reason/counter was cancelled.\n");
        exit(0);
    }

    cpid = fork();
    if (cpid == 0)
    {
        execv(pt_argv[0], pt_argv);
        fprintf(stderr, "Error: execv() failed: %s\n", strerror(errno));
        return 255;
    }

    /*
     * Loop until the process terminates
     */
retry:
    if (waitpid(cpid, &status, 0) < 0)
    {
        if (errno == EINTR) goto retry;
        fprintf(stderr, "Error: waitpid() failed: %s\n", strerror(errno));
        osp_unit_reboot_ex(OSP_REBOOT_CANCEL, NULL, -1);
        return 255;
    }

    if (WEXITSTATUS(status) != 0 || WIFSIGNALED(status))
    {
        fprintf(stderr, "Error: Reboot command failed: %s\n", CONFIG_OSP_REBOOT_COMMAND);
        osp_unit_reboot_ex(OSP_REBOOT_CANCEL, NULL, -1);
        return WEXITSTATUS(status);
    }

    return 0;
}
