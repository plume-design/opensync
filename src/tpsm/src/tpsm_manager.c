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

#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ds_tree.h"
#include "kconfig.h"
#include "log.h"
#include "memutil.h"
#include "monitor.h"
#include "os.h"
#include "os_proc.h"
#include "ovsdb_table.h"
#include "statem.h"
#include "target.h"

#include "tpsm.h"

/* not really max FD, but we won't go further than
 * this number in closefrom() surrogate implementation */
#define TPSM_MAX_FD (1024)

#define IGNORE_SIGMASK ((1 << SIGTERM) | (1 << SIGKILL) | (1 << SIGUSR1) | (1 << SIGUSR2) | (1 << SIGINT))

#define TPSM_OUT_FAST (5)
#define TPSM_OUT_SLOW (60)

struct tpsm_manager
{
    char tpsm_name[64];            /* Manager name */
    char tpsm_path[C_MAXPATH_LEN]; /* Manager path (can be relative or full) */
    bool tpsm_plan_b;              /* Requires Plan-B on crash */
    bool tpsm_restart_always;      /* Always restart */
    int tpsm_restart_delay;        /* Restart delay in ms */
    ds_tree_node_t tpsm_tnode;     /* Linked list node */
    pid_t tpsm_pid;                /* Manager process ID or <0 if not started */
    bool tpsm_enable;              /* True if enabled */
    ev_child tpsm_child_watcher;   /* Child event watcher */
    ev_timer tpsm_restart_timer;   /* Restart timer */
};

#define TPSM_MANAGER_INIT \
    (struct tpsm_manager) \
    { \
        .tpsm_pid = -1, .tpsm_enable = true, \
    }

/**
 * Global list of registered managers
 */
ds_tree_t tpsm_manager_list = DS_TREE_INIT(ds_str_cmp, struct tpsm_manager, tpsm_tnode);

bool pid_dir(void)
{
    bool isdir = false;
    int mdr = 0; /* mkdir result */

    /* try to create folder for managers PIDs. In case dir exists, this is ok */
    mdr = mkdir(CONFIG_TPSM_PID_PATH, 0x0777);

    if (mdr == 0 || (mdr == -1 && errno == EEXIST))
    {
        isdir = true;
    }

    LOG(DEBUG, "pid_dir creation  isdir=%s", isdir ? "true" : "false");
    return isdir;
}
