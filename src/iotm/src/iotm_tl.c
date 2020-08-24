/*
Copyright (c) 2020, Charter Communications Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Charter Communications Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "iotm_tl.h"

struct tl_context_tree_t *tl_tree_new()
{
    struct tl_context_tree_t *tl_tree = NULL;
    ds_tree_t *tree = NULL;

    tl_tree = calloc(1, sizeof(struct tl_context_tree_t));
    if (tl_tree == NULL)
    {
        LOGE("%s: Error allocating memory for tl_tree, exiting\n", __func__);
        return NULL;
    }

    tree = calloc(1, sizeof(*tree));
    if (tree == NULL)
    {
        LOGE("%s: Error allocating memory for ds_tree, exiting\n", __func__);
        free(tl_tree);
        return NULL;
    }

    ds_tree_init(
            tree,
            ds_str_cmp, 
            struct tl_context_node_t,
            ctx_node);

    tl_tree->contexts = tree;
    tl_tree->get = tl_tree_get;

    return tl_tree;
}


void **tl_tree_get(struct tl_context_tree_t *self, char *key)
{
    struct tl_context_node_t *node = NULL;
    if (self == NULL) return NULL;

    node = ds_tree_find(self->contexts, key);

    if (node != NULL) return &node->ctx;

    node = calloc(1, sizeof(struct tl_context_node_t));
    node->key = strdup(key);
    node->ctx = NULL;
    ds_tree_insert(self->contexts, node, node->key);

    return &node->ctx;
}

void tl_tree_free(struct tl_context_tree_t *tree)
{
    struct tl_context_node_t *node = NULL;
    struct tl_context_node_t *last = NULL;

    node = ds_tree_head(tree->contexts);
    while (node != NULL)
    {
        last = node;
        node = ds_tree_next(tree->contexts, node);
        free(last->key);
        free(last);
    }

    free(tree->contexts);
    free(tree);
    tree = NULL;
}
