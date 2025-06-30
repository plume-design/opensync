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

#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <ev.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>

#include "os.h"
#include "log.h"
#include "kconfig.h"
#include "monitor.h"
#include "statem.h"
#include "os_proc.h"
#include "target.h"
#include "ds_tree.h"
#include "ovsdb_table.h"
#include "memutil.h"
#include "os_backtrace.h"
#include "util.h"
#include "os.h"

#include "dm.h"

struct dm_chkmem
{
    int pid;
    int vmrss;
    int vmpss;
    int vmsize;
};

static void proc_get_mem_usage(struct dm_chkmem *mu)
{
    char fname[128] = {};
    char line[1024] = {};

    SPRINTF(fname, "/proc/%d/status", mu->pid);

    FILE *file = fopen(fname, "r");
    if (file)
    {
        while (fgets(line, sizeof(line), file))
        {
            if (strstr(line, "VmRSS:"))
            {
                mu->vmrss = atoi(line + strlen("VmRSS: "));
            }
            else if (strstr(line, "VmSize:"))
            {
                mu->vmsize = atoi(line + strlen("VmSize: "));
            }
        }

        fclose(file);
    }
}

int chkmem_check_pss(int pid, char *pname, int memmax, int memmax_cnt, int *highest, int *cnt)
{
    struct dm_chkmem mu;

    mu.pid = pid;
    if (os_proc_get_pss(mu.pid, (uint32_t *)&(mu.vmpss)) != 0) return -1;
    proc_get_mem_usage(&mu);
    LOG(DEBUG,
        "Process %-10s (pid %5d) memory usage: real mem (rss %6d) (pss %6d), virt mem %6d,  memmax limit: %6d kB, cnt "
        "limit %6d",
        pname,
        mu.pid,
        mu.vmrss,
        mu.vmpss,
        mu.vmsize,
        memmax,
        memmax_cnt);
    if ((mu.vmpss > memmax) && (memmax != -1))
    {
        (*cnt)++;
        if (*cnt < memmax_cnt)
        {
            LOG(ERR,
                "Maximum process memory limit exceeded %d times: %s (%5d), pss: %6d kB, limit: %6d kB",
                *cnt,
                pname,
                mu.pid,
                mu.vmpss,
                memmax);
        }
        else
        {
            LOG(ERR,
                "Maximum process memory limit exceeded %d times, aborting the process: %s (%5d), pss: %6d kB, limit: "
                "%6d kB",
                *cnt,
                pname,
                mu.pid,
                mu.vmpss,
                memmax);
            sig_crash_report_mem_max(mu.pid, memmax);
            sleep(1);
            if (kill(pid, SIGABRT) == -1)
            {
                LOG(ERR, "Cannot abort the process %s (%d) ", pname, mu.pid);
            }
            *cnt = 0;
        }
    }
    else if ((mu.vmpss > 9 * memmax / 10) && (memmax != -1)) /* reached 90% of the limit */
    {
        if ((highest != NULL) && (mu.vmpss > (*highest + 100)))
        {
            *highest = mu.vmpss;
            LOG(INFO,
                "High process memory usage detected - process: %-10s (pid %5d)  real mem (rss %6d) (pss %6d), "
                "virt mem %6d, memmax limit: %6d kB, pss_high %6d kB",
                pname,
                mu.pid,
                mu.vmrss,
                mu.vmpss,
                mu.vmsize,
                memmax,
                *highest);
        }
    }
    else
        *cnt = 0;

    return 0;
}
