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

#ifndef OSN_IPSEC_H_INCLUDED
#define OSN_IPSEC_H_INCLUDED

#include <stdbool.h>

#include "const.h"
#include "osn_types.h"
#include "osn_vpn.h"

/**
 * @file osn_ipsec.h
 *
 * @brief OpenSync IPsec API
 *
 * @addtogroup OSN
 * @{
 *
 * @addtogroup OSN_VPN
 * @{
 *
 * @defgroup OSN_IPSEC OpenSync IPsec APIs
 *
 * OpenSync IPsec APIs
 *
 * @{
 */

/*
 * ===========================================================================
 * OpenSync IPsec API
 * ===========================================================================
 */

/** Maximum number of subnets that can be configured per local or per remote.
 * This limit also applies for virtual IPs. */
#define OSN_SUBNETS_MAX                 32

/** Maximum number of cipher suites per proposal per suite type (e.g. encryption,
 * authentication/integrity, Diffie-Hellman groups) */
#define OSN_CIPHER_SUITE_MAX            8

/** Is the specified mark valid? Some values may be reserved by the implementation */
#define OSN_IPSEC_IS_VALID_MARK(mark) (mark != 0)  // 0 is OpenSync reserved

/** Default Dead Peer Detection (DPD) delay (seconds), if not set */
#define OSN_IPSEC_DPD_DELAY_DEFAULT     30

/** Default Dead Peer Detection (DPD) timeout (seconds), if not set */
#define OSN_IPSEC_DPD_TIMEOUT_DEFAULT   150

/** Default Dead Peer Detection (DPD) action, if not explicitly set */
#define OSN_IPSEC_DPD_ACTION_DEFAULT    OSN_IPSEC_DPD_RESTART

/** Default ikelifetime if not set by @ref osn_ipsec_ike_lifetime_set() */
#define OSN_IPSEC_IKELIFETIME_DEFAULT   10800

/** Default lifetime if not set by @ref osn_ipsec_lifetime_set() */
#define OSN_IPSEC_LIFETIME_DEFAULT      3600

/**
 * OSN IPsec object type.
 *
 * This is an opaque type. The actual structure implementation is hidden as
 * it depends on the platform.
 *
 * A new instance of the object can be obtained by calling
 * @ref osn_ipsec_new() and must be destroyed using @ref
 * osn_ipsec_del().
 */
typedef struct osn_ipsec osn_ipsec_t;

/**
 * IPsec negotiation mode.
 */
enum osn_ipsec_neg_mode
{
    OSN_IPSEC_NEG_MAIN,               /** Main mode (default) */
    OSN_IPSEC_NEG_AGGRESSIVE          /** IKEv1 Aggressive mode */
};

/**
 * IPsec authentication mode.
 */
enum osn_ipsec_auth_mode
{
    OSN_IPSEC_AUTHMODE_NOT_SET,
    OSN_IPSEC_AUTHMODE_PSK,           /** Pre-shared key authentication */
    OSN_IPSEC_AUTHMODE_PUBKEY,        /** Public key encryption (RSA/ECDSA) */
    OSN_IPSEC_AUTHMODE_XAUTH,         /** IKEv1 eXtended Authentication */
    OSN_IPSEC_AUTHMODE_EAP_MSCHAPv2,  /** Extensible Authentication Protocol EAP method mschapv2 */
    OSN_IPSEC_AUTHMODE_MAX
};

/**
 * IPsec method of key exchange; which protocol should be used to initialize
 * the connection.
 */
enum osn_ipsec_key_exchange
{
    OSN_IPSEC_KEY_EXCHANGE_IKE,       /** IKEv1 or IKEv2: use IKEv2 when initiating,
                                       *  but accept any protocol version when responding. */
    OSN_IPSEC_KEY_EXCHANGE_IKEv1,     /** IKEv1 */
    OSN_IPSEC_KEY_EXCHANGE_IKEv2      /** IKEv2 -- recommended */
};

/**
 * IPsec encryption algorithm.
 */
enum osn_ipsec_enc
{
    OSN_IPSEC_ENC_NOT_SET,
    OSN_IPSEC_ENC_3DES,              /** 168 bit 3DES-EDE-CBC */
    OSN_IPSEC_ENC_AES128,            /** 256 bit AES-CBC */
    OSN_IPSEC_ENC_AES192,            /** 192 bit AES-CBC */
    OSN_IPSEC_ENC_AES256,            /** 256 bit AES-CBC */
    OSN_IPSEC_ENC_MAX

};

/**
 * IPsec integrity algorithms
 */
enum osn_ipsec_auth
{
    OSN_IPSEC_AUTH_NOT_SET,
    OSN_IPSEC_AUTH_SHA1,             /** SHA1 HMAC (96 bit) */
    OSN_IPSEC_AUTH_MD5,              /** MD5 HMAC (96 bit) */
    OSN_IPSEC_AUTH_SHA256,           /** SHA2_256_128 HMAC (128 bit) */
    OSN_IPSEC_AUTH_MAX
};

/**
 * Diffie-Hellman groups.
 */
enum osn_ipsec_dh_group
{
    OSN_IPSEC_DH_NOT_SET,
    OSN_IPSEC_DH_1,                  /** DH group 1, 768 bits */
    OSN_IPSEC_DH_2,                  /** DH group 2, 1024 bits */
    OSN_IPSEC_DH_5,                  /** DH group 5, 1536 bits */
    OSN_IPSEC_DH_14,                 /** DH group 14, 2048 bits */
    OSN_IPSEC_DH_MAX
};

/**
 * Dead Peer Detection protocol action to take when timeout checking on
 * liveliness of the IPsec peer expires.
 *
 * The OpenSync default DPD action is OSN_IPSEC_DPD_RESTART.
 */
enum osn_ipsec_dpd_action
{
    OSN_IPSEC_DPD_NONE,             /** Disables the active sending of DPD messages */
    OSN_IPSEC_DPD_CLEAR,            /** Close the connection, take no further actions */
    OSN_IPSEC_DPD_HOLD,             /** Installs a trap policy, which will catch matching
                                     *  traffic and try to re-negotiate the connection on demand */
    OSN_IPSEC_DPD_RESTART,          /** Immediately trigger an attempt to re-negotiate the connection */
    OSN_IPSEC_DPD_MAX
};

/**
 * IPsec connection encapsulation mode
 */
enum osn_ipsec_mode
{
    OSN_IPSEC_MODE_TUNNEL,          /** IPsec tunnel mode, default */
    OSN_IPSEC_MODE_TRANSPORT,       /** IPsec transport mode */
    OSN_IPSEC_MODE_MAX
};

/**
 * IPsec peer role.
 *
 * OpenSync default role is initiator.
 */
enum osn_ipsec_role
{
    OSN_IPSEC_ROLE_INITIATOR,       /** Initiator */
    OSN_IPSEC_ROLE_RESPONDER,       /** Responder */
    OSN_IPSEC_ROLE_MAX
};

/**
 * IPsec status report.
 */
struct osn_ipsec_status
{
    /** IPSec tunnel name for which this report is for */
    char                            is_tunnel_name[C_MAXPATH_LEN];

    /** IPsec connection (phase1 and phase 2 combined) state: */
    enum osn_vpn_conn_state         is_conn_state;

    /**
     * Established traffic selectors:
     *
     * @note a traffic selectors is usually a subnet, but may be an IP as well
     * (/32 for IPv4 or /128 for IPv6).
     */
    osn_ipany_addr_t                is_local_ts[OSN_SUBNETS_MAX];   /** local traffic selectors */
    int                             is_local_ts_len;                /** number of local traffic selectors */
    osn_ipany_addr_t                is_remote_ts[OSN_SUBNETS_MAX];  /** remote traffic selectors */
    int                             is_remote_ts_len;               /** number of remote traffic selectors */

    /**
     * Assigned virtual IPs:
     * @note an assigned virtual IP will usually be used as a local traffic
     * selector so it will probably be reported in the list of local TSs
     * as well.
     */
    osn_ipany_addr_t                is_local_virt_ip[OSN_SUBNETS_MAX];  /** virtual IPs assigned for this peer */
    int                             is_local_virt_ip_len;               /** number of virtual IPs assigned */
};

/**
 * IPsec notification callback function type.
 *
 * @param[in] tunnel_status    New IPsec tunnel status report.
 *
 * See @ref struct osn_ipsec_status
 */
typedef void osn_ipsec_status_fn_t(const struct osn_ipsec_status *tunnel_status);

/**
 * Create a new instance of osn_ipsec_t object.
 *
 * @param[in] tunnel_name   a name for this tunnel. Must be unique.
 *
 * @return a valid osn_ipsec_t object or NULL on error.
 */
osn_ipsec_t *osn_ipsec_new(const char *tunnel_name);

/**
 * Set endpoints IP addresses (or FQDNs) of both participants' public interfaces.
 *
 * @param[in] self             A valid @ref osn_ipsec_t object
 *
 * @param[in] local_endpoint   Local endpoint WAN interface IP address string.
 *                             Note: This may well be different then the node's
 *                             public IP address if a NAT is inbetween.
 *
 *                             If set to NULL then the IP address for the local
 *                             endpoint will be filled in automatically with the
 *                             local address of the default-route interface.
 *                             (recommended).
 *
 * @param[in] remote_endpoint  Remote endpoint WAN interface IP address or FQDN.
 *
 * @return true on success
 */
bool osn_ipsec_endpoints_set(osn_ipsec_t *self, const char *local_endpoint, const char *remote_endpoint);

/**
 * Set local and remote endpoints identifications.
 *
 * An IPsec identification is how the participant should be identified for
 * authentication;
 *
 * @param[in] self                A valid @ref osn_ipsec_t object
 * @param[in] local_endpoint_id   Local endpoint identification. Usually its public
 *                                IP address or FQDN. Optional if the remote peer
 *                                does not perform an ID check. If not set, defaults
 *                                to local_endpoint.
 * @param[in] remote_endpoint_id  Remote endpoint identification. Usually its public
 *                                IP or FQDN. Optional; if not set defaults to
 *                                remote_endpoint.
 *
 * @return true on success
 */
bool osn_ipsec_endpoint_ids_set(osn_ipsec_t *self, const char *local_endpoint_id, const char *remote_endpoint_id);

/**
 * Set local subnets.
 *
 * @param[in] self          A valid @ref osn_ipsec_t object
 * @param[in] subnets       Local subnets to be routable through the IPsec tunnel.
 *                          These will be used as (proposed) traffic selectors.
 * @param[in] subnets_len   Number of subnets. Setting this to 0 will ignore other
 *                          parameters and will clear any previously set subnets.
 *
 * @return true on success
 */
bool osn_ipsec_local_subnet_set(osn_ipsec_t *self, osn_ipany_addr_t *subnets, int subnets_len);

/**
 * Set remote subnets.
 *
 * @param[in] self          A valid @ref osn_ipsec_t object
 * @param[in] subnets       Subnets at remote site to be reachable through the IPsec tunnel.
 *                          These will be used as (proposed) traffic selectors.
 * @param[in] subnets_len   Number of subnets. Setting this to 0 will ignore other
 *                          parameters and will clear any previously set subnets.
 *
 * @return true on success
 */
bool osn_ipsec_remote_subnet_set(osn_ipsec_t *self, osn_ipany_addr_t *subnets, int subnets_len);

/**
 * Set the internal local source IP to use in a tunnel, also known as virtual IP.
 *
 * Special value 0.0.0.0 for IPv4 or :: for IPv6: source-ip-config: source IP
 * is configured at the remote. If an initiator sets source-ip-config source IP
 * is requested from the responder. If source-ip-config is configured at responder
 * side then an initiator must propose an address which is then echoed back.
 *
 * The most typical example usage: as an initiator set local source ip to 0.0.0.0
 * to request IPv4 virtual IP address from the remote peer.
 *
 * @param[in] self         A valid @ref osn_ipsec_t object
 * @param[in] virtip       A pointer to virtual IP specification
 * @param[in] virtip_len   Number of virtual IPs specifications in @ref virtip array.
 *                         Setting this to 0 will ignore other parameters and will
 *                         clear any previously set virtual IPs.
 *
 * @return true on success
 */
bool osn_ipsec_local_virtip_set(osn_ipsec_t *self, osn_ipany_addr_t *virtip, int virtip_len);

/**
 * The internal source IP (virtual IP) to use in a tunnel for the remote peer.
 *
 * @see osn_ipsec_local_virtip_set()
 *
 * @param[in] self         A valid @ref osn_ipsec_t object
 * @param[in] virtip       A pointer to virtual IP specification for the remote peer.
 * @param[in] virtip_len   Number of virtual IPs specifications in @ref virtip array.
 *                         Setting this to 0 will ignore other parameters and will
 *                         clear any previously set virtual IPs.
 *
 * @return true on success
 *
 */
bool osn_ipsec_remote_virtip_set(osn_ipsec_t *self, osn_ipany_addr_t *virtip, int virtip_len);

/**
 * Set authentication method to use locally and authentication method to
 * require from the remote.
 *
 * @see enum osn_ipsec_auth_mode
 *
 * @param[in] self        A valid @ref osn_ipsec_t object
 * @param[in] leftauth    Authentication method to use locally
 * @param[in] rightauth   Authentication method to require from the remote
 *
 * @return true on success
 */
bool osn_ipsec_localremote_auth_set(
        osn_ipsec_t *self,
        enum osn_ipsec_auth_mode leftauth,
        enum osn_ipsec_auth_mode rightauth);

/**
 * Optionally define authentication methods for an additional second
 * authentication round.
 *
 * @see enum osn_ipsec_auth_mode
 *
 * @param[in] self       A valid @ref osn_ipsec_t object
 * @param[in] leftauth   Authentication method to use locally for the second
 *                       authentication round.
 * @param[in] rightauth  Authentication method to require from the remote for the
 *                       second authentication round.
 *
 * @return true on success
 */
bool osn_ipsec_localremote_auth2_set(
        osn_ipsec_t *self,
        enum osn_ipsec_auth_mode leftauth2,
        enum osn_ipsec_auth_mode rightauth2);

/**
 * Set preshared key if authentication mode is @ref OSN_IPSEC_AUTHMODE_PSK.
 *
 * @param[in] self      A valid @ref osn_ipsec_t object
 * @param[in] psk       Preshared key string. NULL or empty string to unset.
 *
 * @return true on success
 */
bool osn_ipsec_psk_set(osn_ipsec_t *self, const char *psk);

/**
 * Set XAUTH credentials if authentication mode is @ref OSN_IPSEC_AUTHMODE_XAUTH.
 *
 * @param[in] self         A valid @ref osn_ipsec_t object
 * @param[in] xauth_user   XAUTH credentials username. NULL or empty string to unset.
 * @param[in] xauth_pass   XAUTH credentials password. NULL or empty string to unset.
 *
 * @return true on success
 */
bool osn_ipsec_xauth_credentials_set(
        osn_ipsec_t *self,
        const char *xauth_user,
        const char *xauth_pass);

/**
 * Set EAP identity.
 *
 * Defines the identity the client uses to reply to an EAP Identity request.
 *
 * If not defined, the IKEv2 identity (local_endpoint_id --
 * @ref osn_ipsec_endpoint_ids_set()) will be used as EAP identity.
 *
 * @param[in] self          A valid @ref osn_ipsec_t object
 * @param[in] eap_identity  The EAP identity to use to reply to an EAP identity request.
 *                          The special value "%identity": use the EAP Identity
 *                          method to ask the client for a EAP identity.
 *                          Set to NULL or empty string to unset.
 */
bool osn_ipsec_eap_identity_set(osn_ipsec_t *self, const char *eap_identity);

/**
 * Set EAP credentials if authentication mode is one of @ref OSN_IPSEC_AUTHMODE_EAP_*,
 * (for instance @ref OSN_IPSEC_AUTHMODE_EAP_MSCHAPv2).
 *
 * @param[in] self         A valid @ref osn_ipsec_t object
 * @param[in] eap_id       EAP user id. NULL or empty string to unset.
 * @param[in] eap_secret   EAP secret. NULL or empty string to unset.
 *
 * @return true on success
 */
bool osn_ipsec_eap_credentials_set(osn_ipsec_t *self, const char *eap_id, const char *eap_secret);

/**
 * Set IPsec negotiation mode.
 *
 * The default is @ref OSN_IPSEC_NEG_MAIN
 *
 * @param[in] self        A valid @ref osn_ipsec_t object
 * @param[in] neg_mode    IPsec negotiation mode.
 *
 * @return true on success
 */
bool osn_ipsec_neg_mode_set(osn_ipsec_t *self, enum osn_ipsec_neg_mode neg_mode);

/**
 * Set IPsec method of key exchange.
 *
 * @see enum osn_ipsec_key_exchange
 *
 * The default is @ref OSN_IPSEC_KEY_EXCHANGE_IKE
 *
 * @param[in] self            A valid @ref osn_ipsec_t object
 * @param[in] key_exchange    IPsec method of key exchange to use
 *
 * @return true on success
 */
bool osn_ipsec_key_exchange_set(osn_ipsec_t *self, enum osn_ipsec_key_exchange key_exchange);

/**
 * Set ikelifetime -- how long the keying channel of a connection
 * (ISAKMP or IKE SA) should last before being renegotiated.
 *
 * Default is @ref OSN_IPSEC_IKELIFETIME_DEFAULT
 *
 * @param[in] self            A valid @ref osn_ipsec_t object
 * @param[in] ike_lifetime    IKE lifetime to set
 *
 * @return true on success
 */
bool osn_ipsec_ike_lifetime_set(osn_ipsec_t *self, int ike_lifetime);

/**
 * Set lifetime -- How long a particular instance of a connection (a set of
 * encryption/authentication keys for user packets) should last, from
 * successful negotiation to expiry.
 *
 * Default is @ref OSN_IPSEC_LIFETIME_DEFAULT
 *
 * @param[in] self           A valid @ref osn_ipsec_t object
 * @param[in] lifetime       lifetime to set
 *
 * @return true on success
 */
bool osn_ipsec_lifetime_set(osn_ipsec_t *self, int lifetime);

/**
 * Set this (local) endpoint IPsec role.
 *
 * Note: if not set, the default role is @ref OSN_IPSEC_ROLE_INITIATOR
 *
 * @param[in] self        A valid @ref osn_ipsec_t object
 * @param[in] role        Role for this local endpoint: initiator or responder
 *
 * @return true on success
 */
bool osn_ipsec_role_set(osn_ipsec_t *self, enum osn_ipsec_role role);

/**
 * Set IPsec connection encapsulation mode.
 *
 * Note: if not set, the default mode is @ref OSN_IPSEC_MODE_TUNNEL.
 *
 * @param[in] self        A valid @ref osn_ipsec_t object
 * @param[in] mode        IPsec mode: tunnel or transport
 *
 * @return true on success
 */
bool osn_ipsec_mode_set(osn_ipsec_t *self, enum osn_ipsec_mode mode);

/**
 * Set algorithms for building a cipher suite for IKE Phase 1.
 *
 * In IKEv2 multiple algorithms may be included.
 *
 * @note The parameters have _set prefix, however the order of the algorithms in
 *       arrays may be important, i.e. in fact these are lists. Depending on
 *       the backend implementation the order may be taken into account.
 *
 * @param[in] self             A valid @ref osn_ipsec_t object
 * @param[in] enc_set          List of Encryption Algorithms for IKE Phase 1
 * @param[in] enc_set_len      Number of encryption algorithms
 * @param[in] auth_set         List of Authentication/Integrity Algorithms for IKE Phase 1
 * @param[in] auth_set_len     Number of auth algorithms
 * @param[in] dh_set           List of Diffie-Hellman (DH) groups for IKE Phase 1
 * @param[in] dh_set_len       Number of DH groups
 *
 * @note Setting any of *_len parameters to 0 will ignore the corresponding array
 *       parameter and unset any previously set values for that type of cipher suites.
 *
 * @return true on success
 */
bool osn_ipsec_ike_cipher_suite_set(
        osn_ipsec_t *self,
        enum osn_ipsec_enc *enc_set, int enc_set_len,
        enum osn_ipsec_auth *auth_set, int auth_set_len,
        enum osn_ipsec_dh_group *dh_set, int dh_set_len);

/**
 * Set algorithms for building a cipher suite for Phase 2 ESP tunnel.
 *
 * @note The parameters have _set prefix, however the order of the algorithms in
 *       arrays may be important, i.e. in fact these are lists. Depending on
 *       the backend implementation the order may be taken into account.
 *
 * @param[in] self             A valid @ref osn_ipsec_t object
 * @param[in] enc_set          List of Encryption Algorithms for Phase 2 ESP tunnel
 * @param[in] enc_set_len      Number of encryption algorithms
 * @param[in] auth_set         List of Authentication/Integrity Algorithms for Phase 2 ESP tunnel
 * @param[in] auth_set_len     Number of auth algorithms
 * @param[in] dh_set           List of Diffie-Hellman (DH) groups for Phase 2 ESP tunnel
 * @param[in] dh_set_len       Number of DH groups
 *
 * @note Setting any of *_len parameters to 0 will ignore the corresponding array
 *       parameter and unset any previously set values for that type of cipher suites.
 *
 * @return true on success
 */
bool osn_ipsec_esp_cipher_suite_set(
        osn_ipsec_t *self,
        enum osn_ipsec_enc *enc_set, int enc_set_len,
        enum osn_ipsec_auth *auth_set, int auth_set_len,
        enum osn_ipsec_dh_group *dh_set, int dh_set_len);

/**
 * Set Dead Peer Detection parameters.
 *
 * The OpenSync default DPD action is @ref OSN_IPSEC_DPD_RESTART.
 *
 * @param[in] self           A valid @ref osn_ipsec_t object
 * @param[in] dpd_delay      Interval of keepalive message to be sent when tunnel is idle.
 *                           The default is @ref OSN_IPSEC_DPD_DELAY_DEFAULT.
 * @param[in] dpd_timeout    Timeout interval, after which all connections to
 *                           a peer are deleted in case of inactivity.
 *                           The default is @ref OSN_IPSEC_DPD_TIMEOUT_DEFAULT.
 * @param[in] dpd_action     Action to take when timeout checking on
 *                           liveliness of the IPsec peer expires.
 *                           The default is @ref OSN_IPSEC_DPD_ACTION_DEFAULT.
 *
 * @return true on success
 */
bool osn_ipsec_dpd_set(
        osn_ipsec_t *self,
        int dpd_delay,
        int dpd_timeout,
        enum osn_ipsec_dpd_action dpd_action);

/**
 * Set XFRM mark for this IPSec tunnel.
 *
 * @param[in] self           A valid @ref osn_ipsec_t object
 * @param[in] mark           Mark to set.
 *                           Value 0 is reserved and will unset the mark.
 *
 * @return true on success
 */
bool osn_ipsec_mark_set(osn_ipsec_t *self, int mark);

/**
 * Enable or disable this tunnel.
 *
 * @param[in] self           A valid @ref osn_ipsec_t object
 * @param[in] enable         Enable or disable
 *
 * @return true on success
 */
bool osn_ipsec_enable_set(osn_ipsec_t *self, bool enable);

/**
 * Set notification callback function to be called whenever this IPsec tunnel
 * status changes.
 *
 * See @ref struct osn_ipsec_status.
 *
 * @param[in] self           A valid @ref osn_ipsec_t object
 * @param[in] status_fn_cb   IPsec status update function callback pointer or
 *                           NULL to unset.
 *
 * @return true on success
 */
bool osn_ipsec_notify_status_set(osn_ipsec_t *self, osn_ipsec_status_fn_t *status_fn_cb);

/**
 * Apply the IPsec tunnel configuration parameters to the running system.
 *
 * After you set IPsec tunnel configuration parameters you must call this
 * function to ensure the configuration takes effect.
 *
 * Note: It depends on the underlying backend and implementation how this is
 * handled. Often there's a single config file for all the configured tunnels
 * and after a config file change a deamon needs to be restarted.
 *
 * @param[in] self           A valid @ref osn_ipsec_t object
 *
 * @return true on success
 */
bool osn_ipsec_apply(osn_ipsec_t *self);

/**
 * Destroy a valid osn_ipsec_t object.
 *
 * If an IPsec tunnel was configured and up, it will be deconfigured and stopped.
 *
 * @param[in] self          A valid @ref osn_ipsec_t object
 *
 * @return true on success. Regardless of the return value after this function
 *         returns the input parameter should be considered invalid and must
 *         no longer be used.
 */
bool osn_ipsec_del(osn_ipsec_t *self);

/** @} OSN_IPSEC */
/** @} OSN_VPN */
/** @} OSN */

#endif /* OSN_IPSEC_H_INCLUDED */
