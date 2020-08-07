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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "log.h"
#include "osp_ps.h"
#include "ds_dlist.h"

#include "osps.h"
#include "module.h"

bool osps_preserve = false;

static const char usage[] =
"osps [-dv] COMMAND  ...\n"
"\n"
"   -d  debug mode\n"
"   -v  verbose\n"
"   -p  use preserved storage (OSP_PS_PRESERVE)\n";

static ds_dlist_t g_command_list = DS_DLIST_INIT(struct osps_command, oc_dnode);

static int osps_help(int argc, char *argv[]);
static int osps_get(int argc, char *argv[]);
static int osps_set(int argc, char *argv[]);
static int osps_del(int argc, char *argv[]);
static int osps_erase(int argc, char *argv[]);

void osps_command_register(struct osps_command *oc)
{
    ds_dlist_insert_tail(&g_command_list, oc);
}

/**
 * Show long help text for command @p cmd
 */
__attribute__((__format__ (__printf__, 2, 0)))
void osps_usage(const char *cmd, const char *fmt, ...)
{
    struct osps_command *oc;

    if (cmd != NULL)
    {
        ds_dlist_foreach(&g_command_list, oc)
        {
            if (strcmp(cmd, oc->oc_name) == 0)
            {
                break;
            }
        }

        if (oc != NULL)
        {
            fprintf(stderr, "%s %s\n\n%s\n", "osps", oc->oc_brief, oc->oc_help);
        }
        else
        {
            fprintf(stderr, "ERROR: Help for command '%s' not found.", cmd);
        }
    }
    else
    {
        fprintf(stderr, "%s\nAvailable commands:\n", usage);
        ds_dlist_foreach(&g_command_list, oc)
        {
            fprintf(stderr, "    %s\n", oc->oc_brief);
        }
    }

    if (fmt != NULL)
    {
        fprintf(stderr, "\nERROR: ");
        va_list va;

        va_start(va, fmt);
        vfprintf(stderr, fmt, va);
        va_end(va);

        fprintf(stderr, "\n");
    }
}

/*
 * ===========================================================================
 *  HELP command
 * ===========================================================================
 */
static struct osps_command osps_help_cmd = OSPS_COMMAND_INIT(
        "help",
        osps_help,
        "help [COMMAND] ; Show help",
        "If COMMAND is not specified, show main help screen\n");

static int osps_help(int argc, char *argv[])
{
    if (argc < 2)
    {
        osps_usage(NULL, NULL);
    }
    else
    {
        osps_usage(argv[1], NULL);
    }

    return 0;
}
/*
 * ===========================================================================
 *  GET command
 * ===========================================================================
 */
static struct osps_command osps_get_cmd = OSPS_COMMAND_INIT(
        "get",
        osps_get,
        "get STORE KEY ; Retrieve a single key from persistent storage",
        "Arguments:\n"
        "\n"
        "   STORE   - The persistent store name\n"
        "   KEY     - The key name\n");

static int osps_get(int argc, char *argv[])
{
    ssize_t datasz;
    osp_ps_t *ps;

    int flags = OSP_PS_READ;
    char *pdata = NULL;
    int retval = 1;

    if (argc < 3)
    {
        osps_usage("get", "Invalid number of arguments.", argc);
        return OSPS_CLI_ERROR;
    }

    if (osps_preserve) flags |= OSP_PS_PRESERVE;

    ps = osp_ps_open(argv[1], flags);
    if (ps == NULL)
    {
        fprintf(stderr, "Error opening store: %s", argv[1]);
        return 1;
    }

    /* Retrieve the size of the store */
    datasz = osp_ps_get(ps, argv[2], NULL, 0);
    if (datasz <= 0)
    {
        fprintf(stderr, "Key not found or error: %s\n", argv[2]);
        goto error;
    }

    pdata = malloc(datasz);

    datasz = osp_ps_get(ps, argv[2], pdata, datasz);
    if (datasz <= 0)
    {
        fprintf(stderr, "Error retrieving data for key: %s\n", argv[2]);
        goto error;
    }

    write(1, pdata, datasz);

    retval = 0;

error:
    if (ps != NULL && !osp_ps_close(ps))
    {
        fprintf(stderr, "Warning: Error closing store: %s\n", argv[1]);
    }

    if (pdata != NULL) free(pdata);

    return retval;
}

/*
 * ===========================================================================
 *  SET command
 * ===========================================================================
 */
static struct osps_command osps_set_cmd = OSPS_COMMAND_INIT(
        "set",
        osps_set,
        "set STORE KEY DATA|-; Store a single key to persistent storage",
        "The STORE is created if it does not exist.\n"
        "\n"
        "Arguments:\n"
        "\n"
        "   STORE   - The persistent store name\n"
        "   KEY     - The key name\n"
        "   DATA    - String to store as data or \"-\" if data should be read STDIN\n");

static int osps_set(int argc, char *argv[])
{
    ssize_t bufsz;
    osp_ps_t *ps;
    ssize_t rc;

    int retval = 1;
    int flags = OSP_PS_WRITE;
    char *buf = NULL;

    if (argc != 4)
    {
        osps_usage("set", "Invalid number of arguments.");
        return OSPS_CLI_ERROR;
    }

    /* If the last argument is '-', read data from stdin */
    if (strcmp(argv[3],  "-") == 0)
    {
        buf = NULL;
        bufsz = 0;

        do
        {
            buf = realloc(buf, bufsz + 4096);
            rc = read(0, buf + bufsz, 4096);
            if (rc < 0) break;
            bufsz += rc;
        }
        while (rc > 0);
    }
    else
    {
        buf = strdup(argv[3]);
        bufsz = strlen(buf);
    }

    if (osps_preserve) flags |= OSP_PS_PRESERVE;

    ps = osp_ps_open(argv[1], flags);
    if (ps == NULL)
    {
        fprintf(stderr, "Error opening store: %s", argv[1]);
        goto error;
    }

    /* Retrieve the size of the store */
    rc = osp_ps_set(ps, argv[2], buf, bufsz);
    if (rc != bufsz)
    {
        fprintf(stderr, "Unable to set key: %s\n", argv[2]);
        goto error;
    }

    retval = 0;

error:
    if (ps != NULL) osp_ps_close(ps);
    if (buf != NULL) free(buf);

    return retval;
}

/*
 * ===========================================================================
 *  DEL command
 * ===========================================================================
 */
static struct osps_command osps_del_cmd = OSPS_COMMAND_INIT(
        "del",
        osps_del,
        "del STORE KEY KEY...; Delete keys from persistent storage",
        "\n"
        "Arguments:\n"
        "\n"
        "   STORE   - The persistent store name\n"
        "   KEY     - The key name; multiple keys can be specified\n");

static int osps_del(int argc, char *argv[])
{
    osp_ps_t *ps;
    ssize_t rc;
    int flags;
    int ii;

    if (argc < 2)
    {
        osps_usage("del", "Invalid number of arguments.");
        return OSPS_CLI_ERROR;
    }

    /* First open in read-only mode just to see if the store exists */
    flags = OSP_PS_READ;
    if (osps_preserve) flags |= OSP_PS_PRESERVE;

    ps = osp_ps_open(argv[1], flags);
    if (ps == NULL)
    {
        fprintf(stderr, "Error opening store (RO): %s", argv[1]);
        return 1;
    }

    if (!osp_ps_close(ps))
    {
        fprintf(stderr, "Warning: Error closing store (RO): %s\n", argv[1]);
    }

    /*
     * Re-open store in write mode
     */
    flags = OSP_PS_WRITE;
    if (osps_preserve) flags |= OSP_PS_PRESERVE;

    ps = osp_ps_open(argv[1], flags);
    if (ps == NULL)
    {
        fprintf(stderr, "Error opening store (RW): %s", argv[1]);
        return 1;
    }

    for (ii = 2; ii < argc; ii++)
    {
        /* Retrieve the size of the store */
        rc = osp_ps_set(ps, argv[ii], NULL, 0);
        if (rc != 0)
        {
            fprintf(stderr, "Unable to delete key: %s\n", argv[ii]);
            continue;
        }
    }

    if (!osp_ps_close(ps))
    {
        fprintf(stderr, "Warning: Error closing store: %s\n", argv[1]);
        return 1;
    }

    return 0;
}

/*
 * ===========================================================================
 *  ERASE command
 * ===========================================================================
 */
static struct osps_command osps_erase_cmd = OSPS_COMMAND_INIT(
        "erase",
        osps_erase,
        "erase STORE ; Delete all keys from persistent storage",
        "\n"
        "Arguments:\n"
        "\n"
        "   STORE   - The persistent store name\n");

static int osps_erase(int argc, char *argv[])
{
    osp_ps_t *ps;
    int flags;

    if (argc != 2)
    {
        osps_usage("erase", "Invalid number of arguments.");
        return OSPS_CLI_ERROR;
    }

    /* First open in read-only mode just to see if the store exists */
    flags = OSP_PS_READ;
    if (osps_preserve) flags |= OSP_PS_PRESERVE;

    ps = osp_ps_open(argv[1], flags);
    if (ps == NULL)
    {
        fprintf(stderr, "Error opening store (RO): %s", argv[1]);
        return 1;
    }

    if (!osp_ps_close(ps))
    {
        fprintf(stderr, "Warning: Error closing store (RO): %s\n", argv[1]);
    }

    /*
     * Re-open store in write mode
     */
    flags = OSP_PS_WRITE;
    if (osps_preserve) flags |= OSP_PS_PRESERVE;

    ps = osp_ps_open(argv[1], flags);
    if (ps == NULL)
    {
        fprintf(stderr, "Error opening store (RW): %s", argv[1]);
        return 1;
    }

    if (!osp_ps_erase(ps))
    {
        fprintf(stderr, "Unable to erase store: %s\n", argv[1]);
    }

    if (!osp_ps_close(ps))
    {
        fprintf(stderr, "Warning: Error closing store: %s\n", argv[1]);
        return 1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    struct osps_command *oc;
    int opt;

    bool verbose = false;
    bool debug = false;

    /*
     * getop() will complain to stderr about unknown options unless this flag
     * is set to 0.
     */
    opterr = 0;

    /*
     * Register the standard set of commands
     */
    osps_command_register(&osps_help_cmd);
    osps_command_register(&osps_get_cmd);
    osps_command_register(&osps_set_cmd);
    osps_command_register(&osps_del_cmd);
    osps_command_register(&osps_erase_cmd);

    /*
     * Initialize modules
     */
    module_init();

    /*
     * Parse arguments
     */
    while ((opt = getopt(argc, argv, "+vdp")) != -1)
    {
        switch (opt)
        {
            case 'v':
                verbose = true;
                break;

            case 'd':
                debug = true;
                break;

            case 'p':
                osps_preserve = true;
                break;

            case '?':
                osps_usage(NULL, "Invalid option -%c", optopt);
                return OSPS_CLI_ERROR;
        }
    }

    argc -= optind;
    argv += optind;

    log_open("osps", verbose ? LOG_OPEN_DEFAULT : LOG_OPEN_SYSLOG);

    if (debug)
    {
        log_severity_set(LOG_SEVERITY_DEBUG);
    }

    if (argc < 1)
    {
        osps_usage(NULL, "At least 1 argument is required.");
        return OSPS_CLI_ERROR;
    }

    ds_dlist_foreach(&g_command_list, oc)
    {
        if (strcmp(argv[0], oc->oc_name) == 0)
        {
            return oc->oc_fn(argc, argv);
        }
    }

    osps_usage(NULL, "Unknown command: %s", argv[0]);

    return OSPS_CLI_ERROR;
}
