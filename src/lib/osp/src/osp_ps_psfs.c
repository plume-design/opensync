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
 *  OSP Persistent Storage API implemented using libpsfs
 * ===========================================================================
 */
#include <unistd.h>
#include <errno.h>

#include "osp_ps.h"
#include "psfs.h"
#include "log.h"
#include "kconfig.h"

struct osp_ps
{
    psfs_t ps_psfs;
};

osp_ps_t* osp_ps_open(
        const char *store,
        int flags)
{
    osp_ps_t *ps = calloc(1, sizeof(*ps));

    if (!psfs_open(&ps->ps_psfs, store, flags))
    {
        LOG(DEBUG, "osp_ps: %s: Error opening PSFS storage.", store);
        goto error;
    }

    if (!psfs_load(&ps->ps_psfs))
    {
        LOG(ERR, "osp_ps: %s: Error loading PSFS data,", store);
        psfs_close(&ps->ps_psfs);
        goto error;
    }

    return ps;

error:
    if (ps != NULL) free(ps);
    return NULL;
}

bool osp_ps_close(osp_ps_t *ps)
{
    return psfs_close(&ps->ps_psfs);
}

ssize_t osp_ps_set(
        osp_ps_t *ps,
        const char *key,
        void *value,
        size_t value_sz)
{
    return psfs_set(&ps->ps_psfs, key, value, value_sz);
}

ssize_t osp_ps_get(
        osp_ps_t *ps,
        const char *key,
        void *value,
        size_t value_sz)
{
    return psfs_get(&ps->ps_psfs, key, value, value_sz);
}

bool osp_ps_erase(osp_ps_t *ps)
{
    return psfs_erase(&ps->ps_psfs);
}

bool osp_ps_sync(osp_ps_t *ps)
{
    return psfs_sync(&ps->ps_psfs, false);
}
