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


#include<stdio.h>
#include<stdlib.h>
#include<stddef.h>
#include<string.h>

#include<const.h>
#include<log.h>
#include<assert.h>
#include<ovsdb.h>
#include<target.h>
#include<memutil.h>

#include "fsm_csum_utils.h"
#include "dhcp_relay.h"

/*
 *                      DHCP v4 Packet format
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |     op (1)    |   htype (1)   |   hlen (1)    |   hops (1)    |
 *  +---------------+---------------+---------------+---------------+
 *  |                            xid (4)                            |
 *  +-------------------------------+-------------------------------+
 *  |           secs (2)            |           flags (2)           |
 *  +-------------------------------+-------------------------------+
 *  |                          ciaddr  (4)                          |
 *  +---------------------------------------------------------------+
 *  |                          yiaddr  (4)                          |
 *  +---------------------------------------------------------------+
 *  |                          siaddr  (4)                          |
 *  +---------------------------------------------------------------+
 *  |                          giaddr  (4)                          |
 *  +---------------------------------------------------------------+
 *  |                                                               |
 *  |                          chaddr  (16)                         |
 *  |                                                               |
 *  |                                                               |
 *  +---------------------------------------------------------------+
 *  |                                                               |
 *  |                          sname   (64)                         |
 *  +---------------------------------------------------------------+
 *  |                                                               |
 *  |                          file    (128)                        |
 *  +---------------------------------------------------------------+
 *  |                                                               |
 *  |                          options (variable)                   |
 *  +---------------------------------------------------------------+
 */

/*************************************************************************************************/
/**
 *  Insert dhcp option relay agent information
 *  The configuration of the option: DHCPv4_OPTION:82='CIRCUIT_ID:CUIRCUIT_ID_VALUE_STR,REMOTE_ID'
 */

static uint8_t
dhcp_relay_option82_insert(struct dhcp_relay_conf_options *opt,
                           struct net_header_parser *net_parser, uint8_t *popt)
{
    char                *buf, *p, *circuit_id = NULL;
    char                mac_str[MAC_STR_LEN] = {0X0};
    uint8_t             size_option       = 0;
    uint8_t             cid_len           = 0;
    uint8_t             rid_len           = 0;
    struct  dhcp_hdr    *dhcp;
    int                 rc                = 0;

    buf = STRDUP(opt->value);
    if(buf == NULL) return 0;

    /* Parce Option Parameter */
    p = strtok(buf, ",");
    while (p != NULL)
    {
        char *name;

        name = strsep(&p, ":");
        if (name == NULL) continue;

        rc = strcmp(name,"CIRCUIT_ID");
        if (!rc)
        {
            if (p == NULL) continue;

            circuit_id = STRDUP(p);
            if (circuit_id == NULL)
            {
                FREE(buf);
                return 0;
            }

            LOGD("%s: circuit_id = [%s]", __func__, p);
            cid_len = strlen(circuit_id);
            size_option += cid_len + 2;
        }

        rc = strcmp(name,"REMOTE_ID");
        if (!rc)
        {
            dhcp = (void *)(net_parser->data);
            SPRINTF(mac_str, PRI(os_macaddr_lower_t), FMT(os_macaddr_t, dhcp->dhcp_chaddr));
            rid_len = strlen(mac_str);
            size_option += rid_len + 2;
        }

        p = strtok(NULL, ",");
    }

    /* Set the Option Agent Information ID */
    *popt++ = DHCP_OPTION_AGENT_INFORMATION;

    /* Set the Option Agent Information ID Length */
    *popt++ =  size_option;

    /* Set Sub Option Cuircuit ID*/
    *popt++ = RAI_CIRCUIT_ID;
    *popt++ = cid_len;
    memcpy (popt, circuit_id, cid_len);
    popt += cid_len;

    /* Set Sub Option Remote ID*/
    *popt++ = RAI_REMOTE_ID;
    *popt++ = rid_len;
    memcpy (popt, mac_str, rid_len);
    popt += rid_len;

    /* Insert End of Option */
    *popt++ = 0xff;

    FREE(buf);
    if (circuit_id) FREE(circuit_id);

    return size_option + 2;
}

/**
 *  Insert Options to DHCP v4 Packet
 */
static uint8_t
dhcp_relay_insert_dhcpv4_options(struct net_header_parser *net_parser, uint8_t *popt)
{
    ds_dlist_t                      *options;
    struct dhcp_relay_conf_options  *opt = NULL;
    ds_dlist_iter_t                 iter;
    uint8_t                         size_options = 0;

    options = dhcp_get_relay_options();
    for ( opt = ds_dlist_ifirst(&iter, options);
          opt != NULL;
          opt = ds_dlist_inext(&iter))
    {
        switch (opt->id)
        {
            case DHCP_OPTION_AGENT_INFORMATION:
            {
                /* Parse the Option */
                LOGD("%s: Insert option %d",__func__,opt->id);
                size_options += dhcp_relay_option82_insert(opt, net_parser, popt);
                break;
            }

            default:
            {
                LOGD("%s: Option id=%d is not supported", __func__, opt->id);
                break;
            }
        }
    }

    return size_options;
}

/*************************************************************************************************/

/**
 * @brief process a DHCP message to check if it already contains option82
 *
 * @param parser the parsed data container
 * @return true if option 82 exists, false otherwise
 */
bool
dhcp_relay_check_option82(struct dhcp_session *d_session)
{
    struct dhcp_parser          *parser;
    struct net_header_parser    *net_parser;
    struct dhcp_hdr             *dhcp;
    uint8_t                     *popt    = NULL;
    uint8_t                      relay_options_len = 0;

    if (d_session == NULL)
    {
        LOGE("%s: d_session is NULL", __func__);
        return false;
    }

    parser = &d_session->parser;
    net_parser = parser->net_parser;

    relay_options_len = parser->relay_options_len;

    LOGT("%s: total relay_options_len is '%hhu'", __func__, relay_options_len);

    dhcp = (void *)(net_parser->data);
    popt = dhcp->dhcp_options;

    while (popt < ((parser->data) + (parser->dhcp_len) + (relay_options_len)))
    {
        uint8_t optid, optlen;

        // End option, break out
        if (*popt == 255) break;

        // Pad option, continue
        if (*popt == 0)
        {
            popt++;
            continue;
        }

        if (popt + 2 > ((parser->data) + (parser->dhcp_len) + (relay_options_len))) break;

        optid = *popt++;
        optlen = *popt++;

        if ((popt + optlen) > ((parser->data) + (parser->dhcp_len)+(relay_options_len))) break;

        LOGT("%s: DHCP option id: %hhu", __func__, optid);
        switch (optid)
        {
            case DHCP_OPTION_AGENT_INFORMATION:
                return true;

            default:
                break;
        }

        popt += optlen;
    }

    return false;
}

/*************************************************************************************************/
// DHCP message processing(reading options, msg_type, etc)

void
dhcp_relay_process_dhcpv4_message(struct dhcp_session *d_session)
{
    struct  net_header_parser   *net_parser;
    struct  dhcp_parser         *parser;
    struct  dhcp_hdr            *dhcp;

    uint8_t                     *popt    = NULL;
    uint8_t                     msg_type = 0;
    char                        mac_str[MAC_STR_LEN];

    struct pcap_pkthdr          *header;
    uint8_t                     relay_options_len = 0 ;

    if (d_session == NULL) return;

    parser = &d_session->parser;
    net_parser = parser->net_parser;
    header = &parser->header;

    dhcp = (void *)(net_parser->data);

    if (ntohl(dhcp->dhcp_magic) != DHCP_MAGIC)
    {
        LOGE("%s: Magic number invalid, dropping packet", __func__);
        return;
    }

    LOGD("%s: DHCP processing: DHCP packet htype: %d hlen:%d xid:%08x"
         " ClientAddr:"PRI(os_macaddr_t)" ClientIP:"PRI(os_ipaddr_t)" Magic:%08x",
         __func__, dhcp->dhcp_htype, dhcp->dhcp_hlen, ntohl(dhcp->dhcp_xid),
         FMT(os_macaddr_t, dhcp->dhcp_chaddr), FMT(os_ipaddr_t, dhcp->dhcp_yiaddr),
         ntohl(dhcp->dhcp_magic));

    SPRINTF(mac_str, PRI(os_macaddr_lower_t), FMT(os_macaddr_t, dhcp->dhcp_chaddr));

    // Parse DHCP options, update the current lease
    popt = dhcp->dhcp_options;

    while (popt < ((parser->data) + (parser->dhcp_len)))
    {
        uint8_t optid, optlen;

        // End option, break out
        if (*popt == 255) break;

        // Pad option, continue
        if (*popt == 0)
        {
            popt++;
            continue;
        }

        if (popt + 2 > ((parser->data) + (parser->dhcp_len)))
        {
            LOGD("%d: DHCP options truncated", __LINE__);
            break;
        }

        optid = *popt++;
        optlen = *popt++;

        if ((popt + optlen) > ((parser->data) + (parser->dhcp_len)))
        {
            LOGD("%d: DHCP options truncated", __LINE__);
            break;
        }

        LOGT("%s: DHCP option id: %hhu, length: %hhu", __func__, optid, optlen);
        switch (optid)
        {
            case DHCP_OPTION_MSG_TYPE:
            {
                // Message type
                LOGD("%s: Message type = %d", __func__, *(uint8_t *)popt);
                msg_type = *popt;
                break;
            }

            default:
                break;
        }
        popt += optlen;
    }

    /* Because of the way Openflow rules are specified, this plugin should receive only
     * the DHCP packets sent by the clients(DISCOVER, REQUEST and RELEASE). Among these,
     * option82 should be inserted only for DISCOVER and REQUEST packets */
    if (msg_type == DHCP_MSG_DISCOVER || msg_type == DHCP_MSG_REQUEST)
    {
        if (!dhcp_relay_check_option82(d_session))
        {
            LOGT("%s: option 82 absent, adding it", __func__);
            /* Insert Relay Options */
            relay_options_len = dhcp_relay_insert_dhcpv4_options(net_parser, popt);
            dhcp_relay_update_headers(net_parser, popt, relay_options_len);
        }
        else
        {
            LOGT("%s: DHCP message type '%d' already contains option82", __func__, msg_type);
        }
    }

    LOGT("%s: relay_options_len is '%hhu'", __func__, relay_options_len);

    /* Update the pcap_pkthdr caplen and len, required for forwarding  */
    header->caplen = net_parser->caplen + relay_options_len;
    header->len = net_parser->caplen + relay_options_len;
    parser->relay_options_len = relay_options_len;

    return;
}
