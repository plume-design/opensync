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

#ifndef OSN_NFLOG_H_INCLUDED
#define OSN_NFLOG_H_INCLUDED

#include <net/if.h>
#include <stdint.h>

#include "const.h"

/**
 * Structure representing a NFLOG packet
 *
 * Each time a firewall rule matches a NFLOG target, a NFULNL_MSG_PACKET is
 * received on the netlink socket The netlink message gets parsed into this
 * structure for easier handling.
 */
struct osn_nflog_packet
{
    uint16_t        nfp_group_id;                   /**< NFLOG group id */
    uint16_t        nfp_hwproto;                    /**< hw protocol, net order */
    uint32_t        nfp_fwmark;                     /**< Firewall mark */
    double          nfp_timestamp;                  /**< Packet time stamp (as received from the kernel) */
    char            nfp_indev[IF_NAMESIZE];         /**< Input interface */
    char            nfp_outdev[IF_NAMESIZE];        /**< Output interface */
    char            nfp_physindev[IF_NAMESIZE];     /**< Physical input interface */
    char            nfp_physoutdev[IF_NAMESIZE];    /**< Physical output interface */
    char            nfp_hwaddr[C_MACADDR_LEN];      /**< Hardware address */
    uint8_t        *nfp_payload;                    /**< Payload data */
    size_t          nfp_payload_len;                /**< Payload length */
    char           *nfp_prefix;                     /**< The prefix value (--nflog-prefix) */
    uint16_t        nfp_hwtype;                     /**< Hardware header type */
    uint16_t        nfp_hwlen;                      /**< Hardware header length */
    uint8_t        *nfp_hwheader;                   /**< Hardware headet data */
    size_t          nfp_hwheader_len;               /**< Hardware header data len */
};

/**
 *  Initializer for a @ref osn_nflog_packet structure
 */
#define OSN_NFLOG_PACKET_INIT (struct osn_nflog_packet)     \
{                                                           \
    .nfp_timestamp = -1.0,                                  \
}

typedef struct osn_nflog osn_nflog_t;
typedef void osn_nflog_fn_t(osn_nflog_t *self, struct osn_nflog_packet *np);

/**
 * Create a new OSN NFLOG object. The object will be used to register callbacks
 * for NFLOG events. Each event report back a @ref osn_nflog_packet structure
 * representing a single NFLOG packet.
 *
 * @ref osn_nflog_start() must be called to start receiving events via the @p fn
 * callback.
 *
 * @param[in]   nflog_group     The NFLOG group (--nflog-group iptables
 *                              parameter)
 * @param[in]   fn              The function callback that will be called when a
 *                              NFLOG packet is received
 *
 * @return
 * This function returns a new osn_nflog object on success. NULL is returned on
 * error.
 */
osn_nflog_t *osn_nflog_new(int nflog_group, osn_nflog_fn_t *fn);

/**
 * Start processing NFLOG events
 *
 * @param[in]   self            A valid NFLOG object acquired with osn_nflog_new()
 *
 * @return
 * true on success and NFLOG events will be received by the callback function.
 * false if there was an error and no events will be received by the callback.
 */
bool osn_nflog_start(osn_nflog_t *self);

/**
 * Stop processing NFLOG events
 *
 * @param[in]   self            A valid NFLOG object acquired with osn_nflog_new()
 */
void osn_nflog_stop(osn_nflog_t *self);

/**
 * Destroy a osn_nflog_t object that was previously created with @ref osn_nflog_new()
 */
void osn_nflog_del(osn_nflog_t *self);

#endif /* OSN_NFLOG_H_INCLUDED */
