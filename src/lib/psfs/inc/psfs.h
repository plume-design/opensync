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

#ifndef PSFS_H_INCLUDED
#define PSFS_H_INCLUDED

#include <stdbool.h>

#include "ds_tree.h"

struct psfs
{
    char            psfs_name[64];      /* Store name */
    int             psfs_fd;            /* Store file descriptor */
    int             psfs_dirfd;         /* Store folder descriptor */
    int             psfs_flags;         /* Flags */
    ds_tree_t       psfs_root;          /* Key/Value cache */
    ssize_t         psfs_used;          /* Number of bytes used by "good" records
                                           in this store */
};

typedef struct psfs psfs_t;

struct psfs_record
{
    char           *pr_key;             /* Key */
    uint8_t        *pr_data;            /* Data */
    size_t          pr_datasz;          /* Data size in bytes */
    bool            pr_dirty;           /* True if record is dirty */
    ds_tree_node_t  pr_tnode;           /* Tree node */
    ssize_t         pr_used;            /* On-disk bytes used by disk record */
    off_t           pr_off;             /* Record offset */
};

bool psfs_open(psfs_t *ps, const char *name, int flags);
bool psfs_close(psfs_t *ps);
bool psfs_load(psfs_t *ps);
bool psfs_sync(psfs_t *ps, bool force_prune);
bool psfs_erase(psfs_t *ps);
ssize_t psfs_set(psfs_t *ps, const char *key, const void *value, size_t value_sz);
ssize_t psfs_get(psfs_t *ps, const char *key, void *value, size_t value_sz);

#endif /* PSFS_H_INCLUDED */
