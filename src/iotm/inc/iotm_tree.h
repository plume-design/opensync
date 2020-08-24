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

#ifndef IOTM_TREE_H_INCLUDED
#define IOTM_TREE_H_INCLUDED

/**
 * @file iotm_tree.h
 *
 * @brief tree api, key collisions build iotm_list
 *
 * This is a red black tree where each node contains a linked list. This is a
 * pattern frequently required when passing parameter and filter abstractions
 * as it allows for both key/value lookups in the red black tree (such as
 * looking up a MAC) and for key/list lookups (like looking up whitelisted
 * services)
 *
 * The manager doesn't track if the plugin is loading single or multiple
 * values, or if OVSDB has loaded single or multiple.
 */

#include "ds_tree.h"
#include "ovsdb_utils.h"
#include "iotm_list.h"

/**
 * @brief container for RB tree that contains a list
 *
 */
typedef struct iotm_tree_t {
	size_t len;
    void (*free)(struct iotm_tree_t *self); /**< free all elements of the tree */
    char *(*get_val)(struct iotm_tree_t *self, char *key); /**< get a value out of the tree, the head of the list */
    int (*get_type)(
            struct iotm_tree_t *self,
            char *key,
            int type,
            void *out);
    struct iotm_list_t *(*get_list)(struct iotm_tree_t *self, char *key); /**< get a list out of the tree */
    void (*foreach_val)(struct iotm_tree_t *self,
            void(*cb)(ds_list_t *, struct iotm_value_t *, void *),
            void *ctx); /**< iterate over every single value in the tree, this iterates over every node in every list in the tree */
    void (*foreach)(struct iotm_tree_t *self,
        void(*cb)(ds_tree_t *, struct iotm_list_t *, void*),
        void *ctx); /**< iterator for each list in the tree */
    int (*add_val)(struct iotm_tree_t *self,
        char *key,
        struct iotm_value_t *adding); /**< add a struct value  node to the tree */
    int (*add_val_str)(struct iotm_tree_t *self,
        char *key,
        char *val); /**< add a string to the tree */
	ds_tree_t *items; /**< rb tree with each node as an iotm list item  */
	bool init; /**< whether tree is initialized */
} iotm_tree_t;

/**
 * @brief iterate over every value in every list of the tree.
 *
 * @param self  tree to iterate over
 * @param cb    callback to pass each value to, declared by caller
 * @param ctx   void ptr, allows caller to pass data through each callback
 */
void iotm_tree_foreach_value(
        struct iotm_tree_t *self,
        void(*cb)(ds_list_t *, struct iotm_value_t *, void*),
        void *ctx);

/**
 *
 * @brief iterate over every list contained within an IOTM Tree
 *
 * @param self  tree to iterate over
 * @param cb    callback, this will be evoked for every list in the tree
 * @param ctx   context, void * that is used by caller, passthrough
 */
void iotm_tree_foreach(struct iotm_tree_t *self,
        void(*cb)(ds_tree_t *, struct iotm_list_t *, void*),
        void *ctx);

/**
 * @brief : convert 2 static arrays of in a dynamically allocated tree
 *
 * Takes a set of 2 arrays representing <string key, string value> pairs,
 * and creates a DS tree of one element lists where the only element is
 * <key><list>
 *
 * @param elem_size provisioned size of strings in the input arrays
 * @param nelems number of actual elements in the input arrays
 * @param keys the static input array of string keys
 * @param values the static input array of string values
 * @return a pointer to a iotm tree if successful, NULL otherwise
 */
iotm_tree_t *schema2iotmtree(
        size_t key_size,
        size_t value_size,
        size_t nelems,
        char keys[][key_size],
        char values[][value_size]);

/**
 * @brief get a new iotm tree node
 *
 * @return tree node that has been allocated
 */
struct iotm_tree_t *iotm_tree_new();

/**
 * @brief free all elements of a tree
 */
void iotm_tree_free(iotm_tree_t *self);

/**
 * @brief add a list to a tree
 */
int iotm_tree_add_list(struct iotm_tree_t *self, char *key, struct iotm_list_t *adding);

/**
 * @brief add a struct to the tree
 *
 * @param key     tree key to store under, will push to this list
 * @param adding  value to add
 */
int iotm_tree_add(struct iotm_tree_t *self,
        char *key,
        struct iotm_value_t *adding);

/**
 * @brief add a value as a set to a tree node
 * 
 * @note will not insert if value already exists in list
 */
int iotm_tree_set_add_str(struct iotm_tree_t *self,
        char *key,
        char *value);

/**
 * @brief add a value to set in the tree
 */
int iotm_tree_set_add(struct iotm_tree_t *self,
        char *key,
        struct iotm_value_t *val);

/**
 * @brief remove a list from a tree
 */
int iotm_tree_remove_list(
				struct iotm_tree_t *self,
				char *key);
/**
 * @brief add a struct to the tree
 *
 * @param key     tree key to store under, will push to this list
 * @param adding  value to add, string
 */
int iotm_tree_add_str(struct iotm_tree_t *self,
        char *key,
        char *value);

/**
 * @brief allows for the addition of any value to a tree by type
 *
 * @param self   tree for insert
 * @param key    key to add value with
 * @param type   enum TYPE to insert
 * @param value  value of type, will be cast for conversion
 *
 * @return 0     inserted the value
 * @return -1    failed to convert/insert the value
 */
int iotm_tree_add_type(
        struct iotm_tree_t *self,
        char *key,
        int type,
        void *value);

/**
 * @brief add any type, won't add if value exists matching key
 *
 * @param self  tree for addition
 * @param key   key to add to tree
 * @param type  type for conversion
 * @param value value of type, will be cast for conversion
 *
 * @return 0 type converted and inserted
 * @return -1 failed to convert or insert
 */
int iotm_tree_set_add_type(
        struct iotm_tree_t *self,
        char *key,
        int type,
        void *value);
/**
 * @brief get a list from the tree, allocates a new node if it doesn't exist
 */
struct iotm_list_t *iotm_tree_get(struct iotm_tree_t *self, char *key);

/**
 * @brief find a value out of a tree, 
 */
struct iotm_list_t *iotm_tree_find(struct iotm_tree_t *self, char *key);

/**
 * @brief get a value struct from a tree
 */
struct iotm_value_t *iotm_tree_get_single(struct iotm_tree_t *self, char *key);

/**
 * @brief retrieve a value from the tree, convert to a type
 *
 * @param self   tree to perform lookup in
 * @param key    key for lookup
 * @param type   enum datatype to convert to
 * @param out    void ptr output, cast based on type enum 
 *
 * @return 0     successful, void ptr loaded
 * @return -1    error loading output
 */
int iotm_tree_get_single_type(
        struct iotm_tree_t *self,
        char *key,
        int type,
        void *out);

/**
 * @brief iterating over each value match in a tree, casting to correct type
 *
 * @param self       tree to iterate over
 * @param key        key to identify iteration component
 * @param type       ENUM type to cast to, defined in iotm_data_types.h
 * @param cb         called for each match, value can be cast to type
 * @param ctx        passthrough context to allow caller data
 */
void iotm_tree_foreach_type(
        struct iotm_tree_t *self,
        char *key,
        int type,
        void (*cb)(char *key, void *val, void *ctx),
        void *ctx);

/**
 * @brief get a string from a tree
 */
char *iotm_tree_get_single_str(struct iotm_tree_t *self, char *key);

/**
 * @brief add any values from one tree to another
 *
 * @note doesn't copy other value for function pointers, only does str copy
 */
int iotm_tree_concat_str(struct iotm_tree_t *dst, struct iotm_tree_t *src);

#endif // IOTM_TREE_H_INCLUDED */
