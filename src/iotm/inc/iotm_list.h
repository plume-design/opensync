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

#ifndef IOTM_LIST_H_INCLUDED
#define IOTM_LIST_H_INCLUDED

/**
 * @file iotm_list.h
 *
 * @brief list that stores key/value or allows custom data
 *
 */
#include "ds_tree.h"
#include "ds_list.h"
#include "log.h"

typedef struct iotm_value_t
{
    char *value;
    char *key;
    void *other;
    void (*free_other)(void *); /**< if caller loads something into other, they can add a function here to free that element */
    ds_list_t list_node;
} iotm_value_t;

typedef struct iotm_list_t
{
    char *key; /**< key that list is nested under */
    void (*foreach)(struct iotm_list_t *self, void(*cb)(ds_list_t *, struct iotm_value_t *, void*), void *ctx); /**< allows iteration over list elements */
    int (*add_str)(struct iotm_list_t *self, char *adding); /**< allows addition of string to tree */
    int (*free)(struct iotm_list_t *); /**< free the list */
    char *(*get_head_str)(struct iotm_list_t *list); /**< get the first element as a string */
    size_t len; /**< number of members in list */
    ds_list_t items;
    ds_tree_node_t list_node;
} iotm_list_t;

/**
 * @brief print the members of an iotm list
 *
 * @param self   list to iterate and print
 */
void iotm_list_print(struct iotm_list_t *self);

/**
 * @brief add a string to the list
 *
 * @param self  list to add to
 * @param val   string to add
 *
 * @return 0 string added
 * @return -1 failed to add string
 */
int iotm_list_add_str(struct iotm_list_t *self, char *val);

/**
 * @brief get the first element in the list as a string
 */
char *iotm_list_get_head_str(struct iotm_list_t *list);

/**
 * @brief return a ds list that is ready to work with
 */
struct iotm_list_t *iotm_list_new();

/**
 * @brief add a list element to a list list
 *
 * @note will allocate memory for the list, cleaned at end or on remove
 *
 * @param list     struct containing list nodes
 * @param value    value struct to be added
 *
 * @return 0 added item to list
 * @return -1 problems adding item to list
 */
int iotm_list_add(struct iotm_list_t *list, struct iotm_value_t *value);

/**
 * @brief add the value if it doesn't already exist in the list
 *
 * @param list   list to add to
 * @param value  unique value to add
 *
 * @return 0 value added
 * @return -1 error adding
 */
int iotm_set_add(struct iotm_list_t *list, struct iotm_value_t *val);

/**
 * @brief add the string if it doesn't already exist in the list
 *
 * @param list   list to add to
 * @param value  unique string to add
 *
 * @return 0 string added
 * @return -1 error adding
 */
int iotm_set_add_str(struct iotm_list_t *self, char *val);

/**
 * @brief free 
 */
void iotm_value_free(struct iotm_value_t *self);
/**
 * @brief iterates over all list elements and frees them 
 *
 * @param list container for value nodes
 *
 * @return 0 freed all elements in list
 */
int iotm_list_free(struct iotm_list_t *list);

/**
 * @brief helper for iterating over each list element
 *
 * @param lists  container for list list
 * @param cb       callback function defined by caller, recieves item and
 * voidptr
 * @param ctx      a void pointer defined by the caller, castable within
 * callback to allow for complex functional patterns like fold.
 *
 */
void iotm_list_foreach(struct iotm_list_t *self, void(*cb)(ds_list_t *, struct iotm_value_t *, void*), void *ctx);

/**
 * @brief check whether the iotm_val is in the list
 */
bool is_in_list(struct iotm_list_t *self, struct iotm_value_t *val);

/**
 * @brief check whether the string is in the list
 */
bool is_in_list_str(struct iotm_list_t *self, char *val);

struct iotm_value_t *iotm_list_get_head(struct iotm_list_t *list);
char *iotm_list_get_head_str(struct iotm_list_t *list);
/**
 * @breif update all they keys in the list
 */
void iotm_list_update_key(struct iotm_list_t *self, char *newkey);

/**
 * @brief print the value that is passed
 *
 * @note matches foreach cb
 *
 * @param dl       linked list value was pulled from
 * @param val      value to print members of
 * @param context  context for caller
 */
void iotm_print_value(ds_list_t *dl, struct iotm_value_t *val, void *context);
#endif // IOTM_LIST_H_INCLUDED */
