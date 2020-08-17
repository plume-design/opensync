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
 *  WAN Orchestrator Plug-in management
 * ===========================================================================
 */

#include "log.h"
#include "module.h"
#include "osa_assert.h"

#include "wano.h"

static ds_key_cmp_t wanp_cmp;

static ds_tree_t wano_plugin_list = DS_TREE_INIT(wanp_cmp, struct wano_plugin, _wanp_tnode);

void wano_plugin_register(struct wano_plugin *wp)
{
    LOG(INFO, "wano_plugin: Registering plug-in: %s (priority %g)",
              wp->wanp_name, wp->wanp_priority);

    ds_tree_insert(&wano_plugin_list, wp, wp);
}

void wano_plugin_unregister(struct wano_plugin *wp)
{
    LOG(INFO, "wano_plugin: Un-registering plug-in: %s (priority %g)",
              wp->wanp_name, wp->wanp_priority);

    ASSERT(ds_tree_find(&wano_plugin_list, wp) != NULL, "WANO plug-in double unregister")

    ds_tree_remove(&wano_plugin_list, wp);
}

struct wano_plugin *wano_plugin_first(wano_plugin_iter_t *iter)
{
    return ds_tree_ifirst(&iter->wpi_iter, &wano_plugin_list);
}

struct wano_plugin *wano_plugin_next(wano_plugin_iter_t *iter)
{
    return ds_tree_inext(&iter->wpi_iter);
}

struct wano_plugin *wano_plugin_find(const char *name)
{
    struct wano_plugin *wp;

    /*
     * We cannot really do a lookup only by name as the comparator function
     * requires both the priority and the name.
     *
     * We want to return the plug-in with the lowest priority that matches the
     * name
     */
    ds_tree_foreach(&wano_plugin_list, wp)
    {
        if (strcmp(wp->wanp_name, name) == 0) return wp;
    }

    return NULL;
}

wano_plugin_handle_t *wano_plugin_init(
        struct wano_plugin *wp,
        const char *ifname,
        wano_plugin_status_fn_t *status_fn)
{
    wano_plugin_handle_t *wh;

    ASSERT(wp->wanp_init != NULL, "WANO plugin has NULL init function")

    wh = wp->wanp_init(wp, ifname, status_fn);
    if (wh == NULL)
    {
        return NULL;
    }

    wh->wh_plugin = wp;
    STRSCPY(wh->wh_ifname, ifname);

    return wh;
}

void wano_plugin_run(wano_plugin_handle_t *wh)
{
    ASSERT(wh->wh_plugin != NULL, "WANO handle has NULL plug-in reference")
    ASSERT(wh->wh_plugin->wanp_run != NULL, "WANO plug-in has NULL run function")

    wh->wh_plugin->wanp_run(wh);
}

bool wano_plugin_fini(wano_plugin_handle_t *wh)
{
    ASSERT(wh->wh_plugin != NULL, "WANO handle has NULL plug-in reference")
    ASSERT(wh->wh_plugin->wanp_fini != NULL, "WANO plug-in has NULL fini function")

    wh->wh_plugin->wanp_fini(wh);
    return true;
}

/**
 * Plug-in comparator function -- compares the priority and the name
 */
int wanp_cmp(void *_a, void *_b)
{
    int c;

    struct wano_plugin *a = _a;
    struct wano_plugin *b = _b;

    /*
     * We want an ordered list sorted by priority, therefore compare the
     * priority first.
     */
    c = a->wanp_priority - b->wanp_priority;
    if (c != 0) return c;

    c = strcmp(a->wanp_name, b->wanp_name);
    if (c != 0) return c;

    return 0;
}

