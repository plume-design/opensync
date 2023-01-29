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

#ifndef HOSTAP_STA_H_INCLUDED
#define HOSTAP_STA_H_INCLUDED

/* opensync */
#include <os_types.h>

/* unit */
#include <hostap_txq.h>

struct hostap_sta;
struct hostap_sta_ref;

struct hostap_sta_info {
    os_macaddr_t addr;
    const char *buf;
    /* FIXME: This could be nicer, but it's up to the
     * consumer to parse the data for now.
     */
};

typedef void
hostap_sta_connected_fn_t(struct hostap_sta_ref *ref,
                          const struct hostap_sta_info *info,
                          void *priv);

typedef void
hostap_sta_changed_fn_t(struct hostap_sta_ref *ref,
                        const struct hostap_sta_info *info,
                        const char *old_buf,
                        void *priv);

typedef void
hostap_sta_disconnected_fn_t(struct hostap_sta_ref *ref,
                             const struct hostap_sta_info *info,
                             void *priv);

struct hostap_sta_ops {
    hostap_sta_connected_fn_t *connected_fn;
    hostap_sta_changed_fn_t *changed_fn;
    hostap_sta_disconnected_fn_t *disconnected_fn;
};

struct hostap_sta *
hostap_sta_alloc(struct hostap_txq *txq);

void
hostap_sta_free(struct hostap_sta *sta);

void
hostap_sta_rebuild(struct hostap_sta *sta);

const struct hostap_sta_info *
hostap_sta_get_info(struct hostap_sta *sta,
                    const os_macaddr_t *addr);

struct hostap_sta_ref *
hostap_sta_register(struct hostap_sta *sta,
                    const struct hostap_sta_ops *ops,
                    void *priv);

void
hostap_sta_unregister(struct hostap_sta_ref *ref);

#endif /* HOSTAP_STA_H_INCLUDED */
