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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dlfcn.h>
#include <fnmatch.h>
#include <glob.h>

#include "log.h"
#include "ds_tree.h"
#include "module.h"

/*
 * Information about dynamically loaded modules
 */
struct module_load
{
    char           *ml_path;        /* Path name that was used to load the module */
    void           *ml_dlhandle;    /* Handle as returned by dlopen() */
    ds_tree_node_t  ml_tnode;       /* Tree node */
};

/*
 * We want to unload modules automatically only when a module_unload() function
 * is called. Destructors are called before the executable object is unloaded
 * from memory, this includes any exit() functions or returns from the main
 * function. In order to prevent unloading modules after the main() function is
 * returned (or after forks()) the destructor function checks this variable
 * is set to true. If its not, it will not auto-unload a module.
 *
 * module_load() will set this variable to true before calling dlclose().
 */
static bool module_unload_guard;

static ds_dlist_t module_list = DS_DLIST_INIT(
        module_t,
        m_dnode);

static ds_tree_t module_load_list = DS_TREE_INIT(
        ds_str_cmp,
        struct module_load,
        ml_tnode);

static bool __module_unload(struct module_load *ml);

void module_register(module_t *mod)
{
    ds_dlist_insert_tail(&module_list, mod);
}

void module_unregister(module_t *mod)
{
    module_stop(mod);
    ds_dlist_remove(&module_list, mod);
}

/*
 * Autmoatically unload a module only when module_unload_guard is set to true.
 * This typically happens inside module_unload().
 */
void __module_dtor(module_t *mod)
{
    if (!module_unload_guard) return;

    module_unregister(mod);
}

void module_start(module_t *mod)
{
    if (mod->m_started) return;

    mod->m_start_fn();
    mod->m_started = true;
}

void module_stop(module_t *mod)
{
    if (!mod->m_started) return;

    mod->m_stop_fn();
    mod->m_started = false;
}

void module_init(void)
{
    module_t *m;

    ds_dlist_foreach(&module_list, m)
    {
        module_start(m);
    }
}

void module_fini(void)
{
    module_t *m;

    ds_dlist_foreach(&module_list, m)
    {
        module_stop(m);
    }
}

bool module_load(const char *path)
{
    struct module_load *ml;

    /* Check if module already exists */
    ml = ds_tree_find(&module_load_list, (void *)path);
    if (ml != NULL)
    {
        LOG(DEBUG, "module: Module %s already loaded, skipping.", path);
        return true;
    }

    /* Allocate new object */
    ml = calloc(1, sizeof(*ml));
    if (ml == NULL)
    {
        LOG(ERR, "module: Failed to allocate object for module: %s", path);
        goto error;
    }

    ml->ml_path = strdup(path);
    if (ml->ml_path == NULL)
    {
        LOG(ERR, "module: Failed to allocate path: %s", path);
        goto error;
    }

    /*
     * Load module dynamically. Note that dlopen() will call the constructor
     * functions before it actually returns. This means that the module will
     * be loaded AND registered once this function returns.
     */
    ml->ml_dlhandle = dlopen(path, RTLD_LOCAL | RTLD_NOW);
    if (ml->ml_dlhandle == NULL)
    {
        LOG(ERR, "module: Error loading module: %s. Error: %s", path, dlerror());
        goto error;
    }

    ds_tree_insert(&module_load_list, ml, ml->ml_path);

    LOG(NOTICE, "module: Dynamic module successfully loaded: %s", path);

    return true;

error:
    if (ml != NULL)
    {
        if (ml->ml_dlhandle != NULL) dlclose(ml->ml_dlhandle);
        if (ml->ml_path != NULL) free(ml->ml_path);
        free(ml);
    }

    return false;
}

bool __module_unload(struct module_load *ml)
{
    int rc;

    /*
     * We can simply unload the module, destructor functions will take care
     * of un-registering the module from the module list and stopping it if
     * necessary. However, the destructor must be called only and exclusively
     * during module_unload() or similar functions. In order to do this, a global
     * flag (module_dlclose_guard) is set to true before each call to dlclose().
     * This is to prevent the destructors from unloading modules during a call
     * to exit() (when forking to background, for example).
     */
    module_unload_guard = true;
    rc = dlclose(ml->ml_dlhandle);
    module_unload_guard = false;
    if (rc != 0)
    {
        LOG(WARN, "module: Error unloading shared object for module: %s", ml->ml_path);
    }

    return true;
}

bool module_unload(const char *path)
{
    struct module_load *ml;

    ml = ds_tree_find(&module_load_list, (void *)path);
    if (ml == NULL)
    {
        LOG(DEBUG, "module: Module %s not loaded.", path);
        return true;
    }

    __module_unload(ml);

    ds_tree_remove(&module_load_list, ml);
    free(ml->ml_path);
    free(ml);

    return true;
}

bool module_load_all(const char *pattern)
{
    glob_t mg;
    size_t ii;
    struct stat st;
    int rc;

    int nml;
    bool retval = false;

    rc = glob(pattern, 0, NULL, &mg);

    /* No modules were found */
    if (rc == GLOB_NOMATCH)
    {
        LOG(NOTICE, "module: No modules found matching %s.", pattern);
        retval = true;
        goto exit;
    }

    if (rc != 0)
    {
        LOG(WARN, "module: Glob error using pattern: %s. No modules were loaded.", pattern);
        goto exit;
    }

    nml = 0;
    for (ii = 0; ii < mg.gl_pathc; ii++)
    {
        if (access(mg.gl_pathv[ii], R_OK) != 0)
        {
            LOG(NOTICE, "module: Insufficient permissions to access file %s. Skipping.", mg.gl_pathv[ii]);
            continue;
        }

        if (stat(mg.gl_pathv[ii], &st) != 0)
        {
            LOG(NOTICE, "module: Unable to stat file %s. Skipping.", mg.gl_pathv[ii]);
            continue;
        }

        if (!S_ISREG(st.st_mode))
        {
            LOG(NOTICE, "module: Not a regular file: %s. Skipping.", mg.gl_pathv[ii]);
            continue;
        }

        if (!module_load(mg.gl_pathv[ii]))
        {
            /* module_load() already reported errors, do not print anything here */
            continue;
        }

        nml++;
    }

    LOG(NOTICE, "module: Found %zd files matching '%s', loaded %d modules.", mg.gl_pathc, pattern, nml);
    retval= true;

exit:
    globfree(&mg);
    return retval;
}

bool module_unload_all(const char *pattern)
{
    ds_tree_iter_t iter;
    struct module_load *ml;
    int nml;

    nml = 0;
    ds_tree_foreach_iter(&module_load_list, ml, &iter)
    {
        if (fnmatch(pattern, ml->ml_path, FNM_PATHNAME) != 0) continue;

        if (!__module_unload(ml))
        {
            /* Errors already reported by __module_unload() */
            continue;
        }

        ds_tree_iremove(&iter);
        free(ml->ml_path);
        free(ml);

        nml++;
    }

    LOG(NOTICE, "module: Unloaded %d modules.", nml);
    return true;
}
