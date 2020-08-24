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

#include "iotm_plug_event.h"
#include "iotm_tree.h"

struct plugin_event_t *plugin_event_new()
{
    struct plugin_event_t *event = NULL;

    event = (struct plugin_event_t *)calloc(1, sizeof(plugin_event_t));
    if ( event == NULL ) return NULL;

    event->params = iotm_tree_new();
    event->initialized = true;

    // load event ops
    event->ops.plugin_event_free = plugin_event_free;
    event->ops.add_param = plugin_event_add;
    event->ops.add_param_str = plugin_event_add_str;
    event->ops.add_param_type = plugin_event_add_type;
    event->ops.find = plugin_event_find;

    return event;
}

void plugin_event_free(struct plugin_event_t *self)
{
    if ( self == NULL ) return;
    if ( self->params == NULL ) return;

    iotm_tree_free(self->params);
    if ( self ) free(self);
}


int plugin_event_add_str(struct plugin_event_t *self,
        char *key,
        char *val)
{
    return iotm_tree_add_str(self->params, key, val);
}

int plugin_event_add_type(
		struct plugin_event_t *self,
        char *key,
		int type,
        void *val)
{
    return iotm_tree_add_type(self->params, key, type, val);
}

int plugin_event_add(struct plugin_event_t *self,
        char *key,
        struct iotm_value_t *param)
{
    if ( self == NULL ) return -1;
    if ( param == NULL ) return -1;
    if ( !self->initialized ) return -1;

    return iotm_tree_add(self->params, key, param);
}


struct iotm_list_t *plugin_event_get_list(struct plugin_event_t *event, char *key)
{
    return iotm_tree_get(event->params, key);
}

struct iotm_value_t *plugin_event_get(struct plugin_event_t *event, char *key)
{
    if ( event == NULL ) return NULL;
    if ( key == NULL ) return NULL;

    struct iotm_list_t *list = plugin_event_get_list(event, key);
    if ( list == NULL ) return NULL;
    return (struct iotm_value_t *)ds_list_head(&list->items);
}

struct iotm_list_t *plugin_event_find(struct plugin_event_t *self, char *key)
{
    return ds_tree_find(self->params->items, key);
}
