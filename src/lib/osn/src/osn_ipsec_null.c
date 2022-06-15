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

#include "osn_ipsec.h"
#include "memutil.h"

struct osn_ipsec
{

};

osn_ipsec_t *osn_ipsec_new(const char *tunnel_name)
{
    (void)tunnel_name;

    osn_ipsec_t *self = CALLOC(1, sizeof(osn_ipsec_t));

    return self;
}

bool osn_ipsec_endpoints_set(osn_ipsec_t *self, const char *local_endpoint, const char *remote_endpoint)
{
    (void)self;
    (void)local_endpoint;
    (void)remote_endpoint;

    return true;
}

bool osn_ipsec_endpoint_ids_set(osn_ipsec_t *self, const char *local_endpoint_id, const char *remote_endpoint_id)
{
    (void)self;
    (void)local_endpoint_id;
    (void)remote_endpoint_id;

    return true;
}

bool osn_ipsec_local_subnet_set(osn_ipsec_t *self, osn_ipany_addr_t *subnets, int subnets_len)
{
    (void)self;
    (void)subnets;
    (void)subnets_len;

    return true;
}

bool osn_ipsec_remote_subnet_set(osn_ipsec_t *self, osn_ipany_addr_t *subnets, int subnets_len)
{
    (void)self;
    (void)subnets;
    (void)subnets_len;

    return true;
}

bool osn_ipsec_local_virtip_set(osn_ipsec_t *self, osn_ipany_addr_t *virtip, int virtip_len)
{
    (void)self;
    (void)virtip;
    (void)virtip_len;

    return true;
}

bool osn_ipsec_remote_virtip_set(osn_ipsec_t *self, osn_ipany_addr_t *virtip, int virtip_len)
{
    (void)self;
    (void)virtip;
    (void)virtip_len;

    return true;
}

bool osn_ipsec_localremote_auth_set(
        osn_ipsec_t *self,
        enum osn_ipsec_auth_mode leftauth,
        enum osn_ipsec_auth_mode rightauth)
{
    (void)self;
    (void)leftauth;
    (void)rightauth;

    return true;
}

bool osn_ipsec_localremote_auth2_set(
        osn_ipsec_t *self,
        enum osn_ipsec_auth_mode leftauth2,
        enum osn_ipsec_auth_mode rightauth2)
{
    (void)self;
    (void)leftauth2;
    (void)rightauth2;

    return true;
}

bool osn_ipsec_psk_set(osn_ipsec_t *self, const char *psk)
{
    (void)self;
    (void)psk;

    return true;
}

bool osn_ipsec_xauth_credentials_set(
        osn_ipsec_t *self,
        const char *xauth_user,
        const char *xauth_pass)
{
    (void)self;
    (void)xauth_user;
    (void)xauth_pass;

    return true;
}

bool osn_ipsec_eap_identity_set(osn_ipsec_t *self, const char *eap_identity)
{
    (void)self;
    (void)eap_identity;

    return true;
}

bool osn_ipsec_eap_credentials_set(osn_ipsec_t *self, const char *eap_id, const char *eap_secret)
{
    (void)self;
    (void)eap_id;
    (void)eap_secret;

    return true;
}

bool osn_ipsec_neg_mode_set(osn_ipsec_t *self, enum osn_ipsec_neg_mode neg_mode)
{
    (void)self;
    (void)neg_mode;

    return true;
}

bool osn_ipsec_key_exchange_set(osn_ipsec_t *self, enum osn_ipsec_key_exchange key_exchange)
{
    (void)self;
    (void)key_exchange;

    return true;
}

bool osn_ipsec_ike_lifetime_set(osn_ipsec_t *self, int ike_lifetime)
{
    (void)self;
    (void)ike_lifetime;

    return true;
}

bool osn_ipsec_lifetime_set(osn_ipsec_t *self, int lifetime)
{
    (void)self;
    (void)lifetime;

    return true;
}

bool osn_ipsec_role_set(osn_ipsec_t *self, enum osn_ipsec_role role)
{
    (void)self;
    (void)role;

    return true;
}

bool osn_ipsec_mode_set(osn_ipsec_t *self, enum osn_ipsec_mode mode)
{
    (void)self;
    (void)mode;

    return true;
}

bool osn_ipsec_ike_cipher_suite_set(
        osn_ipsec_t *self,
        enum osn_ipsec_enc *enc_set, int enc_set_len,
        enum osn_ipsec_auth *auth_set, int auth_set_len,
        enum osn_ipsec_dh_group *dh_set, int dh_set_len)
{
    (void)self;
    (void)enc_set;
    (void)enc_set_len;
    (void)auth_set;
    (void)auth_set_len;
    (void)dh_set;
    (void)dh_set_len;

    return true;
}

bool osn_ipsec_esp_cipher_suite_set(
        osn_ipsec_t *self,
        enum osn_ipsec_enc *enc_set, int enc_set_len,
        enum osn_ipsec_auth *auth_set, int auth_set_len,
        enum osn_ipsec_dh_group *dh_set, int dh_set_len)
{
    (void)self;
    (void)enc_set;
    (void)enc_set_len;
    (void)auth_set;
    (void)auth_set_len;
    (void)dh_set;
    (void)dh_set_len;

    return true;
}

bool osn_ipsec_dpd_set(
        osn_ipsec_t *self,
        int dpd_delay,
        int dpd_timeout,
        enum osn_ipsec_dpd_action dpd_action)
{
    (void)self;
    (void)dpd_delay;
    (void)dpd_timeout;
    (void)dpd_action;

    return true;
}

bool osn_ipsec_mark_set(osn_ipsec_t *self, int mark)
{
    (void)self;
    (void)mark;

    return true;
}

bool osn_ipsec_enable_set(osn_ipsec_t *self, bool enable)
{
    (void)self;
    (void)enable;

    return true;
}

bool osn_ipsec_notify_status_set(osn_ipsec_t *self, osn_ipsec_status_fn_t *status_fn_cb)
{
    (void)self;
    (void)status_fn_cb;

    return true;
}

bool osn_ipsec_apply(osn_ipsec_t *self)
{
    (void)self;

    return true;
}

bool osn_ipsec_del(osn_ipsec_t *self)
{
    FREE(self);

    return true;
}
