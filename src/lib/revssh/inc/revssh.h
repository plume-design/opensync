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

#ifndef REVSSH_H_INCLUDED
#define REVSSH_H_INCLUDED

#include <stdbool.h>

#include "const.h"
#include "osn_types.h"

/**
 * @file revssh.h
 *
 * @brief OpenSync Reverse SSH API
 *
 * @defgroup REVSSH OpenSync Reverse SSH APIs
 *
 * OpenSync Reverse SSH API
 *
 * @{
 */

#define REVSSH_DEFAULT_SERVER_PORT              22

#define REVSSH_DEFAULT_REMOTE_BIND_ADDR         "127.0.0.1"
#define REVSSH_DEFAULT_LOCAL_ADDR               "127.0.0.1"
#define REVSSH_DEFAULT_LOCAL_PORT               30022

/**
 *  Max number of different public keys that can
 *  be added to temporary authorized_keys:
 */
#define REVSSH_AUTHORIZED_KEY_MAX               4

#define REVSSH_CLIENT_AUTH_RETRY_INTERVAL       5          /* seconds */
#define REVSSH_CLIENT_AUTH_RETRY_MAX            (3*12)

#define REVSSH_DEFAULT_KEYSIZE_RSA              2048
#define REVSSH_DEFAULT_KEYSIZE_ECDSA            256
#define REVSSH_KEYSIZE_ED25519                  256

/**
 * RevSSH object type.
 *
 * This is an opaque type. The actual structure implementation is hidden as
 * it depends on the underlying backend implementation.
 *
 * A new instance of the object can be obtained by calling @ref revssh_new()
 * and must be destroyed using @ref revssh_del().
 *
 */
typedef struct revssh revssh_t;

/**
 * RevSSH tunnel status.
 *
 * - "unknown": Tunnel status unknown. Usually just at the beginning shortly after RevSSH configured.
 *
 * - "init_error": Error before even trying to connect to RevSSH server. Usually this would mean
 *    that the temporary ssh server failed to start on the node (maybe a bind error / address
 *    already in used or similar).
 *
 * - "connect_failure": Node failed connecting to RevSSH server. Usually this would mean that
 *    the RevSSH server was not reachable via the configured FQDN or IP address/port, is refusing
 *    connections or some other network error.
 *
 * - "preauth_failure": Node established a connection to RevSSH server, but failed in the
 *    pre-authorization phase: the client and the server could not agree on the key algorithms
 *    and/or signatures.
 *
 *    For instance, OpenSSH deprecated using SHA-1 in 8.2 in 2020-02-14. and such a server
 *    would refuse authorizations from clients using ssh-rsa (RSA with SHA-1). The solution
 *    is either to make the client use rsa-sha2-256 (RSA with SHA_256) or the server to
 *    allow ssh-rsa.
 *
 * - "connecting": Node is trying to connect to RevSSH server every @ref REVSSH_CLIENT_AUTH_RETRY_INTERVAL
 *    seconds (max @ref REVSSH_CLIENT_AUTH_RETRY_MAX times), and is succeeding in TCP connections
 *    but each time failing only with public key authorization failure (server did not authorize
 *    the client).
 *
 *    RevSSH user is expected to add the node's public key (obtained via @ref revssh_node_pubkey_get())
 *    to RevSSH server's authorized_keys list. When added, the node is expected to successfully
 *    connect/ssh to RevSSH server and establish a reverse SSH tunnel with remote port forwarding.
 *
 * - "auth_failure": Node eventually gave up after several authorization failures
 *    (after trying @ref REVSSH_CLIENT_AUTH_RETRY_MAX times every @ref REVSSH_CLIENT_AUTH_RETRY_INTERVAL seconds)
 *
 * - "remote_fwd_failure": Remote port forwarding failed. This indicates an error on the remote
 *    (RevSSH server) side. Usually this would happen because a conflicting (in use) port was used
 *    on the RevSSH server side and although client connected, a local TCP socket could not be
 *    created.
 *
 * - "established": node ssh-ed into RevSSH server, reverse SSH tunnel with remote port forwarding
 *    established. User can now login to RevSSH server and then ssh to the node through the
 *    established reverse ssh tunnel.
 *
 * - "active": There is at least 1 ssh user session active – i.e. at least 1 ssh user is currently
 *    logged in to the node through the established reverse ssh tunnel. Implies "established".
 *
 * - "disconnected_idle": The whole RevSSH session automatically disconnected due to idle
 *    (inactivity) timeout.
 *
 * - "disconnected_maxtime": The whole RevSSH session automatically disconnected due to
 *    max session time reached.
 *
 * - "disconnected": RevSSH tunnel disconnected for any other reason including a normal stop
 *    operation i.e. a call to @ref revssh_del().
 */
#define REVSSH_TUNSTATUS(M)                                            \
    M(REVSSH_TUN_STATUS_UNKNOWN,               "unknown")              \
    M(REVSSH_TUN_STATUS_INIT_ERR,              "init_error")           \
    M(REVSSH_TUN_STATUS_CONNECT_FAILURE,       "connect_failure")      \
    M(REVSSH_TUN_STATUS_PREAUTH_FAILURE,       "preauth_failure")      \
    M(REVSSH_TUN_STATUS_AUTH_FAILURE,          "auth_failure")         \
    M(REVSSH_TUN_STATUS_CONNECTING,            "connecting")           \
    M(REVSSH_TUN_STATUS_REMOTE_FWD_FAILURE,    "remote_fwd_failure")   \
    M(REVSSH_TUN_STATUS_ESTABLISHED,           "established")          \
    M(REVSSH_TUN_STATUS_ACTIVE     ,           "active")               \
    M(REVSSH_TUN_STATUS_DISCONNECTED_IDLE,     "disconnected_idle")    \
    M(REVSSH_TUN_STATUS_DISCONNECTED_MAXTIME,  "disconnected_maxtime") \
    M(REVSSH_TUN_STATUS_DISCONNECTED,          "disconnected")         \
    M(REVSSH_TUN_STATUS_MAX,                   NULL)

enum revssh_tunnel_status
{
    #define _ENUM(sym, str) sym,
    REVSSH_TUNSTATUS(_ENUM)
    #undef _ENUM
};

/**
 *  Key types.
 */
#define REVSSH_KEYTYPE(M)                    \
    M(REVSSH_KEYTYPE_NONE,      "none")      \
    M(REVSSH_KEYTYPE_RSA,       "rsa")       \
    M(REVSSH_KEYTYPE_ECDSA,     "ecdsa")     \
    M(REVSSH_KEYTYPE_ED25519,   "ed25519")   \
    M(REVSSH_KEYTYPE_MAX,       NULL)        \

enum revssh_keytype
{
    #define _ENUM(sym, str) sym,
    REVSSH_KEYTYPE(_ENUM)
    #undef _ENUM
};

/**
 * RevSSH session status.
 */
struct revssh_status
{
    enum revssh_tunnel_status     rs_tun_status;           /** RevSSH tunnel status */

    char                          rs_last_err_msg[256];    /** Last error message */
};

/**
 * Reverse SSH notification callback function type
 *
 * @param  self[in]            revssh_t object
 * @param  tunnel_status[out]  RevSSH session status.
 *
 */
typedef void revssh_status_fn_t(const revssh_t *self, const struct revssh_status *revssh_status);

/**
 * Create a new instance of revssh_t object.
 *
 * Note: The behavior of this function depends on the underlying backend implementation.
 * It is possible that the underlying implementation may support only a single RevSSH
 * session at any given time (i.e. a singleton revssh_t object).
 *
 * @return  A valid revssh_t object or NULL on error.
 *
 */
revssh_t *revssh_new(void);

/**
 * Set RevSSH server parameters.
 *
 * @param self[in]       A valid @ref revssh_t object.
 *
 * @param host[in]       RevSSH server host: FQDN or IP address.
 *
 * @param port[in]       RevSSH port number or -1 to use the default port @ref REVSSH_DEFAULT_SERVER_PORT;
 *
 * @param user[in]       RevSSH server user name to use when establishing a client connection to it.
 *
 * @return true on success.
 */
bool revssh_server_params_set(revssh_t *self, const char *host, int port, const char *user);

/**
 *
 * A public key to be temporarily (just for the duration of a RevSSH session) added to
 * node’s authorized_keys list.
 *
 * @param self[in]       A valid @ref revssh_t object.
 *
 * @param pubkey[in]     Public key string in SSH public key format - RFC 4253.
 *
 * 1 or more (max @ref REVSSH_AUTHORIZED_KEY_MAX) public keys of a RevSSH server user(s)
 * can be configured. This allows for the same RevSSH session to be used concurrently by
 * 2 or more different users.
 *
 * @return true on success.
 */
bool revssh_authorized_keys_add(revssh_t *self, const char *pubkey);

/**
 * Set RevSSH tunnel parameters.
 *
 * @param self[in]                A valid @ref revssh_t object.
 *
 * @param remote_bind_addr[in]    Remote bind address. Optional, may be specified as NULL in
 *                                which case it defaults to @ref REVSSH_DEFAULT_REMOTE_BIND_ADDR
 *                                (127.0.0.1).
 *
 * @param remote_bind_port[in]    Remote bind port. TCP listening socket on the server bind port.
 *                                Care must be taken to not use conflicting ports, i.e. to not
 *                                use a port number that may already be in use by another
 *                                service (or another RevSSH session, maybe for a different node)
 *                                on the RevSSH server. It is advised to not used well-known ports
 *                                (0-1023) – those can be bound to only by a privileged user anyway.
 *
 * @param local_addr[in]          Local bind address. And also the address to forward the
 *                                TCP connection to. IPv4 or IPv6 address. Temporary ssh server
 *                                instance will be started bound to this address. TCP connections
 *                                will be forwarded to this local address on the node. Optional,
 *                                may be specified as NULL in which case it defaults to
 *                                @ref REVSSH_DEFAULT_LOCAL_ADDR (127.0.0.1).
 *
 * @param local_port[in]          Local bind port. And also the local port to forward the
 *                                TCP connection to. Temporary ssh server instance will be started
 *                                on the node bound to this port. TCP connections will be forwarded
 *                                to this local port on the node. Care must be taken to not use a
 *                                conflicting port that may already be open on the node.
 *                                It is advised to avoid well-known ports (0-1023). Optional, if
 *                                set to -1 it defaults to @ref REVSSH_DEFAULT_LOCAL_PORT.
 *
 * @return true on success.
 */
bool revssh_tunnel_params_set(
        revssh_t *self,
        osn_ipany_addr_t *remote_bind_addr,
        int remote_bind_port,
        osn_ipany_addr_t *local_addr,
        int local_port);

/**
 * Set RevSSH session timeouts.
 *
 * The call to this function is entirely optional. If not called, default timeouts are used:
 * @ref CONFIG_REVSSH_DEFAULT_SESSION_MAX_TIME and @ref REVSSH_DEFAULT_IDLE_TIMEOUT.
 *
 * @param self[in]                A valid @ref revssh_t object.
 *
 * @param session_max_time[in]    Maximum time allowed for the whole RevSSH session (all user ssh
 *                                sessions, regardless of whether active or inactive). In minutes.
 *                                If this time is reached, regardless if user session is active or
 *                                not, the whole RevSSH session is destroyed and resources cleaned up.
 *                                If set to -1 defaults to @ref CONFIG_REVSSH_DEFAULT_SESSION_MAX_TIME.
 *
 * @param idle_timeout[in]        Idle timeout per user ssh session. In minutes. If a user ssh
 *                                session is idle (inactive) for this amount of time, it is
 *                                automatically closed. If there are no more active user ssh
 *                                sessions, the whole RevSSH session is destroyed automatically
 *                                and resources cleaned up. If set to -1 defaults to @ref
 *                                REVSSH_DEFAULT_IDLE_TIMEOUT.
 *
 * @return true on success.
 */
bool revssh_timeout_set(revssh_t *self, int session_max_time, int idle_timeout);

/**
 * Set RevSSH status notification callback.
 *
 * @param self[in]                A valid @ref revssh_t object.
 *
 * @param status_fn_cb[in]        Pointer to function that will be called on each RevSSH session
 *                                status change.
 *
 * @return true on success.
 */
bool revssh_notify_status_callback_set(revssh_t *self, revssh_status_fn_t *status_fn_cb);

/**
 * Request a temporary node keypair to be generated just for this RevSSH session.
 *
 * After a successful call to this function, @ref revssh_node_pubkey_get() can be used to get
 * the public part of the generated temporary node's keypair.
 *
 * @param self[in]                A valid @ref revssh_t object.
 *
 * @param type[in]                Key type.
 *
 * @param bits[in]                Key bitsize or -1 to use the key type's default bitsze.
 *
 * @return true on success
 *
 */
bool revssh_tmpkeygen_set(revssh_t *self, enum revssh_keytype type, int bits);

/**
 * Start RevSSH session.
 *
 * After configuring all the needed RevSSH parameters, this function should be called to
 * start a RevSSH session.
 *
 * @param self[in]                A valid @ref revssh_t object.
 *
 * @return true on success.
 */
bool revssh_start(revssh_t *self);

/**
 * Get a string representation of @ref enum revssh_tunnel_status.
 *
 * @param[in]     status    RevSSH tunnel status
 *
 * @return        String representation of the RevSSH tunnel status.
 */
const char *revssh_tunnel_status_tostr(enum revssh_tunnel_status status);

/**
 * Get a string representation of @ref enum revssh_keytype.
 *
 * @param[in]     type    Key type.
 *
 * @return        String representation of the key type.
 */
const char *revssh_keytype_tostr(enum revssh_keytype type);

/**
 * Get @ref enum revssh_keytype from its string representation.
 *
 * @param[in]     str     string representation of key type
 *
 * @return        Key type.
 */
enum revssh_keytype revssh_keytype_fromstr(const char *str);

/**
 * Get node's effective public key.
 *
 * This will be either node’s “static” public key, or if temporary node keypair generation was
 * requested (@ref revssh_node_gen_tmpkey_set_params() called) it will be the public key of the
 * node's temporarily-generated public-private key pair.
 *
 * RevSSH user should then add this public key to RevSSH server user authorized_keys list so that
 * the node would be able to establish a ssh connection to the RevSSH server.
 *
 * @param self[in]                A valid @ref revssh_t object.
 *
 * @param pubkey[out]             Effective node's public key. In the SSH public key format - RFC 4253.
 *
 * @return true on success.
 */
bool revssh_node_pubkey_get(revssh_t *self, char *pubkey, size_t len);

/**
 * Stop the previously started RevSSH session and cleanup all resources.
 *
 * After a RevSSH session is no longer needed this function should be called to stop the
 * temporary ssh server and client instances, rollback the temporary node's authorized_keys and
 * in general cleanup all the RevSSH session resources.
 *
 * @param self[in]                A valid @ref revssh_t object.
 *
 * @return true on success.
 *
 */
bool revssh_del(revssh_t *self);

/**
 * Try to detect any dangling RevSSH session resources and stop them and clean them up.
 *
 * This is a utility function that can be called prior any revssh_new()/revssh_start() or better
 * at program start to make sure that there are no dangling RevSSH sessions active maybe due to
 * OpenSync crash or restart.
 *
 */
void revssh_cleanup_dangling_sessions(void);

/** @} REVSSH */

#endif /* REVSSH_H_INCLUDED */
