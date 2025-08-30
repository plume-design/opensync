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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "vpnm.h"
#include "osn_types.h"
#include "osn_ipsec.h"
#include "log.h"
#include "util.h"
#include "ovsdb_table.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "schema.h"
#include "ds_tree.h"

#define IPSEC_SCHEMA_IPSTR_LEN (sizeof(((struct schema_IPSec_Config *)(NULL))->local_subnets[0]))
#define IPSEC_SCHEMA_CIPHERSTR_LEN (sizeof(((struct schema_IPSec_Config *)(NULL))->ike_enc_suite[0]))

/* Keep track of IPSec_Config's */
struct vpnm_ipsec
{
    char               vi_name[C_MAXPATH_LEN];    /* Tunnel (and config) name */
    osn_ipsec_t       *vi_ipsec;                  /* Corresponding OSN layer IPsec object */

    osn_ipany_addr_t   vi_remote_endpoint_ip;    /* Cached remote endpoint IP.
                                                  * Needed for cases when we resolve
                                                  * an FQDN to an IP to report the
                                                  * IP in IPSec_State. */

    ds_tree_node_t     vi_tnode;
};

/* The IPSec_Config and IPSec_State OVSDB tables we're handling here */
static ovsdb_table_t table_IPSec_Config;
static ovsdb_table_t table_IPSec_State;

/* Keeping track of IPsec configs */
static ds_tree_t vpnm_ipsec_list = DS_TREE_INIT(ds_str_cmp, struct vpnm_ipsec, vi_tnode);

static const osn_ipany_addr_t IP4_ANY = { .addr_type = AF_INET, .addr.ip4 = OSN_IP_ADDR_INIT };

/* OpenSync/target supported ciphers lists (initialized from Kconfig): */
static enum osn_ipsec_enc       supported_enc_list[OSN_CIPHER_SUITE_MAX];
static enum osn_ipsec_auth      supported_auth_list[OSN_CIPHER_SUITE_MAX];
static enum osn_ipsec_dh_group  supported_dh_list[OSN_CIPHER_SUITE_MAX];

static bool vpnm_ipsec_config_set(
        struct vpnm_ipsec *vpn_ipsec,
        ovsdb_update_monitor_t *mon,
        struct schema_IPSec_Config *old,
        const struct schema_IPSec_Config *new);
static bool vpnm_ipsec_ovsdb_state_upsert(
        struct vpnm_ipsec *vpn_ipsec,
        struct schema_IPSec_Config *ipsec_config);
static bool vpnm_ipsec_ovsdb_state_delete(const char *tunnel_name);

static struct vpnm_ipsec *vpnm_ipsec_new(const char *tunnel_name)
{
    struct vpnm_ipsec *vpn_ipsec;

    vpn_ipsec = CALLOC(1, sizeof(*vpn_ipsec));
    STRSCPY(vpn_ipsec->vi_name, tunnel_name);

    /* Create a new OSN layer IPsec object: */
    vpn_ipsec->vi_ipsec = osn_ipsec_new(tunnel_name);
    if (vpn_ipsec->vi_ipsec == NULL)
    {
        LOG(ERROR, "vpnm_ipsec: %s: Error creating OSN ipsec context", tunnel_name);
        FREE(vpn_ipsec);
        return NULL;
    }

    vpn_ipsec->vi_remote_endpoint_ip = IP4_ANY;

    ds_tree_insert(&vpnm_ipsec_list, vpn_ipsec, vpn_ipsec->vi_name);

    return vpn_ipsec;
}

static bool vpnm_ipsec_del(struct vpnm_ipsec *vpn_ipsec)
{
    bool rv = true;

    /* Delete OSN layer IPsec object: */
    if (!osn_ipsec_del(vpn_ipsec->vi_ipsec))
    {
        LOG(ERROR, "vpnm_ipsec: %s: Error destroying ipsec tunnel", vpn_ipsec->vi_name);
        rv = false;
    }

    ds_tree_remove(&vpnm_ipsec_list, vpn_ipsec);
    FREE(vpn_ipsec);
    return rv;
}

static struct vpnm_ipsec *vpnm_ipsec_get(const char *tunnel_name)
{
    return ds_tree_find(&vpnm_ipsec_list, tunnel_name);
}



static int util_ipanny_addr_array_from_str(
        osn_ipany_addr_t *subnets, int subnets_len,
        const char str_subnets[OSN_SUBNETS_MAX][IPSEC_SCHEMA_IPSTR_LEN], int str_subnets_len)
{
    int i;

    for (i = 0; i < str_subnets_len && i < subnets_len; i++)
    {
        if (!osn_ipany_addr_from_str(&subnets[i], str_subnets[i]))
        {
            LOG(ERROR, "vpnm_ipsec: Error parsing IPv4 or IPv6 address: %s", str_subnets[i]);
            return -1;
        }
    }
    return i;
}

static enum osn_ipsec_auth_mode util_auth_mode_from_schemastr(const char *auth_str)
{
    if (strcmp(auth_str, "psk") == 0)
        return OSN_IPSEC_AUTHMODE_PSK;
    else if (strcmp(auth_str, "pubkey") == 0)
        return OSN_IPSEC_AUTHMODE_PUBKEY;
    else if (strcmp(auth_str, "xauth") == 0)
        return OSN_IPSEC_AUTHMODE_XAUTH;
    else if (strcmp(auth_str, "eap-mschapv2") == 0)
        return OSN_IPSEC_AUTHMODE_EAP_MSCHAPv2;
    else
        return OSN_IPSEC_AUTHMODE_NOT_SET; /* default */
}

static enum osn_ipsec_neg_mode util_neg_mode_from_schemastr(const char *neg_mode)
{
    if (strcmp(neg_mode, "main") == 0)
        return OSN_IPSEC_NEG_MAIN;
    else if (strcmp(neg_mode, "aggressive") == 0)
        return OSN_IPSEC_NEG_AGGRESSIVE;
    else
        return OSN_IPSEC_NEG_MAIN; /* default */
}

static enum osn_ipsec_key_exchange util_keyexchange_from_schemastr(const char *key_exchange)
{
    if (strcmp(key_exchange, "ike") == 0)
        return OSN_IPSEC_KEY_EXCHANGE_IKE;
    else if (strcmp(key_exchange, "ikev1") == 0)
        return OSN_IPSEC_KEY_EXCHANGE_IKEv1;
    else if (strcmp(key_exchange, "ikev2") == 0)
        return OSN_IPSEC_KEY_EXCHANGE_IKEv2;
    else
        return OSN_IPSEC_KEY_EXCHANGE_IKE; /* default */
}

static enum osn_ipsec_enc util_enc_from_schemastr(const char *enc)
{
    if (strcmp(enc, "3des") == 0)
        return OSN_IPSEC_ENC_3DES;
    else if (strcmp(enc, "aes128") == 0)
        return OSN_IPSEC_ENC_AES128;
    else if (strcmp(enc, "aes192") == 0)
        return OSN_IPSEC_ENC_AES192;
    else if (strcmp(enc, "aes256") == 0)
        return OSN_IPSEC_ENC_AES256;
    else
        return OSN_IPSEC_ENC_NOT_SET;
}

static enum osn_ipsec_auth util_auth_from_schemastr(const char *auth)
{
    if (strcmp(auth, "sha1") == 0)
        return OSN_IPSEC_AUTH_SHA1;
    else if (strcmp(auth, "md5") == 0)
        return OSN_IPSEC_AUTH_MD5;
    else if (strcmp(auth, "sha256") == 0)
        return OSN_IPSEC_AUTH_SHA256;
    else
        return OSN_IPSEC_AUTH_NOT_SET;
}

static enum osn_ipsec_dh_group util_dh_from_schemastr(const char *dh)
{
    if (strcmp(dh, "1") == 0)
        return OSN_IPSEC_DH_1;
    else if (strcmp(dh, "2") == 0)
        return OSN_IPSEC_DH_2;
    else if (strcmp(dh, "5") == 0)
        return OSN_IPSEC_DH_5;
    else if (strcmp(dh, "14") == 0)
        return OSN_IPSEC_DH_14;
    else
        return OSN_IPSEC_DH_NOT_SET;
}

static int util_enc_array_from_schemastr(
        enum osn_ipsec_enc *enc_list, int enc_list_len,
        const char str_enc_list[OSN_CIPHER_SUITE_MAX][IPSEC_SCHEMA_CIPHERSTR_LEN], int str_enc_list_len)
{
    int i;

    for (i = 0; i < str_enc_list_len && i < enc_list_len; i++)
    {
        enc_list[i] = util_enc_from_schemastr(str_enc_list[i]);
        if (enc_list[i] == OSN_IPSEC_ENC_NOT_SET)
        {
            break;
        }

    }
    return i;
}

static int util_auth_array_from_schemastr(
        enum osn_ipsec_auth *auth_list, int auth_list_len,
        const char str_auth_list[OSN_CIPHER_SUITE_MAX][IPSEC_SCHEMA_CIPHERSTR_LEN], int str_auth_list_len)
{
    int i;

    for (i = 0; i < str_auth_list_len && i < auth_list_len; i++)
    {
        auth_list[i] = util_auth_from_schemastr(str_auth_list[i]);
        if (auth_list[i] == OSN_IPSEC_AUTH_NOT_SET)
        {
            break;
        }
    }
    return i;
}

static int util_dh_array_from_schemastr(
        enum osn_ipsec_dh_group *dh_list, int dh_list_len,
        const char str_dh_list[OSN_CIPHER_SUITE_MAX][IPSEC_SCHEMA_CIPHERSTR_LEN], int str_dh_list_len)
{
    int i;

    for (i = 0; i < str_dh_list_len && i < dh_list_len; i++)
    {
        dh_list[i] = util_dh_from_schemastr(str_dh_list[i]);
        if (dh_list[i] == OSN_IPSEC_DH_NOT_SET)
        {
            break;
        }
    }
    return i;
}

/* Initialize a supported cipher list from Kconfig */
static bool _util_supported_ciphers_init(
        int *supported_list,
        int (*str_to_enum_func)(const char *),
        const char *kconfig_list)
{
    char kconfig_list_buf[C_MAXPATH_LEN];
    char *tok_cipher;
    int cipher;
    int i;

    i = 0;
    supported_list[0] = OSN_IPSEC_ENC_NOT_SET;

    STRSCPY(kconfig_list_buf, kconfig_list);

    tok_cipher = strtok(kconfig_list_buf, ",");
    while (tok_cipher != NULL && i < OSN_CIPHER_SUITE_MAX)
    {
        cipher = str_to_enum_func(tok_cipher);
        if (cipher == OSN_IPSEC_ENC_NOT_SET)
        {
            LOG(ERR, "vpnm_ipsec: Invalid cipher string in Kconfig: %s", tok_cipher);
            return false;
        }

        supported_list[i++] = cipher;

        tok_cipher = strtok(NULL, ",");
    }
    supported_list[i] = OSN_IPSEC_ENC_NOT_SET;

    return true;
}

/* Initialize supported encryption algorithms from Kconfig */
static bool util_supported_ciphers_init_enc()
{
    return _util_supported_ciphers_init(
            (int *)supported_enc_list,
            (int (*)(const char *))util_enc_from_schemastr,
            CONFIG_OSN_IPSEC_SUPPORTED_CIPHERS_ENC);
}

/* Initialize supported authentication algorithms from Kconfig */
static bool util_supported_ciphers_init_auth()
{
    return _util_supported_ciphers_init(
            (int *)supported_auth_list,
            (int (*)(const char *))util_auth_from_schemastr,
            CONFIG_OSN_IPSEC_SUPPORTED_CIPHERS_AUTH);
}

/* Initialize supported Diffie-Hellman groups from Kconfig */
static bool util_supported_ciphers_init_dh()
{
    return _util_supported_ciphers_init(
            (int *)supported_dh_list,
            (int (*)(const char *))util_dh_from_schemastr,
            CONFIG_OSN_IPSEC_SUPPORTED_CIPHERS_DH);
}

/* Initialize all supported IPsec ciphers from Kconfig */
static bool vpnm_ipsec_supported_ciphers_init()
{
    if (!util_supported_ciphers_init_enc()
            || !util_supported_ciphers_init_auth()
            || !util_supported_ciphers_init_dh())
    {
        return false;
    }
    return true;
}

static bool _util_is_alg_in_set(int *set, int set_len, int alg)
{
    int i;

    for (i = 0; i < set_len; i++)
    {
        if (set[i] == alg)
        {
            return true;
        }
    }
    return false;
}

#define IS_ALG_IN_SET(set, set_len, alg) \
    _util_is_alg_in_set((int *)set, (int)set_len, (int)alg)

/*
 * Filter ciphers in 'cipher_list': Keep only ciphers that are also in the
 * 'supported_ciphers_list'. Additionally, the filtered ciphers list will
 * have ciphers in the same order as in the 'supported_ciphers_list'.
 * - cipher_list and cipher_list_len [in/out]
 *   (regardless of cipher_list_len the cipher_list array must have allocated
 *    space for at least OSN_CIPHER_SUITE_MAX elements)
 * - supported_cipher_list [in]
 */
static void _util_filter_supported_ciphers(
        int *ciphers_list,
        int *ciphers_list_len,
        int *supported_ciphers_list)
{
    int filtered_list[OSN_CIPHER_SUITE_MAX];
    int filtered_list_len = 0;
    int cipher;
    int i, j;

    /*
     * From the Kconfig-defined 'supported_ciphers_list' take those ciphers that
     * are in the configured 'ciphers_list' (and maintain the Kconfig-defined
     * order of ciphers). Special case: if the configured 'cipher_list_len' is
     * 0, then we unconditionally take all the ciphers from the 'supported_ciphers_list'.
     */
    j = 0;
    for (i = 0; i < OSN_CIPHER_SUITE_MAX && supported_ciphers_list[i] != OSN_IPSEC_ENC_NOT_SET; i++)
    {
        cipher = supported_ciphers_list[i];

        if (*ciphers_list_len == 0 || IS_ALG_IN_SET(ciphers_list, *ciphers_list_len, cipher))
        {
            filtered_list[j++] = cipher;
            filtered_list_len = j;
        }
    }

    /* Copy the filtered list to the input list (effectively filtering the input list): */
    for (i = 0; i < OSN_CIPHER_SUITE_MAX && i < filtered_list_len; i++)
    {
        ciphers_list[i] = filtered_list[i];
    }
    *ciphers_list_len = filtered_list_len;
}

/* Filter encryption algorithms according to Kconfig defined supported
 * encryption algorithms list. */
static void util_filter_supported_ciphers_enc(
        enum osn_ipsec_enc *enc_list,
        int *enc_list_len)
{
    TRACE();

    _util_filter_supported_ciphers((int *)enc_list, enc_list_len, (int *)supported_enc_list);

    if (*enc_list_len == 0)
    {
        LOG(WARN, "vpnm_ipsec: filter enc: The resulting list of ciphers empty. "
                "None of the configured encryption algorithms on the list of supported?");
    }
}

/* Filter authentication algorithms according to Kconfig defined supported
 * authentication algorithms list. */
static void util_filter_supported_ciphers_auth(
        enum osn_ipsec_auth *auth_list,
        int *auth_list_len)
{
    TRACE();

    _util_filter_supported_ciphers((int *)auth_list, auth_list_len, (int *)supported_auth_list);

    if (*auth_list_len == 0)
    {
        LOG(WARN, "vpnm_ipsec: filter auth: The resulting list of ciphers empty. "
                "None of the configured authentication algorithms on the list of supported?");
    }
}

/* Filter Diffie-Hellman groups according to Kconfig defined supported
 * Diffie-Hellman groups list. */
static void util_filter_supported_ciphers_dh(
        enum osn_ipsec_dh_group *dh_list,
        int *dh_list_len)
{
    TRACE();

    _util_filter_supported_ciphers((int *)dh_list, dh_list_len, (int *)supported_dh_list);

    if (*dh_list_len == 0)
    {
        LOG(WARN, "vpnm_ipsec: filter dh: The resulting list of ciphers empty. "
                "None of the configured Diffie-Hellman groups on the list of supported?");
    }
}

/*
 * Set IPsec config from OVSDB schema to OSN IPsec layer.
 *
 * true is returned if all configuration parameters set successfully
 */
static bool vpnm_ipsec_config_set(
        struct vpnm_ipsec *vpn_ipsec,
        ovsdb_update_monitor_t *mon,
        struct schema_IPSec_Config *old,
        const struct schema_IPSec_Config *new)
{
    osn_ipany_addr_t subnets[OSN_SUBNETS_MAX];
    osn_ipany_addr_t remote_endpoint_ip;
    enum osn_ipsec_enc enc_list[OSN_CIPHER_SUITE_MAX];
    enum osn_ipsec_auth auth_list[OSN_CIPHER_SUITE_MAX];
    enum osn_ipsec_dh_group dh_list[OSN_CIPHER_SUITE_MAX];
    char remote_endpoint_ip_str[OSN_IPANY_ADDR_LEN];
    int enc_len, auth_len, dh_len;
    int len;
    bool rv = true;

    /*
     * Remote endpoint can be configured as an IPv4 or IPv6 or FQDN. If it is a
     * FQDN we will resolve it to an IP address (v4 or v6 whichever is first).
     * DNS resolution is performed only when remote_endpoint changes and
     * if it is configured after the change.
     */
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(IPSec_Config, remote_endpoint))
            && new->remote_endpoint_exists)
    {
        /*
         * Resolve remote_endpoint. Accept either IPv4 or IPv6 (AF_UNSPEC).
         * If the remote endpoint is already an IP address it simply
         * "resolves" to the IP address.
         */
        if (vpnm_resolve(&remote_endpoint_ip, new->remote_endpoint, AF_UNSPEC))
        {
            /*
             * Remember the resolved IP address. Needed for reporting this
             * resolved IP later in IPSec_State. Controller may need this
             * resolved IP to later configure other parts, such as tunnel
             * interface's remote endpoint IP address to the same IP address
             * that was used to configure an IPsec tunnel.
             */
            vpn_ipsec->vi_remote_endpoint_ip = remote_endpoint_ip;
        }
        else
        {
            remote_endpoint_ip = IP4_ANY; // to indicate in _State DNS resolution failed
            vpn_ipsec->vi_remote_endpoint_ip = remote_endpoint_ip;

            LOG(ERROR, "vpnm_ipsec: %s: Error resolving to an IP address: %s",
                    vpn_ipsec->vi_name, new->remote_endpoint);
        }
    }
    else
    {
        /* Remote endpoint did not change or not set. Use the previous value. */
        remote_endpoint_ip = vpn_ipsec->vi_remote_endpoint_ip;
    }

    snprintf(
        remote_endpoint_ip_str,
        sizeof(remote_endpoint_ip_str),
        PRI_osn_ipany_addr,
        FMT_osn_ipany_addr(remote_endpoint_ip));

    if (osn_ipany_addr_cmp(&remote_endpoint_ip, &IP4_ANY) != 0
            && strcmp(remote_endpoint_ip_str, new->remote_endpoint) != 0)
    {
        // If it was actually an FQDN, i.e. if we actually resolved a name to an IP
        LOG(INFO, "vpnm_ipsec: %s: Resolved %s to %s", vpn_ipsec->vi_name,
                new->remote_endpoint, remote_endpoint_ip_str);
    }

    rv &= osn_ipsec_endpoints_set(vpn_ipsec->vi_ipsec, new->local_endpoint, remote_endpoint_ip_str);

    rv &= osn_ipsec_endpoint_ids_set(vpn_ipsec->vi_ipsec, new->local_endpoint_id, new->remote_endpoint_id);

    len = util_ipanny_addr_array_from_str(subnets, OSN_SUBNETS_MAX,
            new->local_subnets, new->local_subnets_len);
    if (len >= 0)  // len==0 will unset
    {
        rv &= osn_ipsec_local_subnet_set(vpn_ipsec->vi_ipsec, subnets, len);
    }

    len = util_ipanny_addr_array_from_str(subnets, OSN_SUBNETS_MAX,
            new->remote_subnets, new->remote_subnets_len);
    if (len >= 0)  // len==0 will unset
    {
        rv &= osn_ipsec_remote_subnet_set(vpn_ipsec->vi_ipsec, subnets, len);
    }

    len = util_ipanny_addr_array_from_str(subnets, OSN_SUBNETS_MAX,
            new->local_virt_ip, new->local_virt_ip_len);
    if (len >= 0)  // len==0 will unset
    {
        rv &= osn_ipsec_local_virtip_set(vpn_ipsec->vi_ipsec, subnets, len);
    }

    len = util_ipanny_addr_array_from_str(subnets, OSN_SUBNETS_MAX,
            new->remote_virt_ip, new->remote_virt_ip_len);
    if (len >= 0)  // len==0 will unset
    {
        rv &= osn_ipsec_remote_virtip_set(vpn_ipsec->vi_ipsec, subnets, len);
    }

    rv &= osn_ipsec_localremote_auth_set(
            vpn_ipsec->vi_ipsec,
            util_auth_mode_from_schemastr(new->local_auth_mode),
            util_auth_mode_from_schemastr(new->remote_auth_mode));

    rv &= osn_ipsec_localremote_auth2_set(
            vpn_ipsec->vi_ipsec,
            util_auth_mode_from_schemastr(new->local_auth_mode2),
            util_auth_mode_from_schemastr(new->remote_auth_mode2));

    rv &= osn_ipsec_psk_set(vpn_ipsec->vi_ipsec, new->psk);

    rv &= osn_ipsec_xauth_credentials_set(vpn_ipsec->vi_ipsec, new->xauth_user, new->xauth_pass);

    rv &= osn_ipsec_eap_identity_set(vpn_ipsec->vi_ipsec, new->eap_identity);

    rv &= osn_ipsec_eap_credentials_set(vpn_ipsec->vi_ipsec, new->eap_id, new->eap_secret);

    rv &= osn_ipsec_neg_mode_set(vpn_ipsec->vi_ipsec, util_neg_mode_from_schemastr(new->nego_mode));

    rv &= osn_ipsec_key_exchange_set(vpn_ipsec->vi_ipsec, util_keyexchange_from_schemastr(new->key_exchange));

    if (new->ike_lifetime_exists)
    {
        rv &= osn_ipsec_ike_lifetime_set(vpn_ipsec->vi_ipsec, new->ike_lifetime);
    }
    else
    {
        rv &= osn_ipsec_ike_lifetime_set(vpn_ipsec->vi_ipsec, OSN_IPSEC_IKELIFETIME_DEFAULT);
    }

    if (new->lifetime_exists)
    {
        rv &= osn_ipsec_lifetime_set(vpn_ipsec->vi_ipsec, new->lifetime);
    }
    else
    {
        rv &= osn_ipsec_lifetime_set(vpn_ipsec->vi_ipsec, OSN_IPSEC_LIFETIME_DEFAULT);
    }

    /* IKE enc/auth/dh: */
    enc_len = util_enc_array_from_schemastr(enc_list, OSN_CIPHER_SUITE_MAX, new->ike_enc_suite, new->ike_enc_suite_len);
    auth_len = util_auth_array_from_schemastr(auth_list, OSN_CIPHER_SUITE_MAX, new->ike_auth_suite, new->ike_auth_suite_len);
    dh_len = util_dh_array_from_schemastr(dh_list, OSN_CIPHER_SUITE_MAX, new->ike_dh_groups, new->ike_dh_groups_len);

    /* Before setting, filter-out any unsupported ciphers: */
    util_filter_supported_ciphers_enc(enc_list, &enc_len);
    util_filter_supported_ciphers_auth(auth_list, &auth_len);
    util_filter_supported_ciphers_dh(dh_list, &dh_len);

    rv &= osn_ipsec_ike_cipher_suite_set(vpn_ipsec->vi_ipsec, enc_list, enc_len, auth_list, auth_len, dh_list, dh_len);

    /* ESP enc/auth/dh: */
    enc_len = util_enc_array_from_schemastr(enc_list, OSN_CIPHER_SUITE_MAX, new->esp_enc_suite, new->esp_enc_suite_len);
    auth_len = util_auth_array_from_schemastr(auth_list, OSN_CIPHER_SUITE_MAX, new->esp_auth_suite, new->esp_auth_suite_len);
    dh_len = util_dh_array_from_schemastr(dh_list, OSN_CIPHER_SUITE_MAX, new->esp_dh_groups, new->esp_dh_groups_len);

    /* Before setting, filter-out any unsupported ciphers: */
    util_filter_supported_ciphers_enc(enc_list, &enc_len);
    util_filter_supported_ciphers_auth(auth_list, &auth_len);
    util_filter_supported_ciphers_dh(dh_list, &dh_len);

    rv &= osn_ipsec_esp_cipher_suite_set(vpn_ipsec->vi_ipsec, enc_list, enc_len, auth_list, auth_len, dh_list, dh_len);

    rv &= osn_ipsec_dpd_set(
            vpn_ipsec->vi_ipsec,
            new->dpd_delay_exists ? new->dpd_delay : OSN_IPSEC_DPD_DELAY_DEFAULT,
            new->dpd_timeout_exists ? new->dpd_timeout : OSN_IPSEC_DPD_TIMEOUT_DEFAULT,
            OSN_IPSEC_DPD_ACTION_DEFAULT);

    if (new->mark_exists)
    {
        if (OSN_IPSEC_IS_VALID_MARK(new->mark))
        {
            rv &= osn_ipsec_mark_set(vpn_ipsec->vi_ipsec, new->mark);
        }
        else
        {
            LOG(WARN, "vpnm_ipsec: Invalid mark value specified: %d. Setting ignored.", new->mark);
        }
    }
    else
    {
        rv &= osn_ipsec_mark_set(vpn_ipsec->vi_ipsec, 0);  /* mark==0: unset the mark */
    }

    return rv;
}

/*
 * Callback called by IPsec OSN layer when there's an IPsec tunnel status change.
 */
static void vpnm_ipsec_status_cb(const struct osn_ipsec_status *tunnel_status)
{
    struct schema_IPSec_State schema_ipsec_state;
    const osn_ipany_addr_t *ipany;
    int i;

    LOG(TRACE, "%s: %s: NEW state=%d, local_ts_len=%d, remote_ts_len=%d, local_virt_ip_len=%d",
            __func__, tunnel_status->is_tunnel_name, tunnel_status->is_conn_state,
            tunnel_status->is_local_ts_len, tunnel_status->is_remote_ts_len,
            tunnel_status->is_local_virt_ip_len);

    /* First, update IPSec_State: */
    memset(&schema_ipsec_state, 0, sizeof(schema_ipsec_state));
    schema_ipsec_state._partial_update = true;

    STRSCPY(schema_ipsec_state.tunnel_name, tunnel_status->is_tunnel_name);
    schema_ipsec_state.tunnel_name_exists = true;
    schema_ipsec_state.tunnel_name_present = true;

    /* Local traffic selectors: */
    for (i = 0; i < tunnel_status->is_local_ts_len; i++)
    {
        ipany = &tunnel_status->is_local_ts[i];

        if (ipany->addr_type == AF_INET)
        {
            SPRINTF(schema_ipsec_state.local_subnets[i],
                    PRI_osn_ip_addr, FMT_osn_ip_addr(ipany->addr.ip4));
            schema_ipsec_state.local_subnets_len++;
        }
        else if (ipany->addr_type == AF_INET6)
        {
            SPRINTF(schema_ipsec_state.local_subnets[i],
                    PRI_osn_ip6_addr, FMT_osn_ip6_addr(ipany->addr.ip6));
            schema_ipsec_state.local_subnets_len++;
        }
    }
    schema_ipsec_state.local_subnets_present = true;

    /* Remote traffic selectors: */
    for (i = 0; i < tunnel_status->is_remote_ts_len; i++)
    {
        ipany = &tunnel_status->is_remote_ts[i];

        if (ipany->addr_type == AF_INET)
        {
            SPRINTF(schema_ipsec_state.remote_subnets[i],
                    PRI_osn_ip_addr, FMT_osn_ip_addr(ipany->addr.ip4));
            schema_ipsec_state.remote_subnets_len++;
        }
        else if (ipany->addr_type == AF_INET6)
        {
            SPRINTF(schema_ipsec_state.remote_subnets[i],
                    PRI_osn_ip6_addr, FMT_osn_ip6_addr(ipany->addr.ip6));
            schema_ipsec_state.remote_subnets_len++;
        }
    }
    schema_ipsec_state.remote_subnets_present = true;

    /* Local virtual IPs assigned: */
    for (i = 0; i < tunnel_status->is_local_virt_ip_len; i++)
    {
        ipany = &tunnel_status->is_local_virt_ip[i];

        if (ipany->addr_type == AF_INET)
        {
            SPRINTF(schema_ipsec_state.local_virt_ip[i],
                    PRI_osn_ip_addr, FMT_osn_ip_addr(ipany->addr.ip4));
            schema_ipsec_state.local_virt_ip_len++;
        }
        else if (ipany->addr_type == AF_INET6)
        {
            SPRINTF(schema_ipsec_state.local_virt_ip[i],
                    PRI_osn_ip6_addr, FMT_osn_ip6_addr(ipany->addr.ip6));
            schema_ipsec_state.local_virt_ip_len++;
        }
    }
    schema_ipsec_state.local_virt_ip_present = true;

    if (!ovsdb_table_upsert_where(
            &table_IPSec_State,
            ovsdb_where_simple(SCHEMA_COLUMN(IPSec_State, tunnel_name), tunnel_status->is_tunnel_name),
            &schema_ipsec_state, false))
    {
        LOG(ERR, "vpnm_ipsec: %s: Error upserting IPSec_State", tunnel_status->is_tunnel_name);
    }

    /* Then, update the VPN tunnel state as well: */
    vpnm_tunnel_status_update(tunnel_status->is_tunnel_name, tunnel_status->is_conn_state);
}

/*
 * Apply IPsec config to the OSN IPsec layer.
 */
static bool vpnm_ipsec_apply(struct vpnm_ipsec *vpn_ipsec)
{
    if (vpnm_tunnel_is_enabled(vpn_ipsec->vi_name))
    {
        /* If this VPN is enabled, enable the OSN IPsec tunnel: */
        osn_ipsec_enable_set(vpn_ipsec->vi_ipsec, true);
        LOG(DEBUG, "vpnm_ipsec: %s: VPN_Tunnel enabled", vpn_ipsec->vi_name);

        /* Register for OSN IPsec tunnel/connection status change notifications: */
        if (!osn_ipsec_notify_status_set(vpn_ipsec->vi_ipsec, vpnm_ipsec_status_cb))
        {
            LOG(ERR, "vpnm_ipsec: %s: Error registering ipsec status change callback", vpn_ipsec->vi_name);
        }
    }
    else
    {
        /* If this VPN is disabled, disable the OSN IPsec tunnel: */
        osn_ipsec_enable_set(vpn_ipsec->vi_ipsec, false);
        LOG(DEBUG, "vpnm_ipsec: %s: VPN_Tunnel disabled", vpn_ipsec->vi_name);
    }

    /* Finally, apply IPsec OSN config to make configuration changes take effect: */
    return osn_ipsec_apply(vpn_ipsec->vi_ipsec);
}

/*
 * Callback called by VPN tunnel module when tunnel admin config changes.
 */
static void vpnm_tunnel_config_change_cb(const char *tunnel_name)
{
    struct vpnm_ipsec *vpn_ipsec;

    LOG(TRACE, "%s(), tunnel_name=%s", __func__, tunnel_name);

    vpn_ipsec = vpnm_ipsec_get(tunnel_name);
    if (vpn_ipsec != NULL)
    {
        LOG(TRACE, "%s(), tunnel_name=%s: found vpn_ipsec handle: call vpnm_ipsec_apply()", __func__, tunnel_name);

        if (!vpnm_ipsec_apply(vpn_ipsec))
        {
            LOG(ERR, "vpnm_ipsec: %s: Error applying tunnel config", tunnel_name);
        }
    }
}

/*
 * OVSDB monitor update callback for IPSec_Config
 */
void callback_IPSec_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_IPSec_Config *old,
        struct schema_IPSec_Config *new)
{
    struct vpnm_ipsec *vpn_ipsec;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            LOG(INFO, "vpnm_ipsec: %s: IPSec_Config update: NEW row", new->tunnel_name);

            if (!OSN_VPN_IS_VALID_TUNNEL_NAME(new->tunnel_name))
            {
                LOG(ERR, "vpnm_tunnel: %s: Invalid tunnel name", new->tunnel_name);
                return;
            }

            if (vpnm_ipsec_get(new->tunnel_name) != NULL)
            {
                LOG(WARN, "vpnm_ipsec: %s: A tunnel with the same name already exists. Ignoring.", new->tunnel_name);
                return;
            }

            vpn_ipsec = vpnm_ipsec_new(new->tunnel_name);

            /* Register this IPsec config in the generic VPN_Tunnel handling
             * module. Register a callback to be called when the tunnel
             * admin config changes. */
            vpnm_tunnel_cfg_register(new->tunnel_name, vpnm_tunnel_config_change_cb);

            break;

        case OVSDB_UPDATE_MODIFY:
            LOG(INFO, "vpnm_ipsec: %s: IPSec_Config update: MODIFY row", new->tunnel_name);

            vpn_ipsec = vpnm_ipsec_get(new->tunnel_name);

            break;

        case OVSDB_UPDATE_DEL:
            LOG(INFO, "vpnm_ipsec: %s: IPSec_Config update: DELETE row", new->tunnel_name);

            vpn_ipsec = vpnm_ipsec_get(new->tunnel_name);
            if (vpn_ipsec == NULL)
            {
                LOG(ERROR, "vpnm_ipsec: %s: Cannot delete ipsec tunnel: not found", new->tunnel_name);
                return;
            }

            if (!vpnm_ipsec_del(vpn_ipsec))
            {
                LOG(ERROR, "vpnm_ipsec: %s: Error deleting ipsec tunnel", new->tunnel_name);
            }

            /* When an IPSec_Config row is deleted, delete the corresponding
             * IPSec_State row as well: */
            vpnm_ipsec_ovsdb_state_delete(new->tunnel_name);

            /* Deregister this IPsec config in the generic VPN tunnel
             * handling module: */
            vpnm_tunnel_cfg_deregister(new->tunnel_name);
            return;

        default:
            LOG(ERROR, "vpnm_ipsec: Monitor update error.");
            return;
    }

    if (mon->mon_type == OVSDB_UPDATE_NEW ||
        mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        if (vpn_ipsec == NULL)
        {
            LOG(ERROR, "vpnm_ipsec: %s: Could not obtain ipsec handle", new->tunnel_name);
            return;
        }

        /* Set IPsec config from the schema values to OSN IPsec layer: */
        if (!vpnm_ipsec_config_set(vpn_ipsec, mon, old, new))
        {
            LOG(ERROR, "vpnm_ipsec: %s: Error setting IPSec configuration", new->tunnel_name);
            return;
        }

        /* After setting the IPsec config, apply it to make configuration
         * changes take effect: */
        if (!vpnm_ipsec_apply(vpn_ipsec))
        {
            LOG(ERROR, "vpnm_ipsec: %s: Error applying IPSec config.", new->tunnel_name);
            return;
        }
        LOG(NOTICE, "vpnm_ipsec: %s: applied IPSec config.", new->tunnel_name);

        /* IPSec config applied, do the initial IPSec_State upsert:
         *
         * Copy those parameters from IPSec_Config to IPSec_State that are
         * expected to be simply copied and set state parameters known at this
         * point. A few other parameters will then be set in IPSec_State later
         * according to the actual OSN IPsec layer status.
         */
        vpnm_ipsec_ovsdb_state_upsert(vpn_ipsec, new);
    }
}

#define VPNM_IPSEC_CONFIG_COPY(a, b) \
    do \
    { \
        if (b ## _exists) \
        { \
            C_STATIC_ASSERT(sizeof(a) == sizeof(b), #a " not equal in size to " #b); \
            memcpy(&a, &b, sizeof(a)); \
            a ## _exists = true; \
            a ## _present = true; \
        } \
    } while (0)

#define VPNM_IPSEC_CONFIG_COPY_ARRAY(a, b) \
    do \
    { \
        if (b ## _len > 0) \
        { \
            C_STATIC_ASSERT(sizeof(a) == sizeof(b), #a " not equal in size to " #b); \
            memcpy(&a, &b, sizeof(a)); \
            a ## _len = b ## _len; \
            a ## _present = true; \
        } \
    } while (0)


/*
 * From IPSec_Config copy to IPSec_State those parameters that are expected to
 * be simply copied and set some initial actual reported fields such as
 * the actual IP address used for tunnel remote endpoint.
 */
static bool vpnm_ipsec_ovsdb_state_upsert(
        struct vpnm_ipsec *vpn_ipsec,
        struct schema_IPSec_Config *ipsec_config)
{
    struct schema_IPSec_State ipsec_state;

    LOG(DEBUG, "vpnm_ipsec: %s: IPSec_State: initial upsert", ipsec_config->tunnel_name);

    memset(&ipsec_state, 0, sizeof(ipsec_state));
    ipsec_state._partial_update = true;

    STRSCPY(ipsec_state.tunnel_name, ipsec_config->tunnel_name);
    ipsec_state.tunnel_name_exists = true;
    ipsec_state.tunnel_name_present = true;

    /*
     * In IPSec_State->remote_endpoint we always report an actual IP address
     * that was used to configure the IPsec tunnel.
     *
     * If IPSec_Config->remote_endpoint was an FQDN we resolve it, configure
     * IPsec tunnel with it and report the address in IPSec_Sate.
     */
    snprintf(
        ipsec_state.remote_endpoint,
        sizeof(ipsec_state.remote_endpoint),
        PRI_osn_ipany_addr,
        FMT_osn_ipany_addr(vpn_ipsec->vi_remote_endpoint_ip));
    ipsec_state.remote_endpoint_exists = true;
    ipsec_state.remote_endpoint_present = true;

    VPNM_IPSEC_CONFIG_COPY(ipsec_state.remote_endpoint_id, ipsec_config->remote_endpoint_id);
    VPNM_IPSEC_CONFIG_COPY(ipsec_state.local_endpoint_id, ipsec_config->local_endpoint_id);
    VPNM_IPSEC_CONFIG_COPY(ipsec_state.local_endpoint, ipsec_config->local_endpoint);

    VPNM_IPSEC_CONFIG_COPY(ipsec_state.remote_auth_mode, ipsec_config->remote_auth_mode);
    VPNM_IPSEC_CONFIG_COPY(ipsec_state.local_auth_mode, ipsec_config->local_auth_mode);
    VPNM_IPSEC_CONFIG_COPY(ipsec_state.remote_auth_mode2, ipsec_config->remote_auth_mode2);
    VPNM_IPSEC_CONFIG_COPY(ipsec_state.local_auth_mode2, ipsec_config->local_auth_mode2);

    VPNM_IPSEC_CONFIG_COPY(ipsec_state.psk, ipsec_config->psk);
    VPNM_IPSEC_CONFIG_COPY(ipsec_state.xauth_user, ipsec_config->xauth_user);
    VPNM_IPSEC_CONFIG_COPY(ipsec_state.xauth_pass, ipsec_config->xauth_pass);
    VPNM_IPSEC_CONFIG_COPY(ipsec_state.eap_id, ipsec_config->eap_id);
    VPNM_IPSEC_CONFIG_COPY(ipsec_state.eap_secret, ipsec_config->eap_secret);
    VPNM_IPSEC_CONFIG_COPY(ipsec_state.eap_identity, ipsec_config->eap_identity);

    VPNM_IPSEC_CONFIG_COPY(ipsec_state.nego_mode, ipsec_config->nego_mode);
    VPNM_IPSEC_CONFIG_COPY(ipsec_state.key_exchange, ipsec_config->key_exchange);
    VPNM_IPSEC_CONFIG_COPY(ipsec_state.ike_lifetime, ipsec_config->ike_lifetime);
    VPNM_IPSEC_CONFIG_COPY(ipsec_state.lifetime, ipsec_config->lifetime);
    VPNM_IPSEC_CONFIG_COPY(ipsec_state.protocol, ipsec_config->protocol);

    VPNM_IPSEC_CONFIG_COPY_ARRAY(ipsec_state.ike_enc_suite, ipsec_config->ike_enc_suite);
    VPNM_IPSEC_CONFIG_COPY_ARRAY(ipsec_state.ike_auth_suite, ipsec_config->ike_auth_suite);
    VPNM_IPSEC_CONFIG_COPY_ARRAY(ipsec_state.ike_dh_groups, ipsec_config->ike_dh_groups);

    VPNM_IPSEC_CONFIG_COPY_ARRAY(ipsec_state.esp_enc_suite, ipsec_config->esp_enc_suite);
    VPNM_IPSEC_CONFIG_COPY_ARRAY(ipsec_state.esp_auth_suite, ipsec_config->esp_auth_suite);
    VPNM_IPSEC_CONFIG_COPY_ARRAY(ipsec_state.esp_dh_groups, ipsec_config->esp_dh_groups);

    VPNM_IPSEC_CONFIG_COPY(ipsec_state.dpd_delay, ipsec_config->dpd_delay);
    VPNM_IPSEC_CONFIG_COPY(ipsec_state.dpd_timeout, ipsec_config->dpd_timeout);
    VPNM_IPSEC_CONFIG_COPY(ipsec_state.mark, ipsec_config->mark);

    if (!ovsdb_table_upsert_where(
            &table_IPSec_State,
            ovsdb_where_simple(SCHEMA_COLUMN(IPSec_State, tunnel_name), ipsec_config->tunnel_name),
            &ipsec_state, false))
    {
        LOG(ERR, "vpnm_ipsec: %s: Error upserting IPSec_State", ipsec_config->tunnel_name);
        return false;
    }

    return true;
}

/*
 * Delete an IPSec_State row for the specified tunnel.
 */
static bool vpnm_ipsec_ovsdb_state_delete(const char *tunnel_name)
{
    if (ovsdb_table_delete_where(
            &table_IPSec_State,
            ovsdb_where_simple(SCHEMA_COLUMN(IPSec_State, tunnel_name), tunnel_name)) == -1)
    {
        LOG(ERR, "vpnm_ipsec: %s: Error deleting IPSec_State row", tunnel_name);
        return false;
    }

    return true;
}

bool vpnm_ipsec_init(void)
{
    LOG(INFO, "Initializing VPNM IPSec OVSDB");

    OVSDB_TABLE_INIT(IPSec_Config, tunnel_name);
    OVSDB_TABLE_INIT(IPSec_State, tunnel_name);

    OVSDB_TABLE_MONITOR(IPSec_Config, false);

    if (!vpnm_ipsec_supported_ciphers_init())
    {
        LOG(ERR, "vpnm_ipsec: Error initializing supported ciphers from Kconfig.");
        return false;
    }

    return true;
}


/*
 * Static assertions for utility functions with 2-D array function parameters
 * accepting 2-D arrays from schema structs.
 */

C_STATIC_ASSERT((OSN_SUBNETS_MAX) ==
        (sizeof(((struct schema_IPSec_Config *)(NULL))->local_subnets)
                / sizeof(((struct schema_IPSec_Config *)(NULL))->local_subnets[0])),
                "unexpected schema subnets array len");

C_STATIC_ASSERT((OSN_CIPHER_SUITE_MAX) ==
        (sizeof(((struct schema_IPSec_Config *)(NULL))->ike_enc_suite)
                / sizeof(((struct schema_IPSec_Config *)(NULL))->ike_enc_suite[0])),
                "unexpected schema subnets array len");

/* All of the following sizeofs are expected to be IPSEC_SCHEMA_IPSTR_LEN */
C_STATIC_ASSERT(sizeof(((struct schema_IPSec_Config *)(NULL))->local_subnets[0])
                    == sizeof(((struct schema_IPSec_Config *)(NULL))->remote_subnets[0]),
                    "unexpected schema subnets string size");

C_STATIC_ASSERT(sizeof(((struct schema_IPSec_Config *)(NULL))->local_subnets[0])
                    == sizeof(((struct schema_IPSec_Config *)(NULL))->local_virt_ip[0]),
                    "unexpected schema subnets string size");

C_STATIC_ASSERT(sizeof(((struct schema_IPSec_Config *)(NULL))->local_subnets[0])
                    == sizeof(((struct schema_IPSec_Config *)(NULL))->remote_virt_ip[0]),
                    "unexpected schema subnets string size");

/* All of the following sizeofs are expected to be IPSEC_SCHEMA_CIPHERSTR_LEN */
C_STATIC_ASSERT((sizeof(((struct schema_IPSec_Config *)(NULL))->ike_enc_suite[0]))
                    == (sizeof(((struct schema_IPSec_Config *)(NULL))->ike_auth_suite[0])),
                    "unexpected schema subnets string size");

C_STATIC_ASSERT((sizeof(((struct schema_IPSec_Config *)(NULL))->ike_enc_suite[0]))
                    == (sizeof(((struct schema_IPSec_Config *)(NULL))->ike_dh_groups[0])),
                    "unexpected schema subnets string size");

C_STATIC_ASSERT((sizeof(((struct schema_IPSec_Config *)(NULL))->ike_enc_suite[0]))
                    == (sizeof(((struct schema_IPSec_Config *)(NULL))->esp_enc_suite[0])),
                    "unexpected schema subnets string size");

C_STATIC_ASSERT((sizeof(((struct schema_IPSec_Config *)(NULL))->ike_enc_suite[0]))
                    == (sizeof(((struct schema_IPSec_Config *)(NULL))->esp_auth_suite[0])),
                    "unexpected schema subnets string size");

C_STATIC_ASSERT((sizeof(((struct schema_IPSec_Config *)(NULL))->ike_enc_suite[0]))
                    == (sizeof(((struct schema_IPSec_Config *)(NULL))->esp_dh_groups[0])),
                    "unexpected schema subnets string size");
