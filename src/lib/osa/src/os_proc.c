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

#include <glob.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>

#include "os_common.h"

#include "log.h"
#include "os.h"
#include "os_proc.h"
#include "os_util.h"
#include "util.h"

/* Return the process name for pid input */
/* note, using fd instead of FILE so it's safe to call from a signal handler */
int32_t os_pid_to_name(pid_t pid, char *proc_name, int32_t len)
{
    char buf[64];

    int fd = -1;
    int32_t rc = -1;
    int l;

    memset(buf, 0x00, sizeof(buf));
    snprintf(buf, sizeof(buf), "/proc/%d/comm", pid);

    fd = open(buf, O_RDONLY);
    if (fd < 0)
        goto func_exit;

    l = read(fd, buf, sizeof(buf) - 1);
    if (l < 0 || l >= (int)sizeof(buf))
        goto func_exit;

    buf[l] = 0;

    // remove newline if present
    if (l > 0 && buf[l-1] == '\n') {
        buf[l-1] = 0;
    }

    strscpy(proc_name, buf, len);
    rc = 0;

func_exit:
    if (fd >= 0) close(fd);

    return rc;
}

/*
 * Return the parent process ID for given pid
 */

static pid_t os_pid_to_ppid(pid_t pid)
{
    char        buf[64];
    char        *k = NULL;
    char        *v = NULL;
    char        *kv = NULL;
    char        *ppid_string = NULL;
    int         l;
    long        lextr;
    pid_t       ppid = -1;
    FILE        *fp = NULL;
    
    snprintf(buf, sizeof(buf), "/proc/%d/status", pid);

    fp = fopen(buf, "r");
    if (NULL == fp)
        goto func_exit;

    while (fgets(buf, sizeof(buf), fp)) {
        // remove newline if present
        l = strlen(buf);
        if (l > 0 && buf[l-1] == '\n') {
            buf[l-1] = 0;
        }

        kv = buf;
        if ((k = strsep(&kv, ":")) &&
            (v = strsep(&kv, ""))) {
            if (!strcmp(k, "PPid")) {
                ppid_string = v;
                break;
            }
        }
    }

    /* convert it to a number */
    if (false == os_strtoul(ppid_string, &lextr, 0)) {
        LOG(ERR, "Extracting pid from %s failed", v);
        goto func_exit;
    } else {
        ppid = (pid_t)lextr;
    }

func_exit:
    if (fp != NULL) fclose(fp);

    return ppid;
}

/*
 * Return pid based on process name
 */

pid_t os_name_to_pid(const char *proc_name)
{
    pid_t       pid = -1;
    long        lextr;
    glob_t      g;
    char        rbuff[1024];
    uint32_t    i;
    FILE        *f;
    int         rc;
    char *      newline;

    /* process name is stored in /proc/pid/comm */
    if (glob("/proc/*/comm", 0, NULL, &g) != 0)
    {
        goto exit;
    }

    for (i = 0; i < g.gl_pathc; i++)
    {

        /* Read the contents of the file */
        if (NULL == (f = fopen(g.gl_pathv[i], "r")))
        {
            /* error opening file, ignore, continue */
            continue;
        }
        else
        {
            /* try to read comm file */
            // use size-1 for zero termination
            rc = fread(rbuff, 1, sizeof(rbuff) - 1, f);
            fclose(f);
            if (rc <= 0) continue;
            // zero terminate
            rbuff[rc] = 0;
            // trim newline
            if (NULL != (newline = strrchr(rbuff, '\n')))
            {
               *newline = '\0';
            }
        }

        /*
         *  Find the match. If there is a match, extract pid
         */
        if (0 == strcmp(rbuff, proc_name))
        {
            /* os_strtol actually checks end string char and similar,
               so we need to prepare pid string in separate buffer
               next few lines are doing just that
            */
            int j = 0;
            char * pidstr = g.gl_pathv[i];

            /* reset read buffer */
            memset(rbuff, 0, sizeof(rbuff));

            /* find begging of pid string */
            while ((*pidstr != '\0') && (!isdigit(*pidstr)))
                pidstr++;

            /* extract pid string */
            while ((*pidstr != '\0') && (isdigit(*pidstr)))
                rbuff[j++] = *pidstr++;

            /* convert it to a number */
            if (false == os_strtoul(rbuff, &lextr, 0))
            {
                LOG(ERR, "Extracting pid from %s failed", g.gl_pathv[i]);
            }
            else
            {
                pid = (pid_t)lextr;
            }

            break; /* for loop */
        }
    }

exit:
    /* Clean up. */
    globfree(&g);

    return pid;
}

/*
 * Return pid based on parent process ID.
 * In case of multiple forked process with the same name, return the PID
 * if its parent PID matches the given ID, i.e. find the direct descendent
 * of the given PPID.
 */

pid_t os_name_to_pid_of_ppid(const char *proc_name, pid_t ppid)
{
    pid_t       pid = -1;
    glob_t      g = {0};
    char        rbuff[1024];
    uint32_t    i;
    FILE        *f;
    int         rc;
    char *      newline;

    /* process name is stored in /proc/pid/comm */
    if (glob("/proc/*/comm", 0, NULL, &g) != 0) {
        goto exit;
    }

    for (i = 0; i < g.gl_pathc; i++) {

        /* Read the contents of the file */
        if (NULL == (f = fopen(g.gl_pathv[i], "r"))) {
            /* error opening file, ignore, continue */
            continue;
        }
        /* try to read comm file */
        // use size-1 for zero termination
        rc = fread(rbuff, 1, sizeof(rbuff) - 1, f);
        fclose(f);
        if (rc <= 0) continue;
        // zero terminate
        rbuff[rc] = 0;
        // trim newline
        if (NULL != (newline = strrchr(rbuff, '\n'))) {
           *newline = '\0';
        }

        /*
         *  Find the match. If there is a match, extract pid
         */
        if (0 == strcmp(rbuff, proc_name)) {
            if (1 == sscanf(g.gl_pathv[i] + strlen("/proc/"), "%d", &pid)) {
                /* Check whether the parent ID of the found process matches */
                if (ppid == os_pid_to_ppid(pid)) {
                    break;
                }
            }
        }
    }

exit:
    /* Clean up. */
    globfree(&g);

    return pid;
}
