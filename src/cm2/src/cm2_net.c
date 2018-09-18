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
    int  rc;

    if (flag_add) {
        op = op_add;
        op_log = op_or;
    } else {
        op = op_del;
        op_log = op_and;
    }

    LOGN("%s %s state = %s", __func__, port, op);

    /* add/delete it to/from OVS bridge */
    sprintf(command, "ovs-vsctl port-to-br %s | grep %s %s ovs-vsctl %s %s %s",
            port, bridge, op_log, op, bridge, port);
    rc = target_device_execute(command);

    return rc;
}

/**
 * Return the PID of the udhcpc client serving on interface @p ifname
 */
static int os_nif_dhcpc_pid(char *ifname)
{
    char pid_file[256];
    FILE *f;
    int pid;
    int rc;

    tsnprintf(pid_file, sizeof(pid_file), "/var/run/udhcpc-%s.pid", ifname);

    f = fopen(pid_file, "r");
    if (f == NULL) return 0;

    rc = fscanf(f, "%d", &pid);
    fclose(f);

    /* We should read exactly 1 element */
    if (rc != 1)
    {
        return 0;
    }

    if (kill(pid, 0) != 0)
    {
        return 0;
    }

    return pid;
}

static void cm2_dhcpc_dryrun_cb(EV_P_ ev_child *w, int revents)
{
    dhcp_dryrun_t *dhcp_dryrun = (dhcp_dryrun_t *) w;
    bool status;

    ev_child_stop (EV_A_ w);
    if (WIFEXITED(w->rstatus) && WEXITSTATUS(w->rstatus) == 0)
        status = true;
    else
        status = false;

    if (WIFEXITED(w->rstatus))
        LOGD("%s: %s: rstatus = %d", __func__,
             dhcp_dryrun->if_name, WEXITSTATUS(w->rstatus));

    LOGN("dryrun %s %d", dhcp_dryrun->if_name, w->rstatus);

    cm2_ovsdb_connection_update_L3_state(dhcp_dryrun->if_name, status);

    if (!status) {
        struct schema_Connection_Manager_Uplink con;
        int ret;

        ret = cm2_ovsdb_connection_get_connection_by_ifname(dhcp_dryrun->if_name, &con);
        if (!ret)
            LOGI("%s interface does not exist", __func__);
        else if (con.has_L2)
            cm2_dhcpc_dryrun(dhcp_dryrun->if_name, true);
    }

    free(dhcp_dryrun);
}

void cm2_dhcpc_dryrun(char* ifname, bool background)
{
    char pidfile[256];
    char udhcpc_s_option[256];
    pid_t pid;
    char pidname[512];
    char n_param[3];

    LOGN("Trigger dryrun %s background = %d", ifname, background);

    STRSCPY(pidname, "cmdryrun-");
    STRLCAT(pidname, ifname);

    pid = os_nif_dhcpc_pid(pidname);
    if (pid > 0)
    {
        LOGI("DHCP client already running::ifname=%s", ifname);
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
        ev_child_start (EV_DEFAULT_ &dhcp_dryrun->cw);
    }
}
