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

#ifndef STRONGSWAN_H_INCLUDED
#define STRONGSWAN_H_INCLUDED

#include "osn_ipsec.h"
#include "osn_types.h"
#include "ds_tree.h"
#include "ds_dlist.h"
#include "ev.h"

typedef struct strongswan strongswan_t;

/* strongSwan IPSec tunnel */
struct strongswan
{
    char                           *ss_tunnel_name;

    /* Config: */
    bool                            ss_enable;

    char                           *ss_left;
    char                           *ss_leftid;
    char                           *ss_right;
    char                           *ss_rightid;

    osn_ipany_addr_t                ss_leftsubnet[OSN_SUBNETS_MAX];
    int                             ss_leftsubnet_len;

    osn_ipany_addr_t                ss_rightsubnet[OSN_SUBNETS_MAX];
    int                             ss_rightsubnet_len;

    osn_ipany_addr_t                ss_leftsourceip[OSN_SUBNETS_MAX];
    int                             ss_leftsourceip_len;

    osn_ipany_addr_t                ss_rightsourceip[OSN_SUBNETS_MAX];
    int                             ss_rightsourceip_len;

    enum osn_ipsec_auth_mode        ss_leftauth;
    enum osn_ipsec_auth_mode        ss_rightauth;
    enum osn_ipsec_auth_mode        ss_leftauth2;
    enum osn_ipsec_auth_mode        ss_rightauth2;

    char                           *ss_psk;
    char                           *ss_xauth_user;
    char                           *ss_xauth_pass;
    char                           *ss_eap_identity;
    char                           *ss_eap_id;
    char                           *ss_eap_secret;

    enum osn_ipsec_neg_mode         ss_neg_mode;
    enum osn_ipsec_key_exchange     ss_key_exchange;

    int                             ss_ike_lifetime;
    int                             ss_lifetime;

    enum osn_ipsec_role             ss_role;
    enum osn_ipsec_mode             ss_mode;

    enum osn_ipsec_enc              ss_ike_enc_set[OSN_CIPHER_SUITE_MAX];
    int                             ss_ike_enc_set_len;
    enum osn_ipsec_auth             ss_ike_auth_set[OSN_CIPHER_SUITE_MAX];
    int                             ss_ike_auth_set_len;
    enum osn_ipsec_dh_group         ss_ike_dh_set[OSN_CIPHER_SUITE_MAX];
    int                             ss_ike_dh_set_len;

    enum osn_ipsec_enc              ss_esp_enc_set[OSN_CIPHER_SUITE_MAX];
    int                             ss_esp_enc_set_len;
    enum osn_ipsec_auth             ss_esp_auth_set[OSN_CIPHER_SUITE_MAX];
    int                             ss_esp_auth_set_len;
    enum osn_ipsec_dh_group         ss_esp_dh_set[OSN_CIPHER_SUITE_MAX];
    int                             ss_esp_dh_set_len;

    int                             ss_dpd_delay;
    int                             ss_dpd_timeout;
    enum osn_ipsec_dpd_action       ss_dpd_action;

    int                             ss_mark;       // 0 reserved .. mark not set

    /* Status: */
    struct osn_ipsec_status         ss_status;     // IPSec connection status
    osn_ipsec_status_fn_t          *ss_status_cb;  // IPSec connection status change callback

    ev_tstamp                       ss_time_last_up;

    ds_tree_node_t                  ss_tnode;
};


bool strongswan_init(strongswan_t *self, const char *tunnel_name);

bool strongswan_leftright_set(strongswan_t *self, const char *left, const char *right);

bool strongswan_leftrightid_set(strongswan_t *self, const char *leftid, const char *rightid);

bool strongswan_leftsubnet_set(strongswan_t *self, osn_ipany_addr_t *subnets, int subnets_len);

bool strongswan_rightsubnet_set(strongswan_t *self, osn_ipany_addr_t *subnets, int subnets_len);

bool strongswan_leftsourceip_set(strongswan_t *self, osn_ipany_addr_t *sourceip, int sourceip_len);

bool strongswan_rightsourceip_set(strongswan_t *self, osn_ipany_addr_t *sourceip, int sourceip_len);

bool strongswan_leftrightauth_set(
        strongswan_t *self,
        enum osn_ipsec_auth_mode leftauth,
        enum osn_ipsec_auth_mode rightauth);

bool strongswan_leftrightauth2_set(
        strongswan_t *self,
        enum osn_ipsec_auth_mode leftauth2,
        enum osn_ipsec_auth_mode rightauth2);

bool strongswan_psk_set(strongswan_t *self, const char *psk);

bool strongswan_xauth_credentials_set(
        strongswan_t *self,
        const char *xauth_user,
        const char *xauth_pass);

bool strongswan_eap_identity_set(strongswan_t *self, const char *eap_identity);

bool strongswan_eap_credentials_set(strongswan_t *self, const char *eap_id, const char *eap_secret);

bool strongswan_neg_mode_set(strongswan_t *self, enum osn_ipsec_neg_mode neg_mode);

bool strongswan_key_exchange_set(strongswan_t *self, enum osn_ipsec_key_exchange key_exchange);

bool strongswan_ike_lifetime_set(strongswan_t *self, int ike_lifetime);

bool strongswan_lifetime_set(strongswan_t *self, int lifetime);

bool strongswan_role_set(strongswan_t *self, enum osn_ipsec_role role);

bool strongswan_mode_set(strongswan_t *self, enum osn_ipsec_mode mode);

bool strongswan_ike_cipher_suite_set(
        strongswan_t *self,
        enum osn_ipsec_enc *enc_set, int enc_set_len,
        enum osn_ipsec_auth *auth_set, int auth_set_len,
        enum osn_ipsec_dh_group *dh_set, int dh_set_len);

bool strongswan_esp_cipher_suite_set(
        strongswan_t *self,
        enum osn_ipsec_enc *enc_set, int enc_set_len,
        enum osn_ipsec_auth *auth_set, int auth_set_len,
        enum osn_ipsec_dh_group *dh_set, int dh_set_len);

bool strongswan_dpd_set(
        strongswan_t *self,
        int dpd_delay,
        int dpd_timeout,
        enum osn_ipsec_dpd_action dpd_action);

bool strongswan_mark_set(strongswan_t *self, int mark);

bool strongswan_enable_set(strongswan_t *self, bool enable);

bool strongswan_notify_status_set(strongswan_t *self, osn_ipsec_status_fn_t *status_fn_cb);

bool strongswan_apply(strongswan_t *self);

bool strongswan_fini(strongswan_t *self);

#endif /* STRONGSWAN_H_INCLUDED */
