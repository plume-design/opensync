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

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "osn_ipsec.h"
#include "strongswan.h"
#include "memutil.h"
#include "log.h"

struct osn_ipsec
{
    strongswan_t    is_strongswan;
};

osn_ipsec_t *osn_ipsec_new(const char *tunnel_name)
{
    osn_ipsec_t *self = CALLOC(1, sizeof(osn_ipsec_t));

    if (!strongswan_init(&self->is_strongswan, tunnel_name))
    {
        LOG(ERR, "ipsec: %s: Error creating strongSwan tunnel object", tunnel_name);
        FREE(self);
        return NULL;
    }
    return self;
}

bool osn_ipsec_endpoints_set(osn_ipsec_t *self, const char *local_endpoint, const char *remote_endpoint)
{
    return strongswan_leftright_set(&self->is_strongswan, local_endpoint, remote_endpoint);
}

bool osn_ipsec_endpoint_ids_set(osn_ipsec_t *self, const char *local_endpoint_id, const char *remote_endpoint_id)
{
    return strongswan_leftrightid_set(&self->is_strongswan, local_endpoint_id, remote_endpoint_id);
}

bool osn_ipsec_local_subnet_set(osn_ipsec_t *self, osn_ipany_addr_t *subnets, int subnets_len)
{
    return strongswan_leftsubnet_set(&self->is_strongswan, subnets, subnets_len);
}

bool osn_ipsec_remote_subnet_set(osn_ipsec_t *self, osn_ipany_addr_t *subnets, int subnets_len)
{
    return strongswan_rightsubnet_set(&self->is_strongswan, subnets, subnets_len);
}

bool osn_ipsec_local_virtip_set(osn_ipsec_t *self, osn_ipany_addr_t *virtip, int virtip_len)
{
    return strongswan_leftsourceip_set(&self->is_strongswan, virtip, virtip_len);
}

bool osn_ipsec_remote_virtip_set(osn_ipsec_t *self, osn_ipany_addr_t *virtip, int virtip_len)
{
    return strongswan_rightsourceip_set(&self->is_strongswan, virtip, virtip_len);
}

bool osn_ipsec_localremote_auth_set(
        osn_ipsec_t *self,
        enum osn_ipsec_auth_mode leftauth,
        enum osn_ipsec_auth_mode rightauth)
{
    return strongswan_leftrightauth_set(&self->is_strongswan, leftauth, rightauth);
}

bool osn_ipsec_localremote_auth2_set(
        osn_ipsec_t *self,
        enum osn_ipsec_auth_mode leftauth2,
        enum osn_ipsec_auth_mode rightauth2)
{
    return strongswan_leftrightauth2_set(&self->is_strongswan, leftauth2, rightauth2);
}

bool osn_ipsec_psk_set(osn_ipsec_t *self, const char *psk)
{
    return strongswan_psk_set(&self->is_strongswan, psk);
}

bool osn_ipsec_xauth_credentials_set(
        osn_ipsec_t *self,
        const char *xauth_user,
        const char *xauth_pass)
{
    return strongswan_xauth_credentials_set(&self->is_strongswan, xauth_user, xauth_pass);
}

bool osn_ipsec_eap_identity_set(osn_ipsec_t *self, const char *eap_identity)
{
    return strongswan_eap_identity_set(&self->is_strongswan, eap_identity);
}

bool osn_ipsec_eap_credentials_set(osn_ipsec_t *self, const char *eap_id, const char *eap_secret)
{
    return strongswan_eap_credentials_set(&self->is_strongswan, eap_id, eap_secret);
}

bool osn_ipsec_neg_mode_set(osn_ipsec_t *self, enum osn_ipsec_neg_mode neg_mode)
{
    return strongswan_neg_mode_set(&self->is_strongswan, neg_mode);
}

bool osn_ipsec_key_exchange_set(osn_ipsec_t *self, enum osn_ipsec_key_exchange key_exchange)
{
    return strongswan_key_exchange_set(&self->is_strongswan, key_exchange);
}

bool osn_ipsec_ike_lifetime_set(osn_ipsec_t *self, int ike_lifetime)
{
    return strongswan_ike_lifetime_set(&self->is_strongswan, ike_lifetime);
}

bool osn_ipsec_lifetime_set(osn_ipsec_t *self, int lifetime)
{
    return strongswan_lifetime_set(&self->is_strongswan, lifetime);
}

bool osn_ipsec_role_set(osn_ipsec_t *self, enum osn_ipsec_role role)
{
    return strongswan_role_set(&self->is_strongswan, role);
}

bool osn_ipsec_mode_set(osn_ipsec_t *self, enum osn_ipsec_mode mode)
{
    return strongswan_mode_set(&self->is_strongswan, mode);
}

bool osn_ipsec_ike_cipher_suite_set(
        osn_ipsec_t *self,
        enum osn_ipsec_enc *enc_set, int enc_set_len,
        enum osn_ipsec_auth *auth_set, int auth_set_len,
        enum osn_ipsec_dh_group *dh_set, int dh_set_len)
{
    return strongswan_ike_cipher_suite_set(
            &self->is_strongswan,
            enc_set, enc_set_len,
            auth_set, auth_set_len,
            dh_set, dh_set_len);
}

bool osn_ipsec_esp_cipher_suite_set(
        osn_ipsec_t *self,
        enum osn_ipsec_enc *enc_set, int enc_set_len,
        enum osn_ipsec_auth *auth_set, int auth_set_len,
        enum osn_ipsec_dh_group *dh_set, int dh_set_len)
{
    return strongswan_esp_cipher_suite_set(
            &self->is_strongswan,
            enc_set, enc_set_len,
            auth_set, auth_set_len,
            dh_set, dh_set_len);
}

bool osn_ipsec_dpd_set(
        osn_ipsec_t *self,
        int dpd_delay,
        int dpd_timeout,
        enum osn_ipsec_dpd_action dpd_action)
{
    return strongswan_dpd_set(&self->is_strongswan, dpd_delay, dpd_timeout, dpd_action);
}

bool osn_ipsec_mark_set(osn_ipsec_t *self, int mark)
{
    return strongswan_mark_set(&self->is_strongswan, mark);
}

bool osn_ipsec_enable_set(osn_ipsec_t *self, bool enable)
{
    return strongswan_enable_set(&self->is_strongswan, enable);
}

bool osn_ipsec_notify_status_set(osn_ipsec_t *self, osn_ipsec_status_fn_t *status_fn_cb)
{
    return strongswan_notify_status_set(&self->is_strongswan, status_fn_cb);
}

bool osn_ipsec_apply(osn_ipsec_t *self)
{
    return strongswan_apply(&self->is_strongswan);
}

bool osn_ipsec_del(osn_ipsec_t *self)
{
    bool retval = true;

    if (!strongswan_fini(&self->is_strongswan))
    {
        LOG(ERR, "ipsec: %s: Error destroying strongSwan tunnel object",
                self->is_strongswan.ss_tunnel_name);
        retval = false;
    }
    FREE(self);
    return retval;
}
