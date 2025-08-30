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
 *  Reboot reason and Reboot count implementation
 * ===========================================================================
 */
#include <fcntl.h>
#include <unistd.h>

#include "module.h"
#include "log.h"
#include "osp_reboot.h"
#include "osp_ps.h"
#include "const.h"
#include "util.h"
#include "memutil.h"
#include "evx.h"
#include "json_util.h"
#include "ovsdb_table.h"

#define DM_REBOOT_GUARD_FILE "/var/run/dm_reboot_guard"

/*
 * Generate the PJS structures
 */
#include "dm_reboot_pjs.h"
#include "pjs_gen_h.h"

#include "dm_reboot_pjs.h"
#include "pjs_gen_c.h"

#define DM_REBOOT_STORE "reboot"
#define DM_REBOOT_KEY   "reboot"

MODULE(dm_reboot, dm_reboot_init, dm_reboot_fini)

/*
 * Mapping between the reboot type enum constants to string
 */
static const char * const dm_reboot_type_map[] =
{
    [OSP_REBOOT_CANCEL]         = "CANCEL",
    [OSP_REBOOT_UNKNOWN]        = "UNKNOWN",
    [OSP_REBOOT_COLD_BOOT]      = "COLD_BOOT",
    [OSP_REBOOT_POWER_CYCLE]    = "POWER_CYCLE",
    [OSP_REBOOT_WATCHDOG]       = "WATCHDOG",
    [OSP_REBOOT_CRASH]          = "CRASH",
    [OSP_REBOOT_USER]           = "USER",
    [OSP_REBOOT_DEVICE]         = "DEVICE",
    [OSP_REBOOT_HEALTH_CHECK]   = "HEALTH_CHECK",
    [OSP_REBOOT_UPGRADE]        = "UPGRADE",
    [OSP_REBOOT_THERMAL]        = "THERMAL",
    [OSP_REBOOT_CLOUD]          = "CLOUD",
};

/*
 * Global list of reboot reasons
 */
static struct dm_reboot g_dm_reboot;

/*
 * OVSDB Table Handler: Reboot Status
 */
static ovsdb_table_t table_Reboot_Status;

/*
 * Debouncer for persistent storage updates
 */
ev_debounce dm_reboot_update_ev;

static bool dm_reboot_load(struct dm_reboot *dr);
static bool dm_reboot_store(struct dm_reboot *dr);
static bool dm_reboot_guard(void);
static void dm_reboot_ovsdb_init(void);
static void callback_Reboot_Status(
        ovsdb_update_monitor_t *mon,
        struct schema_Reboot_Status *old,
        struct schema_Reboot_Status *new);
static void dm_reboot_update_fn(struct ev_loop *loop, ev_debounce *w, int revents);
static const char* dm_reboot_type_str(enum osp_reboot_type type);

/**
 * Initialize the reboot module
 */
void dm_reboot_init(void *data)
{
    enum osp_reboot_type type;
    char reason[1024];

    /*
     * Initialize the debouncer, minimum update interval is 300ms, maximum
     * is 2 seconds
     */
    ev_debounce_init2(&dm_reboot_update_ev, dm_reboot_update_fn, 0.3, 2.0);

    if (!osp_unit_reboot_get(&type, reason, sizeof(reason)))
    {
        LOG(ERR, "dm_reboot: Unable to get the reboot reason.");
        return;
    }

    LOG(NOTICE, "Last reboot: [%s] %s", dm_reboot_type_str(type), reason);

    /*
     * Load current list of reboot reasons
     */
    if (!dm_reboot_load(&g_dm_reboot))
    {
        LOG(ERR, "dm_reboot: Unable to access reboot data. Reboot counter functionality will not be available.");
        return;
    }

    LOG(NOTICE, "Boot counter: %d", g_dm_reboot.dr_counter);

    /*
     * Store the reboot reason/counter only once per reboot. When we do it for
     * the first time, create a simple file in /var/run.
     */
    if (!dm_reboot_guard())
    {
        /*
         * Update the reboot status structure
         */
        if (g_dm_reboot.dr_records_len >= ARRAY_LEN(g_dm_reboot.dr_records))
        {
            LOG(WARN, "dm_reboot: Maximum number of reboot records reached, discarding oldest entry.");
            memmove(&g_dm_reboot.dr_records[0],
                    &g_dm_reboot.dr_records[1],
                    sizeof(g_dm_reboot.dr_records) - sizeof(g_dm_reboot.dr_records[0]));
            g_dm_reboot.dr_records_len--;
        }

        /*
         * Append reboot reason
         */
        int drl = g_dm_reboot.dr_records_len;

        g_dm_reboot.dr_counter++;
        g_dm_reboot.dr_records[drl].dr_bootid = g_dm_reboot.dr_counter;

        STRSCPY(g_dm_reboot.dr_records[drl].dr_type, dm_reboot_type_str(type));
        STRSCPY(g_dm_reboot.dr_records[drl].dr_reason, reason);
        g_dm_reboot.dr_records_len = drl + 1;

        if (!dm_reboot_store(&g_dm_reboot))
        {
            LOG(ERR, "dm_reboot: Unable to store reboot status.");
        }
    }

    /* Initialize OVSDB */
    dm_reboot_ovsdb_init();
}

/**
 * Deinitialize the reboot module
 */
void dm_reboot_fini(void *data)
{
    ev_debounce_stop(EV_DEFAULT, &dm_reboot_update_ev);
    LOG(INFO, "dm_reboot: Finishing.");
}

/**
 * Load the current reboot data
 */
bool dm_reboot_load(struct dm_reboot *dr)
{
    pjs_errmsg_t perr;
    ssize_t rstrsz;

    bool retval = false;
    osp_ps_t *ps = NULL;
    json_t *rjson = NULL;
    char *rstr = NULL;

    memset(dr, 0, sizeof(*dr));

    ps = osp_ps_open(DM_REBOOT_STORE, OSP_PS_RDWR | OSP_PS_PRESERVE);
    if (ps == NULL)
    {
        LOG(NOTICE, "dm_reboot: Unable to open \"%s\" store.",
                DM_REBOOT_STORE);
        goto exit;
    }

    /*
     * Load and parse the reboot structure
     */
    rstrsz = osp_ps_get(ps, DM_REBOOT_KEY, NULL, 0);
    if (rstrsz < 0)
    {
        LOG(ERR, "dm_reboot: Error fetching \"%s\" key size.",
                DM_REBOOT_KEY);
        goto exit;
    }
    else if (rstrsz == 0)
    {
        /*
         * The "reboot" record does not exist yet, it may indicate this is
         * the first boot
         */
        retval = true;
        goto exit;
    }

    /* Fetch the "reboot" data */
    rstr = MALLOC((size_t)rstrsz);
    if (osp_ps_get(ps, DM_REBOOT_KEY, rstr, (size_t)rstrsz) != rstrsz)
    {
        LOG(ERR, "dm_reboot: Error retrieving persistent \"%s\" key.",
                DM_REBOOT_KEY);
        goto exit;
    }

    /* Convert it to JSON */
    rjson = json_loads(rstr, 0, NULL);
    if (rjson == NULL)
    {
        LOG(ERR, "dm_reboot: Error parsing JSON: %s", rstr);
        goto exit;
    }

    /* Convert it to C */
    if (!dm_reboot_from_json(dr, rjson, false, perr))
    {
        memset(dr, 0, sizeof(*dr));
        LOG(ERR, "dm_reboot: Error parsing dm_reboot record: %s", perr);
        goto exit;
    }

    retval = true;

exit:
    if (rstr != NULL) FREE(rstr);
    if (rjson != NULL) json_decref(rjson);
    if (ps != NULL) osp_ps_close(ps);

    return retval;
}

/**
 * Store the reboot reason to persistent storage
 */
bool dm_reboot_store(struct dm_reboot *dr)
{
    pjs_errmsg_t perr;
    ssize_t rstrsz;

    bool retval = false;
    osp_ps_t *ps = NULL;
    json_t *rjson = NULL;
    char *rstr = NULL;

    /* Open persistent storage in read-write mode */
    ps = osp_ps_open(DM_REBOOT_STORE, OSP_PS_RDWR | OSP_PS_PRESERVE);
    if (ps == NULL)
    {
        LOG(ERR, "dm_reboot: Error opening \"%s\" persistent store.",
                DM_REBOOT_STORE);
        goto exit;
    }

    /*
     * Convert the reboot status structure to JSON
     */
    rjson = dm_reboot_to_json(dr, perr);
    if (rjson == NULL)
    {
        LOG(ERR, "dm_reboot: Error converting dr_reboot structure to JSON: %s", perr);
        goto exit;
    }

    LOG(DEBUG, "dr_records_len = %d", dr->dr_records_len);
    {
        int ii;
        for (ii = 0; ii < dr->dr_records_len; ii++)
        {
            LOG(DEBUG, "dr_dr_records[%d].dr_type = %s", ii, dr->dr_records[ii].dr_type);
            LOG(DEBUG, "dr_dr_records[%d].dr_reason = %s", ii, dr->dr_records[ii].dr_reason);
        }
    }

    /*
     * Convert the reboot structure to string
     */
    rstr = json_dumps(rjson, JSON_COMPACT);
    if (rstr == NULL)
    {
        LOG(ERR, "dm_reboot: Error converting JSON to string.");
        goto exit;
    }

    /*
     * Store the string representation to persistent storage
     */
    rstrsz = (ssize_t)strlen(rstr) + 1;
    if (osp_ps_set(ps, DM_REBOOT_KEY, rstr, (size_t)rstrsz) < rstrsz)
    {
        LOG(ERR, "dm_reboot: Error storing reboot records: %s", rstr);
        goto exit;
    }

    retval = true;

exit:
    if (rstr != NULL) json_free(rstr);
    if (rjson != NULL) json_decref(rjson);
    if (ps != NULL) osp_ps_close(ps);

    return retval;
}

/**
 * Returns true if the guard file already exists. Otherwise create it, and
 * return false. The guard file is stored in /var/run (tmpfs).
 *
 * The idea is that it would return false only once per boot.
 *
 * @return
 * Return true if the guard file already exists. Return false on error or if
 * the guard file didn't exist, but was created.
 */
bool dm_reboot_guard(void)
{
    int fd;

    if (access(DM_REBOOT_GUARD_FILE, F_OK) == 0) return true;

    fd = open(DM_REBOOT_GUARD_FILE, O_WRONLY | O_TRUNC | O_CREAT, 0600);
    if (fd < 0)
    {
        LOG(ERR, "dm_reboot: Error opening guard file: %s. Reboot mechanics will not be available.",
                DM_REBOOT_GUARD_FILE);
        return false;
    }

    close(fd);

    return false;
}

/**
 * Initialize the DM "reboot" OVSDB interface
 */
void dm_reboot_ovsdb_init(void)
{
    int ri;

    OVSDB_TABLE_INIT_NO_KEY(Reboot_Status);
    OVSDB_TABLE_MONITOR(Reboot_Status, false);

    /*
     * Populate the database with the current reboot status data
     */
    for (ri = 0; ri < g_dm_reboot.dr_records_len; ri++)
    {
        struct schema_Reboot_Status rs = { 0 };

        rs.count = g_dm_reboot.dr_records[ri].dr_bootid;
        STRSCPY(rs.type, g_dm_reboot.dr_records[ri].dr_type);
        STRSCPY(rs.reason, g_dm_reboot.dr_records[ri].dr_reason);
        ovsdb_table_insert(&table_Reboot_Status, &rs);
    }
}

static void callback_Reboot_Status(
        ovsdb_update_monitor_t *mon,
        struct schema_Reboot_Status *old,
        struct schema_Reboot_Status *new)
{
    int ir;

    (void)new;

    /*
     * We're interested only in OVSDB_UPDATE_DEL events; these events will
     * update the reboot persistent store data
     */
    if (mon->mon_type != OVSDB_UPDATE_DEL)
    {
        return;
    }

    for (ir = 0; ir < g_dm_reboot.dr_records_len; ir++)
    {
        if (g_dm_reboot.dr_records[ir].dr_bootid == old->count) break;
    }

    if (ir >= g_dm_reboot.dr_records_len)
    {
        LOG(ERR, "dm_reboot: Unable to remove Reboot_Status record with id (count): %d", old->count);
        return;
    }

    /* Remove the record at position ir */
    if ((ir + 1) < g_dm_reboot.dr_records_len)
    {
        memmove(&g_dm_reboot.dr_records[ir],
                &g_dm_reboot.dr_records[ir + 1],
                (g_dm_reboot.dr_records_len - ir - 1) * sizeof(g_dm_reboot.dr_records[0]));
    }

    g_dm_reboot.dr_records_len--;

    ev_debounce_start(EV_DEFAULT, &dm_reboot_update_ev);
}

/**
 * Update the dm_reboot persistent storage with new data after
 * a certain delay
 */
void dm_reboot_update_fn(struct ev_loop *loop, ev_debounce *w, int revents)
{
    (void)loop;
    (void)w;
    (void)revents;

    if (!dm_reboot_store(&g_dm_reboot))
    {
        LOG(ERR, "dm_reboot: Unable store dm_reboot during delayed update.");
        return;
    }
}

static const char *dm_reboot_type_str(enum osp_reboot_type type)
{
    const char *ret = dm_reboot_type_map[OSP_REBOOT_UNKNOWN];

    if (type < ARRAY_LEN(dm_reboot_type_map) && dm_reboot_type_map[type] != NULL)
    {
        ret = dm_reboot_type_map[type];
    }

    return ret;
}

