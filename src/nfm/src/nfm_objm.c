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

#include "const.h"
#include "evx.h"
#include "log.h"
#include "osp_objm.h"
#include "ovsdb_table.h"
#include "schema.h"

#include "nfm_ipset.h"

/* Watch only objm objects with this name */
#define NFM_OBJM_NAME           "netfilter_ipset"
/* Event debounce in seconds */
#define NFM_OBJM_DEBOUNCE_S     0.5

/* Cached objm data */
struct nfm_objm
{
    char            no_version[SCHEMA_COLUMN_SZ(Object_Store_State, version)];
    bool            no_ready;       /** Set to true when the object is ready */
    bool            no_pending;     /** True if event needs to be sent */
    ds_tree_node_t  no_tnode;
};

static void callback_Object_Store_State(
        ovsdb_update_monitor_t *mon,
        struct schema_Object_Store_State *old,
        struct schema_Object_Store_State *new);

static void nfm_objm_drop(const char *vesrion);
static struct nfm_objm *nfm_objm_get(const char *version);
static void nfm_objm_dispatch(void);
ev_debounce_fn_t nfm_objm_dispatch_fn;
static ds_key_cmp_t nfm_objm_version_cmp;

static ovsdb_table_t table_Object_Store_State;
/*
 * Cache of Object_Store_State objects where name == NFM_OBJM_NAME,
 * reverse sorted by version. This means that the first element will always point
 * to the latest version.
 */
static ds_tree_t nfm_objm_list = DS_TREE_INIT(nfm_objm_version_cmp, struct nfm_objm, no_tnode);

static ev_debounce nfm_objm_debounce;

bool nfm_objm_init(void)
{
    OVSDB_TABLE_INIT(Object_Store_State, name);
    OVSDB_TABLE_MONITOR(Object_Store_State, true);

    ev_debounce_init2(&nfm_objm_debounce, nfm_objm_dispatch_fn, 0.5, 3.0);

    return true;
}

static void callback_Object_Store_State(
        ovsdb_update_monitor_t *mon,
        struct schema_Object_Store_State *old,
        struct schema_Object_Store_State *new)
{
    struct nfm_objm *no;
    const char *old_name;
    bool ready;

    const char *old_version = NULL;
    const char *new_version = NULL;

    /*
     * The logic below is somewhat complicated mainly due to handling of
     * renames in the `name` and `version` field. When the code below is
     * finished there are only two possible outcomes:
     *
     * - if old_version is not NULL, the old_version entry is removed
     * - if new_vesrion is not NULL, the new_version entry is added or updated
     */
    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            if (strcmp(new->name, NFM_OBJM_NAME) == 0)
            {
                new_version = new->version;
            }
            break;

        case OVSDB_UPDATE_MODIFY:
            old_name = old->name_exists ? old->name : new->name;
            if (strcmp(old_name, NFM_OBJM_NAME) == 0)
            {
                old_version = old->version_exists ? old->version : new->version;
            }

            if (strcmp(new->name, NFM_OBJM_NAME) == 0)
            {
                new_version = new->version;
            }

            /*
             * If both version are the same, we can simply update
             */
            if (old_version != NULL &&
                    new_version != NULL &&
                    strcmp(old_version, new_version) == 0)
            {
                old_version = NULL;
            }
            break;

        case OVSDB_UPDATE_DEL:
            if (strcmp(old->name, NFM_OBJM_NAME) == 0)
            {
                old_version = old->version;
            }
            break;

        case OVSDB_UPDATE_ERROR:
            LOG(ERR, "nfm_objm: Object_Store_State monitor error.");
            return;
    }

    LOG(DEBUG, "nfm_objm: update old=%s, new=%s", old_version, new_version);

    if (old_version != NULL)
    {
        nfm_objm_drop(old_version);
    }

    if (new_version != NULL)
    {
        no = nfm_objm_get(new_version);
        ready = new->status_exists && (strcmp(new->status, "install-done") == 0);
        if (ready != no->no_ready)
        {
            no->no_pending = true;
        }
        no->no_ready = ready;
    }

    if (old_version != NULL || new_version != NULL)
    {
        nfm_objm_dispatch();
    }
}

struct nfm_objm *nfm_objm_get(const char *version)
{
    struct nfm_objm *no;

    no = ds_tree_find(&nfm_objm_list, (char *)version);
    if (no == NULL)
    {
        no = calloc(1, sizeof(*no));
        STRSCPY(no->no_version, version);
        no->no_pending = true;
        ds_tree_insert(&nfm_objm_list, no, no->no_version);
    }

    return no;
}

void nfm_objm_drop(const char *version)
{
    struct nfm_objm *no;

    no = ds_tree_find(&nfm_objm_list, (char *)version);
    if (no == NULL)
    {
        LOG(ERR, "nfm_objm: Unable to delete version %s.", version);
        return;
    }

    ds_tree_remove(&nfm_objm_list, no);
    free(no);
}


void nfm_objm_dispatch(void)
{
    ev_debounce_start(EV_DEFAULT, &nfm_objm_debounce);
}

void nfm_objm_dispatch_fn(struct ev_loop *loop, ev_debounce *w, int revent)
{
    (void)w;
    (void)loop;
    (void)revent;

    struct nfm_objm *no;

    no = ds_tree_head(&nfm_objm_list);

    if (no == NULL || no->no_pending)
    {
        char path[C_MAXPATH_LEN];

        char *version = (no == NULL) ? NULL : no->no_version;
        bool ready = (no == NULL) ? false : no->no_ready;

        if (version == NULL || !ready)
        {
            path[0] = '\0';
        }
        else
        {
            osp_objm_path(path, sizeof(path), NFM_OBJM_NAME, version);
        }

        LOG(DEBUG, "nfm_objm: Dispatching version=%s, ready=%d, path=%s",
                version, ready, path);

        nfm_ipset_objm_notify(version, ready, path);
    }

    if (no != NULL) no->no_pending = false;
}

int nfm_objm_version_cmp(void *a, void *b)
{
    /*
     * Invert the parameters so newer versions end up at the beginning of the tree.
     * This way ds_tree_head() will always fetch the newest version.
     */
    return ds_str_cmp(b, a);
}
