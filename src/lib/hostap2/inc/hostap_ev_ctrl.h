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

#ifndef HOSTAP_EV_CTRL_H_INCLUDED
#define HOSTAP_EV_CTRL_H_INCLUDED

#include <hostap_conn.h>
#include <hostap_txq.h>
#include <hostap_ev.h>
#include <hostap_wdog.h>
#include <hostap_sta.h>

struct hostap_ev_ctrl {
    struct hostap_conn *conn;
    struct hostap_conn_ref *conn_ref;
    struct hostap_conn_ref *conn_attach;
    struct hostap_txq_req *attach_req;
    struct hostap_txq_req *attach_probe_req;
    struct hostap_txq *txq;
    struct hostap_sta *sta;
    struct hostap_sta_ref *sta_ref;
    struct hostap_wdog *wdog;
    struct hostap_ev *ev;
    struct ev_loop *loop;
    ev_timer attach_timeout;
};

void
hostap_ev_ctrl_init(struct hostap_ev_ctrl *ctrl,
                    struct ev_loop *loop,
                    const char *ctrl_path,
                    const struct hostap_conn_ref_ops *conn_ops,
                    const struct hostap_sta_ops *sta_ops,
                    void *ops_priv);

void
hostap_ev_ctrl_fini(struct hostap_ev_ctrl *ctrl);

#endif /* HOSTAP_CTRL_H_INCLUDED */
