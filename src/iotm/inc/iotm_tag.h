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

#ifndef IOTM_TAG_H_INCLUDED
#define IOTM_TAG_H_INCLUDED

/**
 * @file iotm_tag.h
 *
 * @brief tree containing Openflow_Tags, apis for string->tag conversion
 *
 * Includes mgr struct and data shared with plugins
 */

#include "schema.h"

#include "iotm_tree.h"

/**
 * @brief whether string has variable
 *
 * @note example: 'test string ${variable}' 
 *
 * @param str   string to check
 *
 * @return true    found variable match
 * @return false   no variables in string
 */
bool str_has_template(const char *str);

/**
 * @brief pull all variables matching special chars from string
 *
 * @param        rule  string to find variables in
 * @param[out]   vars  key/value list of all found variables
 */
int tag_extract_all_vars(
        const char *rule,
        iotm_list_t *vars);

/**
 * @brief remove a row from the tree of tracked tags
 *
 * @param tree    tree to reference tags from
 * @param row     OVSDB row to remove from tree data
 */
int remove_tag_from_tree(
        iotm_tree_t *tree,
        struct schema_Openflow_Tag *row);

/**
 * @brief add a row to the tree of tracked tags
 *
 * @param tree   tree to track tags in
 * @param row    OVSDB row to add to tree
 */
int add_tag_to_tree(
        iotm_tree_t *tree,
        struct schema_Openflow_Tag *row);

/**
 * @brief print all tags in tree
 *
 * @param tags   tree to print
 */
void tag_print_tree(struct iotm_tree_t *tags);

/**
 * @brief passed as context when updating key
 */
struct key_update_t
{
    char *newk;
    void(*cb)(ds_list_t *, struct iotm_value_t *, void *);
    void *ctx;
};

/**
 * @brief get list of all values matching tag, pass to callback
 *
 * @param tag  tag matching Openflow_Tag value
 * @param key  key that this tag was used for (mac, uuid, etc.)
 * @param cb   callback to pass all matches to
 * @param ctx  context for cb passthrough
 */
void expand_tags(
        char *tag,
        char *key,
        void(*cb)(ds_list_t *, struct iotm_value_t *, void *),
        void *ctx);

#endif // IOTM_TAG_H_INCLUDED */
