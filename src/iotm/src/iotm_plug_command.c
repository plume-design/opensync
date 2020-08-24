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

#include "iotm_plug_command.h"
#include "iotm_list.h"

struct plugin_command_t *alloc_command()
{
    return (struct plugin_command_t *)calloc(1, sizeof(plugin_command_t));
}

struct plugin_command_t *plugin_command_new()
{
    struct plugin_command_t *cmd = NULL;

    cmd = alloc_command();
    if ( cmd == NULL ) return NULL;

    cmd->params = iotm_tree_new();

	//initialize ops
	cmd->ops.get_params = plugin_command_get_list;
	cmd->ops.get_param = plugin_command_get;
    cmd->ops.get_param_type = plugin_command_get_param_type;
    cmd->ops.foreach_param_type = plugin_command_foreach_param_type;
	return cmd;
}

int plugin_command_add(struct plugin_command_t *self,
        char *key,
        struct iotm_value_t *param)
{
    int err = -1;
    if ( self == NULL ) return err;
    if ( param == NULL ) return err;

    return iotm_tree_add(self->params, key, param);
}

struct iotm_list_t *plugin_command_get_list(struct plugin_command_t *self,
        char *key)
{
    return iotm_tree_get(self->params, key);
}

char *plugin_command_get(struct plugin_command_t *self,
        char *key)
{
    if ( self == NULL ) return NULL;
    if ( key == NULL ) return NULL;

    struct iotm_value_t *val = iotm_tree_get_single(self->params, key);

    if ( val == NULL ) return NULL;
    return val->value;
}

void plugin_command_free(struct plugin_command_t *self)
{
    if ( self == NULL ) return;

    iotm_tree_free(self->params);
    if ( self->action ) free(self->action);
    free(self);
}


int plugin_command_get_param_type(
        struct plugin_command_t *self,
        char *key,
        int type, 
        void *out)
{
    return iotm_tree_get_single_type(self->params, key, type, out);
}

void plugin_command_foreach_param_type(
        struct plugin_command_t *self,
        char *key,
        int type,
        void (*cb)(char *key, void *val, void *ctx),
        void *ctx)
{
    iotm_tree_foreach_type(self->params, key, type, cb, ctx);
}
