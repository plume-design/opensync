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

#ifndef OSW_STA_H_INCLUDED
#define OSW_STA_H_INCLUDED

#include <osw_types.h>

#define OSW_STA_MAX_LINKS 16 /* 802.11be limit */

typedef enum
{
    OSW_STA_ASSOC_UNDEFINED,
    OSW_STA_ASSOC_CONNECTED,
    OSW_STA_ASSOC_RECONNECTED,
    OSW_STA_ASSOC_DISCONNECTED,
#define OSW_STA_EVENT_COUNT 4
} osw_sta_assoc_event_e;

typedef struct osw_sta_assoc osw_sta_assoc_t;
typedef struct osw_sta_assoc_entry osw_sta_assoc_entry_t;
typedef struct osw_sta_assoc_link osw_sta_assoc_link_t;
typedef struct osw_sta_assoc_links osw_sta_assoc_links_t;
typedef struct osw_sta_assoc_observer osw_sta_assoc_observer_t;
typedef struct osw_sta_assoc_observer_params osw_sta_assoc_observer_params_t;
typedef void osw_sta_assoc_observer_notify_fn_t(
        void *priv,
        const osw_sta_assoc_entry_t *entry,
        osw_sta_assoc_event_e ev);

struct osw_sta_assoc_link
{
    struct osw_hwaddr local_sta_addr;
    struct osw_hwaddr remote_sta_addr;
};

struct osw_sta_assoc_links
{
    osw_sta_assoc_link_t links[OSW_STA_MAX_LINKS];
    size_t count;
};

const char *osw_sta_assoc_event_to_cstr(osw_sta_assoc_event_e ev);

osw_sta_assoc_observer_params_t *osw_sta_assoc_observer_params_alloc(void);
void osw_sta_assoc_observer_params_drop(osw_sta_assoc_observer_params_t *params);
void osw_sta_assoc_observer_params_set_changed_fn(
        osw_sta_assoc_observer_params_t *params,
        osw_sta_assoc_observer_notify_fn_t *fn,
        void *priv);
void osw_sta_assoc_observer_params_set_addr(osw_sta_assoc_observer_params_t *params, const struct osw_hwaddr *sta_addr);

osw_sta_assoc_observer_t *osw_sta_assoc_observer_alloc(osw_sta_assoc_t *m, osw_sta_assoc_observer_params_t *params);
void osw_sta_assoc_observer_drop(osw_sta_assoc_observer_t *o);
const osw_sta_assoc_entry_t *osw_sta_assoc_observer_get_entry(osw_sta_assoc_observer_t *o);

bool osw_sta_assoc_entry_is_connected(const osw_sta_assoc_entry_t *e);
bool osw_sta_assoc_entry_is_mlo(const osw_sta_assoc_entry_t *e);
size_t osw_sta_assoc_entry_get_assoc_ies_len(const osw_sta_assoc_entry_t *e);
const void *osw_sta_assoc_entry_get_assoc_ies_data(const osw_sta_assoc_entry_t *e);
const struct osw_hwaddr *osw_sta_assoc_entry_get_addr(const osw_sta_assoc_entry_t *e);
const struct osw_hwaddr *osw_sta_assoc_entry_get_local_mld_addr(const osw_sta_assoc_entry_t *e);
const osw_sta_assoc_links_t *osw_sta_assoc_entry_get_active_links(const osw_sta_assoc_entry_t *e);
const osw_sta_assoc_links_t *osw_sta_assoc_entry_get_stale_links(const osw_sta_assoc_entry_t *e);

const osw_sta_assoc_link_t *osw_sta_assoc_links_lookup(
        const osw_sta_assoc_links_t *l,
        const struct osw_hwaddr *local_sta_addr,
        const struct osw_hwaddr *remote_sta_addr);
void osw_sta_assoc_links_append_local_to(const osw_sta_assoc_links_t *l, struct osw_hwaddr_list *list);
void osw_sta_assoc_links_append_remote_to(const osw_sta_assoc_links_t *l, struct osw_hwaddr_list *list);

#endif /* OSW_STA_H_INCLUDED */
