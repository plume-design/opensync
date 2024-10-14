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

/* opensync */
#include <log.h>

/* unit */
#include <hostap_ev_ctrl.h>
#include <hostap_conn.h>
#include <hostap_sock.h>

/* private */
#define HOSTAP_EV_CTRL_ATTACH_TIMEOUT_SEC 5.0
#define LOG_PREFIX_CTRL(ctrl, fmt, ...) \
    "hostap: ctrl: %s: " fmt, \
    hostap_sock_get_path( \
    hostap_conn_get_sock(ctrl->conn)), \
    ##__VA_ARGS__

static void
hostap_ev_ctrl_attach_done_cb(struct hostap_txq_req *req,
                              void *priv)
{
    struct hostap_ev_ctrl *ctrl = priv;
    const bool ok = hostap_txq_req_is_reply_ok(req);
    if (ok) {
        LOGD(LOG_PREFIX_CTRL(ctrl, "attached event monitor"));
        WARN_ON(ctrl->opened);
        if (ctrl->opened == false) {
            ctrl->opened = true;
            if (ctrl->ops != NULL &&
                ctrl->ops->opened_fn != NULL) {
                void *ops_priv = ctrl->ops_priv;
                ctrl->ops->opened_fn(ctrl, ops_priv);
            }
        }
    }
    else {
        LOGW(LOG_PREFIX_CTRL(ctrl, "failed to attach: not OK; expect no events"));
    }
    hostap_txq_req_free(ctrl->attach_req);
    ctrl->attach_req = NULL;
    /* timeout is stopped in hostap_ev_ctrl_attach_probe_done_cb */
}

static void
hostap_ev_ctrl_attach_probe_done_cb(struct hostap_txq_req *req,
                                    void *priv)
{
    struct hostap_ev_ctrl *ctrl = priv;
    const bool ok = hostap_txq_req_is_reply_ok(req);
    if (ok) {
        LOGD(LOG_PREFIX_CTRL(ctrl, "attached event monitor with probe request reporting"));
    }
    else {
        LOGI(LOG_PREFIX_CTRL(ctrl, "no probe request reporting (its fine for wpa_s)"));
    }
    hostap_txq_req_free(ctrl->attach_probe_req);
    ctrl->attach_probe_req = NULL;
    ev_timer_stop(ctrl->loop, &ctrl->attach_timeout);
}

static void
hostap_ev_ctrl_attach_timeout_cb(struct ev_loop *loop,
                                 ev_timer *timer,
                                 int events)
{
    struct hostap_ev_ctrl *ctrl = timer->data;
    LOGW(LOG_PREFIX_CTRL(ctrl, "failed to attach: timed out; expect no events"));
}

static void
hostap_ev_ctrl_attach_send_cb(struct hostap_conn_ref *ref,
                              void *priv)
{
    struct hostap_ev_ctrl *ctrl = priv;
    hostap_txq_req_free(ctrl->attach_req);
    LOGD(LOG_PREFIX_CTRL(ctrl, "attaching event monitor"));
    ev_timer_stop(ctrl->loop, &ctrl->attach_timeout);
    ev_timer_start(ctrl->loop, &ctrl->attach_timeout);
    ctrl->attach_req = hostap_txq_request(ctrl->txq,
                                          "ATTACH",
                                          hostap_ev_ctrl_attach_done_cb,
                                          ctrl);
    ctrl->attach_probe_req = hostap_txq_request(ctrl->txq,
                                                "ATTACH probe_rx_events=1",
                                                hostap_ev_ctrl_attach_probe_done_cb,
                                                ctrl);
}

static void
hostap_ev_ctrl_attach_msg_cb(struct hostap_conn_ref *ref,
                             const void *msg,
                             size_t msg_len,
                             bool is_event,
                             void *priv)
{
    struct hostap_ev_ctrl *ctrl = priv;
    if (ctrl->ops == NULL) return;
    if (ctrl->ops->msg_fn == NULL) return;

    void *ops_priv = ctrl->ops_priv;
    ctrl->ops->msg_fn(ref, msg, msg_len, is_event, ops_priv);
}

static void
hostap_ev_ctrl_attach_closed_cb(struct hostap_conn_ref *ref,
                                void *priv)
{
    struct hostap_ev_ctrl *ctrl = priv;
    if (ctrl->ops == NULL) return;
    if (ctrl->ops->closed_fn == NULL) return;

    hostap_txq_req_free(ctrl->attach_req);
    hostap_txq_req_free(ctrl->attach_probe_req);
    ctrl->attach_req = NULL;
    ctrl->attach_probe_req = NULL;

    if (ctrl->opened == false) return;

    void *ops_priv = ctrl->ops_priv;
    ctrl->opened = false;
    ctrl->ops->closed_fn(ctrl, ops_priv);
}

/* public */
void
hostap_ev_ctrl_init(struct hostap_ev_ctrl *ctrl,
                    struct ev_loop *loop,
                    const char *ctrl_path,
                    const struct hostap_ev_ctrl_ops *ops,
                    const struct hostap_sta_ops *sta_ops,
                    void *ops_priv)
{
    static const struct hostap_conn_ref_ops attach_ops = {
        .msg_fn = hostap_ev_ctrl_attach_msg_cb,
        .opened_fn = hostap_ev_ctrl_attach_send_cb,
        .closed_fn = hostap_ev_ctrl_attach_closed_cb,
    };

    ctrl->loop = loop;
    ev_timer_init(&ctrl->attach_timeout, hostap_ev_ctrl_attach_timeout_cb,
                  HOSTAP_EV_CTRL_ATTACH_TIMEOUT_SEC, 0);
    ctrl->attach_timeout.data = ctrl;

    ctrl->ops = ops;
    ctrl->ops_priv = ops_priv;
    ctrl->conn = hostap_conn_alloc(ctrl_path);
    ctrl->conn_attach = hostap_conn_register_ref(ctrl->conn, &attach_ops, ctrl);
    ctrl->ev = hostap_ev_alloc(ctrl->conn, loop);
    ctrl->txq = hostap_txq_alloc(ctrl->conn);
    ctrl->sta = hostap_sta_alloc(ctrl->txq);
    ctrl->sta_ref = sta_ops ? hostap_sta_register(ctrl->sta, sta_ops, ops_priv) : NULL;
    ctrl->wdog = hostap_wdog_alloc(ctrl->txq, loop);
}

void
hostap_ev_ctrl_fini(struct hostap_ev_ctrl *ctrl)
{
    ev_timer_stop(ctrl->loop, &ctrl->attach_timeout);
    hostap_wdog_free(ctrl->wdog);
    hostap_sta_unregister(ctrl->sta_ref);
    hostap_sta_free(ctrl->sta);
    hostap_txq_free(ctrl->txq);
    hostap_ev_free(ctrl->ev);
    hostap_conn_ref_unregister(ctrl->conn_attach);
    hostap_conn_free(ctrl->conn);
    hostap_txq_req_free(ctrl->attach_req);
    hostap_txq_req_free(ctrl->attach_probe_req);
    memset(ctrl, 0, sizeof(*ctrl));
}
