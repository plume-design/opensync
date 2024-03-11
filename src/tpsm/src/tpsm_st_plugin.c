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
 *  TPSM speedtest plug-in management
 * ===========================================================================
 */

#include "tpsm_st_plugin.h"
#include "log.h"
#include "osa_assert.h"

static ds_tree_t tpsm_st_plugin_list = DS_TREE_INIT(ds_str_cmp, struct tpsm_st_plugin, _st_node);

void tpsm_st_plugin_register(struct tpsm_st_plugin *p)
{
    LOG(INFO, "tpsm_st_plugin: Registering plug-in: %s", p->st_name);
    ds_tree_insert(&tpsm_st_plugin_list, p, (void *)p->st_name);
}

void tpsm_st_plugin_unregister(struct tpsm_st_plugin *p)
{
    LOG(INFO, "tpsm_st_plugin: Un-registering plug-in: %s", p->st_name);
    ASSERT(ds_tree_find(&tpsm_st_plugin_list, p) != NULL, "tpsm_st_plugin double unregister");
    ds_tree_remove(&tpsm_st_plugin_list, p);
}

struct tpsm_st_plugin *tpsm_st_plugin_first(tpsm_st_plugin_iter_t *iter)
{
    return ds_tree_ifirst(&iter->_st_iter, &tpsm_st_plugin_list);
}

struct tpsm_st_plugin *tpsm_st_plugin_next(tpsm_st_plugin_iter_t *iter)
{
    return ds_tree_inext(&iter->_st_iter);
}

struct tpsm_st_plugin *tpsm_st_plugin_find(const char *name)
{
    return ds_tree_find(&tpsm_st_plugin_list, (void *)name);
}
