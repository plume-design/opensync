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

#ifndef OSW_BSS_MAP_H
#define OSW_BSS_MAP_H

#include <osw_types.h>

struct osw_bss_entry;
struct osw_bss_provider;
struct osw_bss_map_observer;

typedef void
osw_bss_set_fn_t(struct osw_bss_map_observer *observer,
                 const struct osw_hwaddr *bssid,
                 const struct osw_bss_entry *bss_entry);

typedef void
osw_bss_unset_fn_t(struct osw_bss_map_observer *observer,
                   const struct osw_hwaddr *bssid);

struct osw_bss_map_observer {
    const char *const name;
    osw_bss_set_fn_t *const set_fn; /* Called on addition and/or modification */
    osw_bss_unset_fn_t *const unset_fn; /* Called when lasst prvider frees BSS's entry */

    struct ds_dlist_node node;
};

struct osw_bss_provider*
osw_bss_map_register_provider(void);

void
osw_bss_map_unregister_provider(struct osw_bss_provider *provider);

void
osw_bss_map_register_observer(struct osw_bss_map_observer *observer);

void
osw_bss_map_unregister_observer(struct osw_bss_map_observer *observer);

struct osw_bss_entry*
osw_bss_map_entry_new(struct osw_bss_provider *provider,
                      const struct osw_hwaddr *bssid);

void
osw_bss_map_entry_free(struct osw_bss_provider *provider,
                       struct osw_bss_entry* entry);

#define OSW_BSS_ENTRY_SET_PROTOTYPE(attr_type, attr)                        \
    void                                                                    \
    osw_bss_entry_set_ ## attr(struct osw_bss_entry* entry,                 \
                               const attr_type *attr);

#define OSW_BSS_GET_PROTOTYPE(attr_type, attr)                              \
    const attr_type*                                                        \
    osw_bss_get_ ## attr(const struct osw_hwaddr* bssid);

OSW_BSS_ENTRY_SET_PROTOTYPE(struct osw_ssid, ssid);
OSW_BSS_ENTRY_SET_PROTOTYPE(struct osw_channel, channel);
OSW_BSS_ENTRY_SET_PROTOTYPE(uint8_t, op_class);

OSW_BSS_GET_PROTOTYPE(struct osw_ssid, ssid);
OSW_BSS_GET_PROTOTYPE(struct osw_channel, channel);
OSW_BSS_GET_PROTOTYPE(uint8_t, op_class);

#undef OSW_BSS_SET_PROTOTYPE
#undef OSW_BSS_GET_PROTOTYPE

#endif /* OSW_BSS_MAP_H */
