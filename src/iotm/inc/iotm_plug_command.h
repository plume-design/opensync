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

#ifndef IOTM_PLUGIN_COMMAND_H_INCLUDED
#define IOTM_PLUGIN_COMMAND_H_INCLUDED

/**
 * @file iotm_plug_command.h
 *
 * @brief iotm constructs a command, then sends to a plugin
 *
 * A command is sent by the IoT Manager to a plugin's handler when a rule
 * action matches that plugin. 
 */

#include "iotm_list.h"
#include "iotm_tree.h"

/**
 * @brief allocate a new command
 */
struct plugin_command_t *plugin_command_new();

/**
 * @brief free all data in current command
 *
 * This is called by the manager after the plugin has processed the command and
 * returned a code.
 */
void plugin_command_free(struct plugin_command_t *self);

/**
 * @brief add a param to a command
 *
 * This is utilized by the IoT manager to load in any params specified by the
 * rule
 */
int plugin_command_add(struct plugin_command_t *self,
        char *key,
        struct iotm_value_t *param);

/**
 * @brief get a list of params from a command
 *
 * @note example: 'serv_whitelist' : ['first_service', 'second_service']
 */
struct iotm_list_t *plugin_command_get_list(struct plugin_command_t *self,
        char *key);

/**
 * @brief get a single param matching the key
 *
 * @note inteded for use on values that plugin knows aren't lists
 * e.g.: 'mac' : 'AA:BB:CC:DD:EE:FF'
 */
char *plugin_command_get(struct plugin_command_t *self,
        char *key);

/**
 * @brief these are the operations that a plugin may utilize when interacting
 * with a command
 *
 * @note when the IoT manager identifies an event that must be routed to a
 * plugin, the manager will build a command and route it to the plugin. These
 * methods are provided to the plugin for retrieving the information needed to
 * handle the command.
 */
struct command_ops_t
{
    struct iotm_list_t *(*get_params)(struct plugin_command_t *self,
            char *key); /**< get the list associated with the parameter key */
    char *(*get_param)(struct plugin_command_t *self,
            char *key); /**< get the head of the list associated with key, used when expected value should be single */
    int (*get_param_type)(
            struct plugin_command_t *self,
            char *key,
            int type, 
            void *out);
    void (*foreach_param_type)(
            struct plugin_command_t *self,
            char *key,
            int type,
            void (*cb)(char *key, void *val, void *ctx),
            void *ctx);
};

/**
 * @brief retrieve a command from a plugin of a given type
 *
 * @param      self   plugin to get command from
 * @param      key    key for parameter (i.e. mac)
 * @param      type   type to get, i.e. UINT16
 * @param[out] out    storage for converted parameter
 */
int plugin_command_get_param_type(
        struct plugin_command_t *self,
        char *key,
        int type, 
        void *out);

/**
 * @brief iterate over multiple parameters matching a key, converted to a type
 * 
 * @param self    plugin command for iteration
 * @param key     key to iterate over, i.e. 'UUID'
 * @param type    type for conversion, i.e. UINT16
 * @param cb      callback, will be passed converted type for each match
 * @param ctx     context for caller to pass to cb
 */
void plugin_command_foreach_param_type(
        struct plugin_command_t *self,
        char *key,
        int type,
        void (*cb)(char *key, void *val, void *ctx),
        void *ctx);
/**
 * @brief contains structures for an iot command
 *
 * @note passes as a result of an event hitting rules, and a rule passing the
 * filter
 */
struct plugin_command_t {
    char *action; /**< action to be taken */
    struct command_ops_t ops; /**< Utilities provided to plugin for interacting with command struct */
    struct iotm_tree_t *params;   /**< params in OVSDB rule to be passed with command */
} plugin_command_t;

#endif // IOTM_PLUGIN_COMMAND_H_INCLUDED */
