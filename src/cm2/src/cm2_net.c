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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>

#include "ev.h"

#include "log.h"
#include "schema.h"
#include "target.h"
#include "cm2.h"

/* ev_child must be the first element of the structure */
typedef struct {
    ev_child cw;
    char     if_name[128];
} dhcp_dryrun_t;

int cm2_ovs_insert_port_into_bridge(char *bridge, char *port, int flag_add)
{
    char *op_add = "add-port";
    char *op_del = "del-port";
    char *op_and = "&&";
    char *op_or  = "||";
    char command[128];
    char *op_log;
    char *op;

    if (flag_add) {
        op = op_add;
        op_log = op_or;
    } else {
        op = op_del;
        op_log = op_and;
    }

    LOGI("OVS bridge: %s port = %s bridge = %s", op, port, bridge);

    /* add/delete it to/from OVS bridge */
    sprintf(command, "ovs-vsctl list-ifaces %s | grep %s %s ovs-vsctl %s %s %s",
            bridge, port, op_log, op, bridge, port);

    LOGD("%s: Command: %s", __func__, command);

    return target_device_execute(command);
}

/**
 * Return the PID of the udhcpc client serving on interface @p ifname
 */
static int cm2_util_get_dhcpc_pid(char *ifname)
{
    char pid_file[256];
    int  pid;
    FILE *f;
    int  rc;

    tsnprintf(pid_file, sizeof(pid_file), "/var/run/udhcpc-%s.pid", ifname);

    f = fopen(pid_file, "r");
    if (f == NULL)
        return 0;

    rc = fscanf(f, "%d", &pid);
    fclose(f);

    /* We should read exactly 1 element */
    if (rc != 1)
        return 0;

    if (kill(pid, 0) != 0)
        return 0;

    return pid;
}

static void cm2_dhcpc_dryrun_cb(struct ev_loop *loop, ev_child *w, int revents)
{
    struct schema_Connection_Manager_Uplink con;
    dhcp_dryrun_t                           *dhcp_dryrun;
    bool                                    status;
    int                                     ret;

    dhcp_dryrun = (dhcp_dryrun_t *) w;

    ev_child_stop (loop, w);
    if (WIFEXITED(w->rstatus) && WEXITSTATUS(w->rstatus) == 0)
        status = true;
    else
        status = false;

    if (WIFEXITED(w->rstatus))
        LOGD("%s: %s: rstatus = %d", __func__,
             dhcp_dryrun->if_name, WEXITSTATUS(w->rstatus));

    LOGI("%s: dryrun state: %d", dhcp_dryrun->if_name, w->rstatus);

    ret = cm2_ovsdb_connection_get_connection_by_ifname(dhcp_dryrun->if_name, &con);
    if (!ret) {
        LOGD("%s: interface %s does not exist", __func__, dhcp_dryrun->if_name);
        free(dhcp_dryrun);
        return;
    }

    ret = cm2_ovsdb_connection_update_L3_state(dhcp_dryrun->if_name, status);
    if (!ret)
        LOGW("%s: %s: Update L3 state failed status = %d ret = %d",
             __func__, dhcp_dryrun->if_name, status, ret);

    if (!status && con.has_L2)
            cm2_dhcpc_start_dryrun(dhcp_dryrun->if_name, true);

    free(dhcp_dryrun);
}

void cm2_dhcpc_start_dryrun(char* ifname, bool background)
{
    char pidfile[256];
    char udhcpc_s_option[256];
    pid_t pid;
    char pidname[512];
    char n_param[3];

    LOGN("%s: Trigger dryrun, background = %d", ifname, background);

    STRSCPY(pidname, "cmdryrun-");
    STRLCAT(pidname, ifname);

    pid = cm2_util_get_dhcpc_pid(pidname);
    if (pid > 0)
    {
        LOGI("%s: DHCP client already running", ifname);
        return;
    }

    tsnprintf(pidfile, sizeof(pidfile), "/var/run/udhcpc-%s.pid", pidname);
    snprintf(udhcpc_s_option, sizeof(udhcpc_s_option),
             "/usr/plume/bin/udhcpc-dryrun.sh");

    if (background)
        STRSCPY(n_param, "");
    else
        STRSCPY(n_param, "-n");

    char *argv_dry_run[] = {
        "/sbin/udhcpc",
        "-p", pidfile,
        n_param,
        "-t", "6",
        "-T", "1",
        "-A", "2",
        "-f",
        "-i", ifname,
        "-s", udhcpc_s_option,
        "-S",
        "-Q",
        "-q",
        NULL
    };

    pid = fork();
    if (pid == 0) {
        execv(argv_dry_run[0], argv_dry_run);
        LOGW("%s: %s: failed to exec dry dhcp: %d (%s)",
             __func__, ifname, errno, strerror(errno));
        exit(1);
    } else {
        dhcp_dryrun_t *dhcp_dryrun = (dhcp_dryrun_t *) malloc(sizeof(dhcp_dryrun_t));

        memset(dhcp_dryrun, 0, sizeof(dhcp_dryrun_t));
        STRSCPY(dhcp_dryrun->if_name, ifname);

        ev_child_init (&dhcp_dryrun->cw, cm2_dhcpc_dryrun_cb, pid, 0);
        ev_child_start (EV_DEFAULT, &dhcp_dryrun->cw);
    }
}

void cm2_dhcpc_stop_dryrun(char *ifname)
{
    pid_t pid;
    char  pidname[512];

    STRSCPY(pidname, "cmdryrun-");
    STRLCAT(pidname, ifname);

    pid = cm2_util_get_dhcpc_pid(pidname);
    if (!pid) {
        LOGI("%s: DHCP client not running", ifname);
        return;
    }

    LOGI("%s: pid: %d pid_name: %s", ifname, pid, pidname);

    if (kill(pid, SIGKILL) < 0) {
        LOGW("%s: %s: failed to send kill signal: %d (%s)",
             __func__, ifname, errno, strerror(errno));
    }
}
