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
 * Custom extension for the PSFS backend
 * ===========================================================================
 */
#include <stdio.h>
#include <getopt.h>

#include "module.h"
#include "osp_ps.h"
#include "psfs.h"

#include "osps.h"


static int osps_list(int argc, char *argv[]);
static int osps_prune(int argc, char *argv[]);

/*
 * ===========================================================================
 *  List command
 * ===========================================================================
 */
static struct osps_command osps_list_cmd = OSPS_COMMAND_INIT(
        "list",
        osps_list,
        "list STORE ; List keys available in store [PSFS extension]",
        "Arguments:\n"
        "\n"
        "   STORE   - The persistent store name\n");


int osps_list(int argc, char *argv[])
{
    struct psfs_record *pr;
    psfs_t ps;

    int flags = OSP_PS_READ;
    int retval = 1;

    if (argc != 2)
    {
        osps_usage("list", "Invalid number of arguments.");
        return OSPS_CLI_ERROR;
    }

    if (osps_preserve) flags |= OSP_PS_PRESERVE;

    if (!psfs_open(&ps, argv[1], flags))
    {
        fprintf(stderr, "Error opening store %s.\n", argv[1]);
        return 1;
    }

    if (!psfs_load(&ps))
    {
        fprintf(stderr, "Error loading data from store %s.\n", argv[1]);
        goto error;
    }

    ds_tree_foreach(&ps.psfs_root, pr)
    {
        printf("%s\n", pr->pr_key);
    }

    retval = 0;

error:
    if (!psfs_close(&ps))
    {
        fprintf(stderr, "Warning: Error closing store %s.\n", argv[1]);
        retval = 1;
    }

    return retval;
}

/*
 * ===========================================================================
 *  List command
 * ===========================================================================
 */
static struct osps_command osps_prune_cmd = OSPS_COMMAND_INIT(
        "prune",
        osps_prune,
        "prune STORE ; Prune a store [PSFS extension]",
        "Arguments:\n"
        "\n"
        "   STORE   - The persistent store name\n");


int osps_prune(int argc, char *argv[])
{
    int flags;
    psfs_t ps;

    int retval = 1;

    if (argc != 2)
    {
        osps_usage("list", "Invalid number of arguments.");
        return OSPS_CLI_ERROR;
    }

    /*
     * Check if a store exists by opening it read-only first
     */
    flags = OSP_PS_READ;
    if (osps_preserve) flags |= OSP_PS_PRESERVE;

    if (!psfs_open(&ps, argv[1], flags))
    {
        fprintf(stderr, "Error opening store (RO): %s.\n", argv[1]);
        return 1;
    }

    if (!psfs_close(&ps))
    {
        fprintf(stderr, "Error closing store (RO): %s\n", argv[1]);
        return 1;
    }

    /*
     * Re-open the store in R/W mode
     */
    flags = OSP_PS_WRITE;
    if (osps_preserve) flags |= OSP_PS_PRESERVE;

    if (!psfs_open(&ps, argv[1], flags))
    {
        fprintf(stderr, "Error opening store (RW): %s\n", argv[1]);
        return 1;
    }

    if (!psfs_load(&ps))
    {
        fprintf(stderr, "Error loading data from store: %s\n", argv[1]);
        goto error;
    }

    /*
     * Sync by forcing a prune
     */
    if (!psfs_sync(&ps, true))
    {
        fprintf(stderr, "Error pruning store: %s\n", argv[1]);
        goto error;
    }

    retval = 0;

error:
    if (!psfs_close(&ps))
    {
        fprintf(stderr, "Warning: Error closing store %s.\n", argv[1]);
        retval = 1;
    }

    return retval;
}

/*
 * ===========================================================================
 *  Module section
 * ===========================================================================
 */
MODULE(osps_psfs, osps_psfs_init, osps_psfs_fini)

void osps_psfs_init(void)
{
    osps_command_register(&osps_list_cmd);
    osps_command_register(&osps_prune_cmd);
}

void osps_psfs_fini(void)
{
}
