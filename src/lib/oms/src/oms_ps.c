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

#include "oms.h"
#include "oms_ps.h"
#include "osp_ps.h"
#include "log.h"


bool oms_ps_save_last_active_version(struct oms_state_entry *entry)
{
    ssize_t strsz;
    bool success = false;
    osp_ps_t *ps = NULL;

    ps = osp_ps_open(OMS_STORAGE, OSP_PS_RDWR);
    if (ps == NULL)
    {
        LOG(ERR, "oms: (%s) Unable to open \"%s\" store.", __func__, OMS_STORAGE);
        goto exit;
    }

    strsz = (ssize_t)strlen(entry->version) + 1;
    // Fetch the "store" data
    if (osp_ps_set(ps, entry->object, entry->version, (size_t)strsz) < strsz)
    {
        LOG(ERR, "oms: (%s) Error storing object key %s records: %s", __func__, entry->object, entry->version);
        goto exit;
    }

    success = true;
exit:
    if (ps != NULL) osp_ps_close(ps);

    return success;
}

struct oms_config_entry *oms_ps_get_last_active_version(char *object)
{
    struct oms_config_entry *entry = NULL;
    struct oms_mgr *mgr;
    ds_tree_t *tree;
    ssize_t strsz;
    char *str = NULL;
    osp_ps_t *ps = NULL;

    ps = osp_ps_open(OMS_STORAGE, OSP_PS_RDWR);
    if (ps == NULL)
    {
        LOG(ERR, "oms: (%s) Unable to open \"%s\" store.", __func__, OMS_STORAGE);
        goto exit;
    }

    // Load the object structure
    strsz = osp_ps_get(ps, object, NULL, 0);
    if (strsz < 0)
    {
        LOG(ERR, "oms: (%s) Error fetching \"%s\" key size.", __func__, object);
        goto exit;
    }
    else if (strsz == 0)
    {
        // The record does not exist yet
        goto exit;
    }

    // Fetch the "store" data
    str = malloc((size_t)strsz);
    if (osp_ps_get(ps, object, str, (size_t)strsz) != strsz)
    {
        LOG(ERR, "oms: (%s) Error retrieving persistent \"%s\" key.", __func__, object);
        goto exit;
    }

    mgr = oms_get_mgr();
    tree = &mgr->config;
    entry = ds_tree_head(tree);
    while (entry != NULL)
    {
        if (strcmp(entry->object, object) == 0 &&
            strcmp(entry->version, str) == 0)
        {
            break;
        }
        entry = ds_tree_next(tree, entry);
    }

exit:
    if (str != NULL) free(str);
    if (ps != NULL) osp_ps_close(ps);

    return entry;
}
