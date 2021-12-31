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

#include <sys/types.h>
#include <sys/wait.h>

#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "log.h"
#include "unity.h"
#include "procfs.h"
#include "execsh.h"

//#define PR(...) do { if (opt_verbose) fprintf(stderr, __VA_ARGS__); } while (0)
#define PR(...) do { if (opt_verbose) LOG(INFO, __VA_ARGS__); } while (0)

int opt_verbose = 0;

/*
 * ===========================================================================
 *  exec_sh() and scripts
 * ===========================================================================
 */
char sh_create_vlan[] = _S(ip link del "$1.$2" || true ; ip link add link "$1" name "$1.$2" type vlan id "$2");
char sh_delete_vlan[] = _S(ip link del "$1.$2");
#if 0
/* Not supported by our QSDK */
char sh_create_veth[] = _S(ip link add "$1" type veth peer name "$2");
char sh_delete_veth[] = _S(ip link del "$1" ; ip link del "$2");
#endif

/*
 * ===========================================================================
 *  ev utilities
 * ===========================================================================
 */

/*
 * Wait timeout or *trigger to become non-zero; returns false if timeout has expired, true otherwise
 */
void __ev_wait_dummy(EV_P_ ev_timer *w, int revent)
{
    (void)loop;
    (void)w;
    (void)revent;
}

bool ev_wait(int *trigger, double timeout)
{
    bool retval = false;

    /* the dnsmasq process is started with a short delay, sleep for few seconds before continuing */
    ev_timer ev;
    ev_timer_init(&ev, __ev_wait_dummy, timeout, 0.0);

    ev_timer_start(EV_DEFAULT, &ev);

    while (ev_run(EV_DEFAULT, EVRUN_ONCE))
    {
        if (!ev_is_active(&ev))
        {
            break;
        }

        if (trigger != NULL && *trigger != 0)
        {
            retval = true;
            break;
        }
    }

    if (ev_is_active(&ev)) ev_timer_stop(EV_DEFAULT, &ev);

    return retval;
}

/*
 * ===========================================================================
 *  procfs utilities
 * ===========================================================================
 */
bool procfs_entry_has_args(procfs_entry_t *pe, ...)
{
    char **parg;

    if (pe->pe_cmdline == NULL) return false;

    parg = pe->pe_cmdline;
    for (parg = pe->pe_cmdline; *parg != NULL; parg++)
    {
        char **carg = parg;
        char *varg;
        va_list va;

        va_start(va, pe);
        while ((varg = va_arg(va, char *)) != NULL)
        {
            if (*carg == NULL) break;

            if (strcmp(*carg, varg) != 0) break;

            carg++;
        }
        va_end(va);

        /* All arguments matched */
        if (varg == NULL) return true;
        /* End of arguments reached */
        if (*carg == NULL) return false;
    }

    return false;
}

/*
 * ===========================================================================
 *  File utilities
 * ===========================================================================
 */

/**
 * Return true if string @p needle is found in file @p haystack.
 */
bool find_in_file(const char *haystack, const char *needle)
{
    bool retval = false;
    FILE *f = NULL;
    char *buf = NULL;

    /* Read the whole file into memory */
    f = fopen(haystack, "r");
    if (f == NULL)
    {
        PR("Error reading file: %s\n", haystack);
        return false;
    }

    size_t bufmem = getpagesize();
    size_t bufsz = 0;
    size_t nrd;

    buf = malloc(bufmem);
    if (buf == NULL)
    {
        PR("Error allocating memory for find_in_file() buffer.");
        goto exit;
    }

    while ((nrd = fread(buf + bufsz, 1, bufmem - bufsz, f)) > 0)
    {
        bufsz += nrd;

        if (bufsz >= bufmem)
        {
            bufmem <<= 1;

            /* Add +1 for the ending \0 terminator */
            buf = realloc(buf, bufmem + 1);
            if (buf == NULL)
            {
                PR("Error reallocint buffer for find_in_file().");
                goto exit;
            }
        }
    }

    buf[bufsz] = '\0';

    if (strstr(buf, needle) != NULL)
    {
        PR("String '%s' was found in %s.", needle, haystack);
        retval = true;
    }
    else
    {
        PR("String '%s' was NOT found in %s.", needle, haystack);
    }

exit:
    if (f != NULL) fclose(f);
    if (buf != NULL) free(buf);

    return retval;
}

bool __pr(void *ctx, enum execsh_io type, const char *msg)
{
    (void)ctx;

    PR("%c %s", type == EXECSH_IO_STDOUT ? '>' : '|', msg);

    return true;
}

#define execpr(script, ...) execsh_fn_a(__pr, NULL, script, C_VPACK(__VA_ARGS__))

/*
 * Find the udhcpc instance for interface @p ifname
 */
procfs_entry_t *procfs_find_dhcpc(char *ifname)
{
    procfs_t pr;
    procfs_entry_t *pe;

    procfs_entry_t *retval = NULL;

    if (!procfs_open(&pr)) return NULL;

    while ((pe = procfs_read(&pr)) != NULL)
    {
        if (strstr(pe->pe_name, "udhcpc") == NULL) continue;
        if (pe->pe_cmdline == NULL) continue;

        if (!procfs_entry_has_args(pe, "-i", ifname, NULL)) continue;

        break;
    }

    if (pe != NULL)
    {
        /* Found it */
        retval = procfs_entry_getpid(pe->pe_pid);
    }

    procfs_close(&pr);

    return retval;
}

/* Find the DNSMASQ instance */
procfs_entry_t *procfs_find_dnsmasq(void)
{
    procfs_t pr;
    procfs_entry_t *pe;

    procfs_entry_t *retval = NULL;

    if (!procfs_open(&pr)) return NULL;

    while ((pe = procfs_read(&pr)) != NULL)
    {
        if (strstr(pe->pe_name, "dnsmasq") == NULL) continue;
        if (pe->pe_cmdline == NULL) continue;

        if (!procfs_entry_has_args(pe, "--keep-in-foreground", NULL)) continue;

        break;
    }

    if (pe != NULL)
    {
        /* Found it */
        retval = procfs_entry_getpid(pe->pe_pid);
    }

    procfs_close(&pr);

    return retval;
}

/*
 * ===========================================================================
 *  Argument parsing
 * ===========================================================================
 */
bool parse_opts(int argc, char *argv[])
{
    int o;

    static struct option opts[] =
    {
        {   "verbose",      no_argument,    NULL,   'v' },
        {   NULL,           0,              NULL,   0   }
    };

    while ((o = getopt_long(argc, argv, "v", opts, NULL)) != -1)
    {
        switch (o)
        {
            case 'v':
                opt_verbose = 1;
                break;

            default:
                //fprintf(stderr, "Invalid option: %s\n", argv[optind - 1]);
                return false;
        }
    }

    return true;
}

