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

#ifndef OSW_STA_CACHE_H
#define OSW_STA_CACHE_H

struct osw_sta;
struct osw_sta_link;
struct osw_sta_observer;
struct osw_sta_cache_observer;

typedef void
osw_sta_cache_sta_appeared_fn_t(struct osw_sta_cache_observer *self,
                                struct osw_sta *sta);

typedef void
osw_sta_cache_sta_vanished_fn_t(struct osw_sta_cache_observer *self,
                                struct osw_sta *sta);

typedef void
osw_sta_connected_fn_t(struct osw_sta_observer *self,
                       struct osw_sta *sta,
                       const struct osw_sta_link *link);

typedef void
osw_sta_disconnected_fn_t(struct osw_sta_observer *self,
                          struct osw_sta *sta,
                          const struct osw_sta_link *link);

typedef void
osw_sta_probe_req_fn_t(struct osw_sta_observer *self,
                       struct osw_sta *sta,
                       const struct osw_sta_link *link,
                       const struct osw_drv_report_vif_probe_req *probe_req);

struct osw_sta_observer {
    const char *name;
    osw_sta_connected_fn_t *const connected_fn;
    osw_sta_disconnected_fn_t *const disconnected_fn;
    osw_sta_probe_req_fn_t *const probe_req_fn;

    struct ds_dlist_node node;
};

struct osw_sta_cache_observer {
    const char *name;
    osw_sta_cache_sta_appeared_fn_t *const appeared_fn;
    osw_sta_cache_sta_vanished_fn_t *const vanished_fn;

    struct ds_dlist_node node;
};

void
osw_sta_cache_register_observer(struct osw_sta_cache_observer *observer);

void
osw_sta_cache_unregister_observer(struct osw_sta_cache_observer *observer);

struct osw_sta*
osw_sta_cache_lookup_sta(const struct osw_hwaddr *sta_addr);

void
osw_sta_register_observer(struct osw_sta *sta,
                          struct osw_sta_observer *observer);

void
osw_sta_unregister_observer(struct osw_sta *sta,
                            struct osw_sta_observer *observer);

const struct osw_hwaddr*
osw_sta_get_mac_addr(const struct osw_sta *sta);

const struct osw_state_vif_info*
osw_sta_link_get_vif_info(const struct osw_sta_link *link);

#endif /* OSW_STA_CACHE_H */
