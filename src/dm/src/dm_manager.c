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

#include "os.h"
#include "log.h"
#include "kconfig.h"
#include "monitor.h"
#include "statem.h"
#include "os_proc.h"
#include "target.h"
#include "ds_tree.h"
#include "ovsdb_table.h"

#include "dm.h"

/* not really max FD, but we won't go further than
 * this number in closefrom() surrogate implementation */
#define DM_MAX_FD       (1024)

#define IGNORE_SIGMASK ((1 << SIGTERM) | (1 << SIGKILL) | (1 << SIGUSR1) | \
                        (1 << SIGUSR2) | (1 << SIGINT))

#define TM_OUT_FAST     (5)
#define TM_OUT_SLOW     (60)

struct dm_manager
{
    char                dm_name[64];                /* Manager name */
    char                dm_path[C_MAXPATH_LEN];     /* Manager path (can be relative or full) */
    bool                dm_plan_b;                  /* Requires Plan-B on crash */
    bool                dm_restart_always;          /* Always restart */
    int                 dm_restart_delay;           /* Restart delay in ms */
    ds_tree_node_t      dm_tnode;                   /* Linked list node */
    pid_t               dm_pid;                     /* Manager process ID or <0 if not started */
    bool                dm_enable;                  /* True if enabled */
    ev_child            dm_child_watcher;           /* Child event watcher */
    ev_timer            dm_restart_timer;           /* Restart timer */
};

#define DM_MANAGER_INIT (struct dm_manager)                         \
{                                                                   \
    .dm_pid = -1,                                                   \
    .dm_enable = true,                                              \
}

STATE_MACHINE_USE;

/*
 * OVSDB table for manager list
 */
static ovsdb_table_t table_Node_Services;

/**
 * Global list of registered managers
 */
ds_tree_t dm_manager_list = DS_TREE_INIT(ds_str_cmp, struct dm_manager, dm_tnode);

/*
 * List of valid characters for the manager name
 */
static char dm_name_valid[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    "/_";

/* prototypes  */
static bool dm_manager_pid_set(const char *pid_file, pid_t pid);
static bool dm_manager_pid_file(struct dm_manager *dm, char *out, size_t outsz);
static void dm_manager_child_fn(struct ev_loop *loop, ev_child *w, int revents);

static bool dm_manager_update(
        const char *name,
        bool enable,
        bool plan_b,
        bool always_restart,
        int restart_timer);

static bool dm_manager_start_all(void);
static void dm_manager_kill(struct dm_manager *dm);
static bool dm_manager_start(struct dm_manager *dm);
static bool dm_manager_stop(struct dm_manager *dm);
static bool dm_manager_exec(struct dm_manager *dm);
static bool ignore_signal(int status);

void callback_Node_Services(
        ovsdb_update_monitor_t *mon,
        struct schema_Node_Services *old,
        struct schema_Node_Services *new);

void dm_manager_node_service_update(const char *service, bool enabled, bool error);

static const char *dm_manager_basename(const char *name);

bool pid_dir(void)
{
    bool isdir = false;
    int mdr = 0;    /* mkdir result */

    /* try to create folder for managers PIDs. In case dir exists, this is ok */
    mdr = mkdir(CONFIG_DM_PID_PATH, 0x0777);

    if (mdr == 0 || (mdr == -1 && errno == EEXIST))
    {
        isdir = true;
    }

    LOG(DEBUG, "pid_dir creation  isdir=%s", isdir ? "true" : "false");
    return isdir;
}

bool init_managers()
{
    int i;

    /* terminate all running managers */
    if (!pid_dir())
    {
        LOG(ERR, "Can't create PID folder: path=%s", CONFIG_DM_PID_PATH);
    }

    /*
     * Legacy code: Convert the TARGET API manager list to a dynamic
     * list.
     */
    for (i = 0; i < (int)target_managers_num; i++)
    {
        struct dm_manager *dm = calloc(1, sizeof(*dm));

        /*
         * Add manager to global list of managers
         */
        LOG(INFO, "Adding legacy manager: %s", target_managers_config[i].name);

        dm_manager_register(
                target_managers_config[i].name,
                target_managers_config[i].needs_plan_b,
                target_managers_config[i].always_restart,
                target_managers_config[i].restart_delay);
    }

    if (!dm_manager_start_all())
    {
        LOG(ERR, "Failed to start at least one manager.");
        return false;
    }

    OVSDB_TABLE_INIT(Node_Services, service);
    /*
     * Do not listen to _version and status fields
     */
    char *filter[] =
    {
        "-",
        "_version",
        SCHEMA_COLUMN(Node_Services, status),
        NULL
    };

    OVSDB_TABLE_MONITOR_F(Node_Services, filter);

    return true;
}

bool act_init_managers (void)
{
    bool retval = false;

    retval = init_managers();

    if (true == retval)
    {
#if defined(USE_SPEED_TEST) || defined(CONFIG_SPEEDTEST)
        /* start monitoring speedtest config table */
        dm_st_monitor();
#endif
    }

    STATE_TRANSIT(STATE_IDLE);

    return retval;
}

/**
 * Register a manager
 */
bool dm_manager_register(
        const char *path,
        bool plan_b,
        bool restart,
        int restart_delay)
{
    const char *name;
    struct dm_manager *dm;

    /* Infer the manager name from the manager path */
    name = dm_manager_basename(path);

    dm = ds_tree_find(&dm_manager_list, (char *)name);
    if (dm != NULL)
    {
        LOG(ERR, "Manager %s already registered.", name);
        return false;
    }

    /* Check if the child process contains only valid characters */
    if (strspn(path, dm_name_valid) != strlen(path))
    {
        LOG(ERR, "Manager path contains invalid characters: %s", path);
        return false;
    }

    dm = calloc(1, sizeof(*dm));
    *dm = DM_MANAGER_INIT;

    STRSCPY(dm->dm_name, name);
    STRSCPY(dm->dm_path, path);
    dm->dm_plan_b = plan_b;
    dm->dm_restart_always = restart;
    dm->dm_restart_delay = restart_delay;

    ds_tree_insert(&dm_manager_list, dm, (char *)dm->dm_name);

    LOG(INFO, "Registered manager: %s", dm->dm_name);

    /* Clean up old instances of this manager */
    dm_manager_kill(dm);


    return true;
}

bool dm_manager_update(
        const char *path,
        bool enable,
        bool plan_b,
        bool restart_always,
        int restart_delay)
{
   const  char *name;
    struct dm_manager *dm;

    name = dm_manager_basename(path);

    dm = ds_tree_find(&dm_manager_list, (char *)name);
    if (dm == NULL)
    {
        LOG(ERR, "Unable to update manager %s. Not found.", name);
        return false;
    }

    dm->dm_plan_b = plan_b;
    dm->dm_restart_always = restart_always;
    dm->dm_restart_delay = restart_delay;
    dm->dm_enable = enable;

    if (enable)
    {
        return dm_manager_start(dm);
    }
    else
    {
        return dm_manager_stop(dm);
    }

    return false;
}


/*
 * ===========================================================================
 *  Private functions
 * ===========================================================================
 */

/**
 * Start all currently registered (and stopped) managers
 *
 * Return false if at least one of the managers failed to start
 */
bool dm_manager_start_all(void)
{
    struct dm_manager *dm;

    bool retval = true;

    ds_tree_foreach(&dm_manager_list, dm)
    {
        retval &= dm_manager_start(dm);
    }

    return retval;
}

/*
 * Terminate a stale instances of the manager. This should be executed at
 * startup to possibly clear all instances that somehow weren't killed from a
 * previous run. This function looks up the manager processes by their names.
 */
void dm_manager_kill(struct dm_manager *dm)
{
    pid_t pid2kill;

    pid2kill = os_name_to_pid(dm->dm_name);
    if (pid2kill < 0)
    {
        LOG(NOTICE, "Manager: %s - no instance", dm->dm_name);
        return;
    }

    /* try to terminate managers if it is running */
    LOG(NOTICE, "Killing process: name=%s pid=%d", dm->dm_name, pid2kill);
    mon_process_terminate(pid2kill);

    if (dm->dm_plan_b)
    {
        LOG(NOTICE, "Plan B required by %s pid:%d",
                    dm->dm_name,
                    (int)pid2kill);

        target_managers_restart();

        LOG(ERR, "Restart manager was not executed!");
    }
}

/**
 * Start a manager:
 *  - Fork/exec the manager process
 *  - Create a PID file with the manager PID
 *  - Start a child watcher
 */
bool dm_manager_start(struct dm_manager *dm)
{
    char ppid[C_MAXPATH_LEN];

    /* Manager is disabled, nothing to do */
    if (!dm->dm_enable) return true;

    if (dm->dm_pid >= 0)
    {
        LOG(DEBUG, "Manager %s already started.", dm->dm_name);
        return true;
    }

    /*
     * Calculate the PID path
     */
    if (!dm_manager_pid_file(dm, ppid, sizeof(ppid)))
    {
        LOG(ERR, "Error calculating PID file path: %s", dm->dm_name);
        return false;
    }

    /*
     * Fork the child
     */
    if (!dm_manager_exec(dm))
    {
        return false;
    }

    /* Save pid for all nice and clean process termination */
    if (!dm_manager_pid_set(ppid, dm->dm_pid))
    {
        LOG(ERR, "Error storing pid for manager  name=%s|pid=%d",
                 dm->dm_name,
                 (int)dm->dm_pid);
    }

    LOG(NOTICE, "Started process name=%s pid=%d",
                dm->dm_name,
                (int)dm->dm_pid);

    /* Start monitoring this process */
    ev_child_init(
            &dm->dm_child_watcher,
            dm_manager_child_fn,
            dm->dm_pid,
            0);

    ev_child_start(EV_DEFAULT, &dm->dm_child_watcher);

    return true;
}

bool dm_manager_stop(struct dm_manager *dm)
{
    char ppid[C_MAXPATH_LEN];

    if (dm->dm_pid < 0) return true;

    /* Stop the process watcher */
    ev_child_stop(EV_DEFAULT, &dm->dm_child_watcher);

    LOG(INFO, "Stopping process: name=%s pid=%d", dm->dm_name, dm->dm_pid);

    if (dm->dm_plan_b)
    {
        LOG(NOTICE, "Warning: Stopping PLAN_B manager: %s", dm->dm_name);
    }

    mon_process_terminate(dm->dm_pid);
    dm->dm_pid = -1;

    if (!dm_manager_pid_file(dm, ppid, sizeof(ppid)))
    {
        LOG(ERR, "Error getting PID file name (remove): %s", ppid);
        return false;
    }

    if (unlink(ppid) != 0)
    {
        LOG(ERR, "Error removing PID file: %s", ppid);
        return false;
    }

    return true;
}


/**
 * Fork and execute on of the managers (create child process)
 * Errors will be handled in calling function(s)
 **/
bool dm_manager_exec(struct dm_manager *dm)
{
    pid_t cpid;
    int ifd;
    char pexe[C_MAXPATH_LEN];

    /*
     * Find out the path to the manager executable:
     * - if the path contains slashes, assume it's a full path
     * - if the path doesn't contain slashes, look it up in the
     *   CONFIG_DM_MANAGER_PATH folder list
     */
    if (strchr(dm->dm_path, '/') != NULL)
    {
        STRSCPY(pexe, dm->dm_path);
    }
    else
    {
        /* Lookup the manager in the manager path */
        char *ppath;
        char *dir;

        char path[] = CONFIG_DM_MANAGER_PATH;

        ppath = path;
        while ((dir = strsep(&ppath, ";")) != NULL)
        {
            int rc;

            rc = snprintf(pexe, sizeof(pexe), "%s/%s", dir, dm->dm_path);
            if (rc >= (int)sizeof(pexe))
            {
                LOG(ERR, "Path to manager truncated: %s/%s", dir, dm->dm_path);
                return false;
            }

            if (access(pexe, X_OK) == 0)
            {
                break;
            }
        }

        /* Not found */
        if (dir == NULL)
        {
            LOG(ERR, "Unable to find manager %s in path.", dm->dm_path);
            return false;
        }
    }

    if (access(pexe, X_OK) != 0)
    {
        LOG(ERR, "File not found or not executable: %s.", dm->dm_path);
        return false;
    }

    /*
     * Fork the child process
     */
    cpid = fork();
    if (cpid < 0)
    {
        LOG(ERR, "Fork error when executing %s.", dm->dm_name);
        return cpid;
    }
    else if (cpid > 0)
    {
        dm->dm_pid = cpid;
        return true;
    }

    /*
     * we are in child process
     */

    /* close all open fds in parent process */
    /* This is usually done by closefrom(), uClibc has not such */
    /* function - use this surrogate implementation */
    for (ifd = 3; ifd < DM_MAX_FD; ifd++)
    {
        close(ifd);
    }

    execl(pexe, pexe, NULL);

    _exit(EXIT_FAILURE);

    return false;
}


/**
 * Timer callback used to start crashed process with some delay after the crash
 **/
void dm_manager_restart_fn(struct ev_loop *loop, ev_timer *w, int events)
{
    (void)events;
    (void)loop;

    struct dm_manager *dm = CONTAINER_OF(w, struct dm_manager, dm_restart_timer);

    if (dm->dm_plan_b)
    {
        LOG(NOTICE, "Manager '%s' restart requires PLAN_B.", dm->dm_name);
        target_managers_restart();
    }

    /* restart failed process */
    dm_manager_start(dm);
}


void dm_manager_restart(struct dm_manager *dm, int delay)
{
    ev_timer_init(
            &dm->dm_restart_timer,
            dm_manager_restart_fn,
            (double)delay,
            0);

    ev_timer_start(EV_DEFAULT, &dm->dm_restart_timer);
}

#if 0
/*
 * Read the PID from the PID file
 */
bool dm_manager_pid_get(const char *pid_file, pid_t *pid)
{
    FILE *fp;
    char buf[16];

    bool retval = false;

    fp = fopen(pid_file, "r");
    if (fp == NULL)
    {
        goto error;
    }

    if (fgets(buf, sizeof(buf), fp) == NULL)
    {
        goto error;
    }

    *pid = strtol(buf, NULL, 0);
    /* Conversion error, return an error */
    if (*pid <= 0)
    {
        goto error;
    }

    retval = true;

error:
    if (fp != NULL) fclose(fp);

    return retval;
}
#endif

/*
 * Write the PID to PID file
 */
bool dm_manager_pid_set(const char *pid_file, pid_t pid)
{
    FILE *fp;

    fp = fopen(pid_file, "w+");
    if (fp == NULL)
    {
        LOG(DEBUG, "Error opening PID file: %s", pid_file);
        return false;
    }

    fprintf(fp, "%d\n", (int)pid);

    fclose(fp);

    return true;
}


/**
 * Return pid file name for given manager name
 */
bool dm_manager_pid_file(struct dm_manager *dm, char *out, size_t outsz)
{
    int rc;

    rc = snprintf(out, outsz, "%s/%s.pid",
            CONFIG_DM_PID_PATH,
            dm->dm_name);

    /* String was truncated? */
    if (rc >= (int)outsz)
    {
        LOG(ERR, "Path name to PID file truncated: %s", dm->dm_name);
        return false;
    }

    return true;
}

/**
 * Child signals callback
 */
void dm_manager_child_fn(struct ev_loop *loop, ev_child *w, int revents)
{
    (void)revents;

    struct dm_manager *dm = CONTAINER_OF(w, struct dm_manager, dm_child_watcher);
    int delay = dm->dm_restart_delay;

    /*
     * For backwards compatibility reasons,
     * a restart_delay value of 0 maps to TM_OUT_FAST
     * and restart_delay value of -1 maps to 0
     */
    if (delay == 0)
    {
        delay = TM_OUT_FAST;
    }
    else if (delay == -1)
    {
        delay = 0;
    }

    LOG(INFO, "Manager status update: name=%s pid=%d status=%d signal=%d",
              dm->dm_name,
              w->rpid,
              w->rstatus,
              WTERMSIG(w->rstatus));

#ifdef WIFCONTINUED
    if (WIFCONTINUED(w->rstatus))
    {
        LOG(INFO, "Manager continued: name=%s pid=%d", dm->dm_name, w->rpid);
        return;
    }
#endif
#ifdef WIFSTOPPED
    if (WIFSTOPPED(w->rstatus))
    {
        LOG(INFO, "Manager stopped: name=%s pid=%d", dm->dm_name, w->rpid);
        return;
    }
#endif

    /* Process exited, flag the manager as not active */
    ev_child_stop(loop, w);
    dm->dm_pid = -1;

    if (WIFEXITED(w->rstatus))
    {
        LOG(INFO, "Manager exited: name=%s pid=%d rc=%d",
                dm->dm_name,
                w->rpid,
                WEXITSTATUS(w->rstatus));

        /* Fast restart processes that exited normally */
        if (WEXITSTATUS(w->rstatus) == 0)
        {
            LOG(NOTICE, "Restarting %s in %d seconds.",
                        dm->dm_name, TM_OUT_FAST);

            /* on exit try to restart it relatively fast */
            dm_manager_restart(dm, TM_OUT_FAST);
            return;
        }
        else
        {
            /* Slow restart if exited with error code */
            LOG(INFO, "Manager failed to start, restart in %d seconds: name=%s pid=%d",
                      TM_OUT_SLOW, dm->dm_name, w->rpid);
            dm_manager_restart(dm, TM_OUT_SLOW);
            return;
        }
    }

    /* check if the on of the manager processes had crashed     */
    /* ignore managers stop due to user actions                 */
    if (!ignore_signal(w->rstatus) || dm->dm_restart_always)
    {
        LOG(NOTICE, "Manager '%s' terminated, signal: %d, restarting in %d seconds.",
                    dm->dm_name,
                    WTERMSIG(w->rstatus),
                    TM_OUT_FAST);

        dm_manager_restart(dm, TM_OUT_FAST);
        return;
    }

    LOG(NOTICE, "Manager user termination by signal: %d, process=%s pid=%d",
                WTERMSIG(w->rstatus),
                dm->dm_name,
                w->rpid);
}

/**
 * Helper used to check if a signal should be ignored
 */
static bool ignore_signal(int status)
{
    uint32_t termsig;

    /* Bail if the child process was not terminated by a signal */
    if (!WIFSIGNALED(status))
    {
        return true;
    }

    /* Check if the signal reflects a user action */
    if (WTERMSIG(status) <= 31)
    {
        termsig = 1 << WTERMSIG(status);
        return ((termsig & IGNORE_SIGMASK) != 0);
    }

    return false;
}

/*
 * basename() is somewhat tricky as it may modify the input string. We need a
 * much simpler version that only checks for slashes.
 */
const char *dm_manager_basename(const char *name)
{
    char *pname = strrchr(name, '/');
    return (pname != NULL ? ++pname : name);
}

/*
 * ===========================================================================
 *  OVSDB
 * ===========================================================================
 */

void callback_Node_Services(
        ovsdb_update_monitor_t *mon,
        struct schema_Node_Services *old,
        struct schema_Node_Services *new)
{
    (void)mon;

    int ii;

    bool plan_b = false;
    bool enable = false;
    bool restart_always = false;
    bool restart_delay = 0;
    bool retval = false;

    /* Deletions not yet supported */
    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        LOG(ERR, "Manager deletion not supported: %s", old->service);
        return;
    }

    /* Parse other config */
    for (ii = 0; ii < new->other_config_len; ii++)
    {
        if (strcmp(new->other_config_keys[ii], "needs_plan_b") == 0)
        {
            plan_b = strcasecmp(new->other_config[ii], "true") == 0;
        }
        else if (strcmp(new->other_config_keys[ii], "always_restart") == 0)
        {
            restart_always = strcasecmp(new->other_config[ii], "true") == 0;
        }
        else if (strcmp(new->other_config_keys[ii], "restart_delay") == 0)
        {
            restart_delay = atoi(new->other_config[ii]);
        }
    }

    enable = new->enable_exists && new->enable;

    LOG(INFO, "Registering/updating[%d] manager: name=%s enable=%s needs_plan_b=%s always_restart=%s restart_delay=%d",
            old != NULL,
            new->service,
            enable ? "true" : "false",
            plan_b ? "true" : "false",
            restart_always ? "true" : "false",
            restart_delay);

    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        (void)dm_manager_register(new->service, plan_b, restart_always, restart_delay);
    }

    if (!dm_manager_update(new->service, enable, plan_b, restart_always, restart_delay))
    {
        goto error;
    }

    retval = true;

error:
    dm_manager_node_service_update(new->service, enable, !retval);
}

void dm_manager_node_service_update(
        const char *service,
        bool enabled,
        bool error)
{
    struct schema_Node_Services row;
    char *status;

    memset(&row, 0, sizeof(row));

    if (error)
    {
        status = "error";
    }
    else
    {
        status = enabled ? "enabled" : "disabled";
    }

    row.service_exists = true;
    STRSCPY(row.service, service);
    row.status_exists = true;
    STRSCPY(row.status, status);

    char *filter[] =
    {
        "+",
        SCHEMA_COLUMN(Node_Services, status),
        NULL
    };

    LOG(INFO, "Node_Service update: service=%s status=%s", service, status);
    if (!ovsdb_table_update_f(&table_Node_Services, &row, filter))
    {
        LOG(ERR, "Error updating Node_Services status: %s = %s", service, status);
    }
}

