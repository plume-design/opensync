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
#include <stdbool.h>
#include <memutil.h>
#include <errno.h>

#include <unistd.h>

#include <sys/stat.h>

#include "log.h"
#include "execsh.h"
#include "ds_dlist.h"
#include "ds_map_str.h"
#include "memutil.h"

#include "cake_autorate.h"
#include "osn_adaptive_qos.h"

#define CAKE_AUTORATE_TMPFS_DIR    "/tmp/cake-autorate"
#define CAKE_AUTORATE_TMPFS_CONFIG "/tmp/cake-autorate/config.primary.sh"

static bool cake_autorate_config_files_init(cake_autorate_t *self);
static bool cake_autorate_config_write(cake_autorate_t *self);
static bool cake_autorate_start(cake_autorate_t *self);
static bool cake_autorate_stop(cake_autorate_t *self);
static void cake_autorate_execsh_exit_fn(execsh_async_t *esa, int exit_status);
static void cake_autorate_execsh_io_fn(execsh_async_t *esa, enum execsh_io io_type, const char *msg);

struct refl_entry
{
    osn_ipany_addr_t ip_addr;

    ds_dlist_node_t dlist_node;
};

bool cake_autorate_init(cake_autorate_t *self, const char *DL_ifname, const char *UL_ifname)
{
    memset(self, 0, sizeof(*self));

    self->DL_ifname = strdup(DL_ifname);
    self->UL_ifname = strdup(UL_ifname);

    self->DL_shaper_adjust = true;
    self->DL_min_rate = 5000;
    self->DL_base_rate = 20000;
    self->DL_max_rate = 80000;

    self->UL_shaper_adjust = true;
    self->UL_min_rate = 5000;
    self->UL_base_rate = 20000;
    self->UL_max_rate = 35000;

    ds_dlist_init(&self->reflectors, struct refl_entry, dlist_node);

    self->rand_reflectors = OSN_ADAPTIVE_QOS_DEFAULT_RAND_REFLECTORS;
    self->num_pingers = OSN_ADAPTIVE_QOS_DEFAULT_NUM_PINGERS;
    self->ping_interval = OSN_ADAPTIVE_QOS_DEFAULT_PING_INTERVAL;
    self->active_thresh = OSN_ADAPTIVE_QOS_DEFAULT_ACTIVE_THRESH;

    self->other_config = ds_map_str_new();

    /* Initialize cake-autorate script execsh async handler: */
    execsh_async_init(&self->autorate_script, cake_autorate_execsh_exit_fn);
    execsh_async_set(&self->autorate_script, NULL, cake_autorate_execsh_io_fn);

    if (!cake_autorate_config_files_init(self))
    {
        LOG(ERR, "cake-autorate: Error initializing config files");
        return false;
    }
    return true;
}

bool cake_autorate_DL_shaper_adjust_set(cake_autorate_t *self, bool shaper_adjust)
{
    LOG(TRACE, "%s: shaper_adjust=%d", __func__, shaper_adjust);

    self->DL_shaper_adjust = shaper_adjust;

    return true;
}

bool cake_autorate_DL_shaper_params_set(cake_autorate_t *self, int min_rate, int base_rate, int max_rate)
{
    LOG(TRACE, "%s: min_rate=%d, base_rate=%d, max_rate=%d", __func__, min_rate, base_rate, max_rate);

    if (min_rate <= 0 || base_rate <= 0 || max_rate <= 0) return false;

    self->DL_min_rate = min_rate;
    self->DL_base_rate = base_rate;
    self->DL_max_rate = max_rate;

    return true;
}

bool cake_autorate_UL_shaper_adjust_set(cake_autorate_t *self, bool shaper_adjust)
{
    LOG(TRACE, "%s: shaper_adjust=%d", __func__, shaper_adjust);

    self->UL_shaper_adjust = shaper_adjust;

    return true;
}

bool cake_autorate_UL_shaper_params_set(cake_autorate_t *self, int min_rate, int base_rate, int max_rate)
{
    LOG(TRACE, "%s: min_rate=%d, base_rate=%d, max_rate=%d", __func__, min_rate, base_rate, max_rate);

    if (min_rate <= 0 || base_rate <= 0 || max_rate <= 0) return false;

    self->UL_min_rate = min_rate;
    self->UL_base_rate = base_rate;
    self->UL_max_rate = max_rate;

    return true;
}

bool cake_autorate_reflector_add(cake_autorate_t *self, const osn_ipany_addr_t *ip_addr)
{
    struct refl_entry *entry;

    LOG(TRACE, "%s: Adding reflector: " PRI_osn_ipany_addr, __func__, FMT_osn_ipany_addr(*ip_addr));

    entry = CALLOC(1, sizeof(*entry));
    entry->ip_addr = *ip_addr;

    ds_dlist_insert_tail(&self->reflectors, entry);
    return true;
}

/* Clear all reflectors from the internal reflectors list. */
void cake_autorate_reflector_list_clear(cake_autorate_t *self)
{
    struct refl_entry *entry;

    LOG(TRACE, "%s: Clearing reflector list", __func__);

    while (!ds_dlist_is_empty(&self->reflectors))
    {
        entry = ds_dlist_remove_head(&self->reflectors);
        FREE(entry);
    }
}

bool cake_autorate_reflector_list_add(cake_autorate_t *self, const osn_ipany_addr_t ip_addr_list[], int num)
{
    // Clear the existing list first:
    cake_autorate_reflector_list_clear(self);

    // Add all the reflectors to the list:
    for (int i = 0; i < num; i++)
    {
        cake_autorate_reflector_add(self, &ip_addr_list[i]);
    }
    return true;
}

bool cake_autorate_reflectors_randomize_set(cake_autorate_t *self, bool randomize)
{
    LOG(TRACE, "%s: randomize_reflectors:%d", __func__, randomize);

    self->rand_reflectors = randomize;
    return true;
}

bool cake_autorate_reflectors_ping_interval_set(cake_autorate_t *self, int ping_interval)
{
    LOG(TRACE, "%s: ping_interval:%d", __func__, ping_interval);

    if (ping_interval > 0)
    {
        self->ping_interval = ping_interval;
    }
    else
    {
        self->ping_interval = OSN_ADAPTIVE_QOS_DEFAULT_PING_INTERVAL;
    }
    return true;
}

bool cake_autorate_num_pingers_set(cake_autorate_t *self, int num_pingers)
{
    LOG(TRACE, "%s: num_pingers:%d", __func__, num_pingers);

    if (num_pingers > 0)
    {
        self->num_pingers = num_pingers;
    }
    else
    {
        self->num_pingers = OSN_ADAPTIVE_QOS_DEFAULT_NUM_PINGERS;
    }
    return true;
}

bool cake_autorate_active_threshold_set(cake_autorate_t *self, int threshold)
{
    LOG(TRACE, "%s: threshold:%d", __func__, threshold);

    if (threshold > 0)
    {
        self->active_thresh = threshold;
    }
    else
    {
        self->active_thresh = OSN_ADAPTIVE_QOS_DEFAULT_ACTIVE_THRESH;
    }
    return true;
}

bool cake_autorate_other_config_set(cake_autorate_t *self, const ds_map_str_t *other_config)
{
    ds_map_str_iter_t iter;

    ds_map_str_clear(self->other_config);
    if (other_config == NULL) return true;

    ds_map_str_foreach((ds_map_str_t *)other_config, iter)
    {
        LOG(TRACE, "%s: other_config:%s=%s", __func__, iter.key, iter.val);

        ds_map_str_insert(self->other_config, iter.key, iter.val);
    }
    return true;
}

/* Initialize cake-autorate config files/directories. */
static bool cake_autorate_config_files_init(cake_autorate_t *self)
{
    LOG(DEBUG, "cake-autorate: Initializing configuration files and directories");

    /* Create directory in tmpfs for cake-autorate config file: */
    if (mkdir(CAKE_AUTORATE_TMPFS_DIR, 0755) != 0 && errno != EEXIST)
    {
        LOG(ERR, "cake-autorate: Error creating dir: %s: %s\n", CAKE_AUTORATE_TMPFS_DIR, strerror(errno));
        return false;
    }

    /* Remove any previous or original cake-autorate config file: */
    if (unlink(CONFIG_OSN_ADAPTIVE_QOS_CAKE_AUTORATE_CONFIG) == -1 && errno != ENOENT)
    {
        LOG(ERR,
            "cake-autorate: Error removing file: %s: %s",
            CONFIG_OSN_ADAPTIVE_QOS_CAKE_AUTORATE_CONFIG,
            strerror(errno));
        return false;
    }

    /*
     * Create a symlink in place of the original config files
     * pointing to actual tmpfs-located config file.
     */
    if (symlink(CAKE_AUTORATE_TMPFS_CONFIG, CONFIG_OSN_ADAPTIVE_QOS_CAKE_AUTORATE_CONFIG) == -1)
    {
        LOG(ERR,
            "cake-autorate: Error creating symlink: %s <-- %s: %s",
            CAKE_AUTORATE_TMPFS_CONFIG,
            CONFIG_OSN_ADAPTIVE_QOS_CAKE_AUTORATE_CONFIG,
            strerror(errno));
        return false;
    }

    return true;
}

/* Write current OpenSync config to cake-autorate config file. */
static bool cake_autorate_config_write(cake_autorate_t *self)
{
    bool rv = false;
    FILE *f;

    LOG(DEBUG, "cake-autorate: Writing cake-autorate configuration to file: %s", CAKE_AUTORATE_TMPFS_CONFIG);

    remove(CAKE_AUTORATE_TMPFS_CONFIG);

    f = fopen(CAKE_AUTORATE_TMPFS_CONFIG, "w");
    if (f == NULL)
    {
        LOG(ERR, "cake-autorate: Error opening cake-autorate config file %s", CAKE_AUTORATE_TMPFS_CONFIG);
        return false;
    }

    fprintf(f, "#!/usr/bin/env bash\n");
    fprintf(f, "\n");
    fprintf(f, "dl_if=%s\n", self->DL_ifname);
    fprintf(f, "ul_if=%s\n", self->UL_ifname);
    fprintf(f, "\n");
    fprintf(f, "adjust_dl_shaper_rate=%d\n", self->DL_shaper_adjust ? 1 : 0);
    fprintf(f, "adjust_ul_shaper_rate=%d\n", self->UL_shaper_adjust ? 1 : 0);
    fprintf(f, "\n");
    fprintf(f, "min_dl_shaper_rate_kbps=%d\n", self->DL_min_rate);
    fprintf(f, "base_dl_shaper_rate_kbps=%d\n", self->DL_base_rate);
    fprintf(f, "max_dl_shaper_rate_kbps=%d\n", self->DL_max_rate);
    fprintf(f, "\n");
    fprintf(f, "min_ul_shaper_rate_kbps=%d\n", self->UL_min_rate);
    fprintf(f, "base_ul_shaper_rate_kbps=%d\n", self->UL_base_rate);
    fprintf(f, "max_ul_shaper_rate_kbps=%d\n", self->UL_max_rate);
    fprintf(f, "\n");

    if (!ds_dlist_is_empty(&self->reflectors))  // if custom reflectors list configured
    {
        struct refl_entry *entry;

        fprintf(f, "reflectors=(\n");
        ds_dlist_foreach (&self->reflectors, entry)
        {
            fprintf(f, "\"%s\" ", FMT_osn_ipany_addr(entry->ip_addr));
        }
        fprintf(f, "\n)\n");
    }

    fprintf(f, "randomize_reflectors=%d\n", self->rand_reflectors);
    fprintf(f, "no_pingers=%d\n", self->num_pingers);
    fprintf(f, "reflector_ping_interval_s=%f\n", (double)self->ping_interval / 1000);
    fprintf(f, "connection_active_thr_kbps=%d\n", self->active_thresh);
    fprintf(f, "\n");

    /* By default, the cake-autorate shaping algorithm settings are not aggressive enough.
     * The following will set default latency delta thresholds to lower values
     * to react to latency changes faster. */
    fprintf(f, "dl_owd_delta_thr_ms=10.0\n");
    fprintf(f, "ul_owd_delta_thr_ms=10.0\n");
    fprintf(f, "dl_avg_owd_delta_thr_ms=20.0\n");
    fprintf(f, "ul_avg_owd_delta_thr_ms=20.0\n");
    fprintf(f, "\n");

    fprintf(f, "output_cake_changes=1\n");
    fprintf(f, "debug=0\n");
    fprintf(f, "\n");

    /* other config (mainly for debugging): put verbatim into the config file: */
    if (ds_map_str_size(self->other_config) > 0)
    {
        ds_map_str_iter_t iter;

        ds_map_str_foreach(self->other_config, iter)
        {
            fprintf(f, "%s=%s\n", iter.key, iter.val);
        }
    }
    fprintf(f, "\n");

    LOG(DEBUG, "cake-autorate: Wrote config to: %s", CAKE_AUTORATE_TMPFS_CONFIG);
    rv = true;

    fclose(f);
    return rv;
}

/* Start the cake-autorate script. */
static bool cake_autorate_start(cake_autorate_t *self)
{
    pid_t script_pid;

    LOG(DEBUG, "cake-autorate: Starting cake-autorate script");

    /* Start cake-autorate: */
    script_pid = execsh_async_start(&self->autorate_script, CONFIG_OSN_ADAPTIVE_QOS_CAKE_AUTORATE_PATH);

    if (script_pid == -1)
    {
        LOG(ERR, "cake-autorate: Error starting cake-autorate");
        return false;
    }

    LOG(NOTICE, "cake-autorate: Started, pid=%d", script_pid);
    return true;
}

/* Stop the cake-autorate script. */
static bool cake_autorate_stop(cake_autorate_t *self)
{
    LOG(DEBUG, "cake-autorate: Stopping cake-autorate script");

    execsh_async_stop(&self->autorate_script);

    /* cake-autorate may take some additional time to fully kill all of its subprocesses, the
     * following short sleep is a mitigation for problems when cake_autorate_stop() would be called
     * followed immediately by cake_autorate_start(), in some cases cake-autorate script
     * would complain 'an instance appears to be running with main process pid <pid>.
     * Exiting script.' and not start the script. */
    sleep(1);
    return true;
}

bool cake_autorate_apply(cake_autorate_t *self)
{
    LOG(DEBUG, "cake-autorate: Applying configuraton");

    cake_autorate_stop(self);

    /* Write the current OpenSync config for cake-autorate: */
    if (!cake_autorate_config_write(self))
    {
        LOG(ERR, "cake-autorate: Error writing cake-autorate configuration file");
        return false;
    }

    return cake_autorate_start(self);
}

bool cake_autorate_fini(cake_autorate_t *self)
{
    cake_autorate_stop(self);

    ds_map_str_delete(&self->other_config);
    cake_autorate_reflector_list_clear(self);

    FREE(self->DL_ifname);
    FREE(self->UL_ifname);

    return true;
}

/* cake-autorate script at-exit callback. */
static void cake_autorate_execsh_exit_fn(execsh_async_t *esa, int exit_status)
{
    LOG(NOTICE, "cake-autorate: script exited, exit_status=%d", exit_status);
}

/* cake-autorate script output handler: */
static void cake_autorate_execsh_io_fn(execsh_async_t *esa, enum execsh_io io_type, const char *msg)
{
    if (strlen(msg) == 0)
    {
        return;
    }
    LOG(INFO, "cake-autorate: %s", msg);
}
