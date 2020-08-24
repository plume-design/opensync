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

#ifndef IOTM_TL_H_INCLUDED
#define IOTM_TL_H_INCLUDED

/**
 * @file iotm_tl.h
 *
 * @brief allows plugins to namespace out target layer contexts
 *
 * Includes ds_tree for managing contexts between target layer sessions
 * IoT target layers are passed a context for all of their methods so they can
 * store references to scoped variables. This tree allows for a plugin to
 * provide a unique key for the target layer context such as 'ble' to specify
 * what context it would like to pass. This allows for multiple plugins to pass
 * the same 'ble context' while also allowing a zigbee context to be
 * distinguished from a ble context.
 */

#include "ds_tree.h"
#include "log.h"         /* Logging routines */

typedef struct tl_context_node_t
{
    char *key; /**< lookup value for tree */
    void *ctx; /**< context to be passed to target layer */
    ds_tree_node_t ctx_node; /**< for ds tree api */
} tl_context_node_t;

typedef struct tl_context_tree_t
{
    void **(*get)(struct tl_context_tree_t *self, char *key); /**< get a context based off a tree */
    ds_tree_t *contexts; /**< all target layer contexts tracked for plugins */
} tl_context_tree_t;

/**
 * @brief gets current or allocates new if not in tree
 *
 * @param self  tree to interact with
 * @param key   value to get from tree
 */
void **tl_tree_get(struct tl_context_tree_t *self, char *key);

/**
 * @brief get a new instance of a target layer context tree
 *
 * @return ptr to tree if init success
 * @return NULL on failure
 */
struct tl_context_tree_t *tl_tree_new();

/**
 * @brief free all members of a target tree
 * 
 * @param tree  tree to free members of
 */
void tl_tree_free(struct tl_context_tree_t *tree);

#endif // IOTM_TL_H_INCLUDED
