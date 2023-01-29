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

#include <net/if.h>
#include <linux/un.h>
#include <linux/limits.h>

#include "sm.h"
#include "opensync-ctrl.h"
#include "opensync-hapd.h"

unsigned int CLIENT_SIZE_LIMIT = CONFIG_SM_BACKEND_HAPD_CLIENTS_AUTH_FAILS_PER_VAP_LIMIT;
static const char backend_name[] = "client_auth_fails";

static void
sm_client_auth_fails_hostapd_wpa_key_mismatch(struct hapd *hapd, const char *mac)
{
    sm_client_auth_fails_client_t *client;

    if ((client = sm_client_auth_fails_get_client((const char *)hapd->ctrl.bss, mac)))
    {
        client->invalid_psk++;
        LOGD("%s: sta: %s auth failed: key mismatch", backend_name, mac);
    }
}

bool
sm_client_auth_fails_bss_priv_init(void **priv, const char *radio_name, const char *vif_name)
{
    struct hapd *hapd = NULL;

    hapd = hapd_lookup(vif_name) ?: hapd_new(radio_name, vif_name);
    if (!hapd) {
        LOGD("%s: Failed to alloc hapd for client auth fails reporting for if_name: %s",
             backend_name, vif_name);
        return false;
    }

    hapd->wpa_key_mismatch = sm_client_auth_fails_hostapd_wpa_key_mismatch;
    *priv = hapd;

    /*
     * ctrl_enable() will eventually connect to hostapd once WM will prepare
     * everything.
     */
    (void) ctrl_enable(&hapd->ctrl);

    return true;
}

void
sm_client_auth_fails_bss_priv_free(void *priv)
{
    if (priv)
        hapd_release((struct hapd *)priv);
}

bool
sm_client_auth_fails_collect_data(void *priv)
{
    /* We're not doing any work here, data collected asynchronously by hapd hooks */
    return true;
}

bool
sm_client_auth_fails_implementation_not_null(void)
{
    return true;
}
