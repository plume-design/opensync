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
 *                      DHCPv6 Packet Format
 *     0                   1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |    msg-type   |               transaction-id                  |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |                                                               |
 *     .                            options                            .
 *     .                 (variable number and length)                  .
 *     |                                                               |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */

/*************************************************************************************************/

/**
 * Insert option 37 Remote id
 * The configuration of the option: DHCPv6_OPTION:37='ENTERPRISE_NBR:VALUE,REMOTE_ID'
 */

static uint8_t
dhcp_relay_option37_insert(struct dhcp_relay_conf_options *opt,
                           struct net_header_parser *net_parser, uint8_t *popt)
{
    uint16_t    optid, optlen ;
    char        *buf, *p;
    u_int32_t   enterprise_number    = 0;
    char        mac_str[MAC_STR_LEN] = {0X0};
    uint8_t     size                 = 0;
    int         rc                   = 0;

    buf = STRDUP(opt->value);
    if (buf == NULL) return 0;

    /* Parce Option Parameter */
    p = strtok(buf, ",");
    while (p != NULL)
    {
        char *name;

        name = strsep(&p, ":");
        if (name == NULL) continue;

        rc = strcmp(name,"ENTERPRISE_NBR");
        if (!rc)
        {
            if (p == NULL) continue;
            enterprise_number = atoi(p);
            if (enterprise_number == 0)
            {
                FREE(buf);
                return 0;
            }

            LOGT("%s: enterprise number = [%d]", __func__, enterprise_number);
            size += sizeof(uint32_t);
        }

        rc = strcmp(name, "REMOTE_ID");
        if (!rc)
        {
            struct eth_header *eth_header;

            eth_header = net_header_get_eth(net_parser);
            LOGT("%s: Remote id = " PRI_os_macaddr_lower_t ,
                 __func__, FMT_os_macaddr_pt(eth_header->srcmac));

            sprintf (mac_str, PRI_os_macaddr_lower_t, FMT_os_macaddr_pt(eth_header->srcmac));
            size += strlen(mac_str);
        }

        p = strtok(NULL, ",");
    }

    /* Insert option id */
    optid = htons(DHCPv6_REMOTE_ID);
    memcpy(popt, &optid, sizeof(uint16_t));
    popt += sizeof(uint16_t);

    /* Insert option length */
    optlen = htons(size);
    memcpy(popt, &optlen, sizeof(uint16_t));
    popt += sizeof(uint16_t);

    /* Insert Enterprise id */
    enterprise_number = htonl(enterprise_number);
    memcpy(popt, &enterprise_number, sizeof(uint32_t));
    popt  += sizeof(uint32_t);

    /* Insert remote id */
    rc = strlen(mac_str);
    if (rc > 0)
    {
        memcpy(popt, &mac_str, strlen(mac_str));
        popt  += strlen(mac_str);
    }

    /* Calculate size of all the options */
    size +=  2*sizeof(uint16_t) ;

    FREE(buf);
    return size;
}

/**
 *  Insert DHCPv6 option interface id
 *  The configuration of the option: DHCPv6_OPTION:18='VALUE'
 */
static uint8_t
dhcp_relay_option18_insert(struct dhcp_relay_conf_options *opt, uint8_t *popt)
{
    uint16_t optid, optlen;
    uint8_t  size;

    /* Insert option id */
    optid = htons(DHCPv6_INTERFACE_ID);
    memcpy(popt, &optid, sizeof(uint16_t));
    popt += sizeof(uint16_t);

    /* Insert option length */
    optlen = strlen(opt->value);
    optlen = htons(optlen);
    memcpy(popt, &optlen, sizeof(uint16_t));
    popt  += sizeof(uint16_t);

    /* Insert option value */
    memcpy(popt, opt->value, strlen(opt->value));
    popt  += strlen(opt->value);

    /* Calculate size of all the option */
    size = ntohs(optlen) + 2*sizeof(uint16_t) ;

    return size;
}


/**
 *  Inset Option to DHCP Packet
 */
static uint8_t
dhcp_relay_insert_dhcpv6_options(struct net_header_parser *net_parser, uint8_t *popt)
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
            case DHCPv6_INTERFACE_ID:
            {
                /* Parse the Option */
                LOGT("%s: Insert option interface id",__func__);
                size_options += dhcp_relay_option18_insert(opt, popt);
                popt += size_options;
                break;
            }

            case DHCPv6_REMOTE_ID:
            {
                /* Parse the Option */
                LOGT("%s: Insert option remote id",__func__);
                size_options += dhcp_relay_option37_insert(opt, net_parser, popt);
                popt += size_options;
                break;
            }

            default:
            {
                LOGT("%s: Option id = '%d' is not supported", __func__, opt->id);
                break;
            }
        }
    }

    return size_options;
}

/**
 * @brief process a DHCP v6 message to check if it already contains DHCPv6 options
 *
 * @param parser the parsed data container
 * @param dhcpv6_option the dhcpv6 option to check
 * @return true if dhcpv6_option exists, false otherwise
 */
bool
dhcp_relay_check_dhcpv6_option(struct dhcp_session *d_session, int dhcpv6_option)
{
    struct dhcp_parser          *parser;
    struct net_header_parser    *net_parser;
    struct dhcpv6_hdr           *dhcpv6;
    uint8_t                     *popt = NULL;
    uint8_t                     relay_options_len = 0;

    if (d_session == NULL)
    {
        LOGE("%s: d_session is NULL", __func__);
        return false;
    }

    parser = &d_session->parser;
    net_parser = parser->net_parser;

    LOGT("%s: Checking for option '%d'", __func__, dhcpv6_option);

    relay_options_len = parser->relay_options_len;
    LOGT("%s: total relay_options_len is '%hhu'", __func__, relay_options_len);

    dhcpv6 = (void *)(net_parser->data);
    popt   = dhcpv6->options;

    while (popt < ((parser->data) + (parser->dhcp_len) + (relay_options_len)))
    {
        uint16_t optid = 0, optlen = 0;

        /* option id */
        memcpy(&optid, popt, 2);
        optid = ntohs(optid);
        popt += 2;

        /* option len */
        memcpy(&optlen, popt, 2);
        optlen = ntohs(optlen);

        LOGT("%s: Received DHCPv6 Option: %d(%d)\n", __func__, optid, optlen);

        if (optid == dhcpv6_option)
        {
            LOGT("%s: DHCPv6 packet already contains option '%d'", __func__, optid);
            return true;
        }

        popt += optlen + 2;
    }

    return false;
}

/*************************************************************************************************/

void
dhcp_relay_process_dhcpv6_message(struct dhcp_session *d_session)
{
    struct  net_header_parser   *net_parser;
    struct  dhcp_parser         *parser;
    struct  dhcpv6_hdr          *dhcpv6;
    uint8_t                     *popt     = NULL;
    struct pcap_pkthdr          *header;
    uint8_t                     relay_options_len = 0 ;

    if (d_session == NULL) return;

    parser = &d_session->parser;
    net_parser = parser->net_parser;

    header = &parser->header;

    dhcpv6 = (void *)(net_parser->data);

    LOGT("%s: DHCP processing: DHCPv6 packet type: %d", __func__, dhcpv6->msg_type);

    if (dhcp_relay_check_dhcpv6_option(d_session, DHCPv6_INTERFACE_ID))
    {
        LOGT("%s: DHCPv6 packet contains DHCPv6_INTERFACE_ID, not adding", __func__);
        return;
    }

    if (dhcp_relay_check_dhcpv6_option(d_session, DHCPv6_REMOTE_ID))
    {
        LOGT("%s: DHCPv6 packet contains DHCPv6_REMOTE_ID, not adding", __func__);
        return;
    }

    // Parse DHCP options, update the current lease
    popt = dhcpv6->options;

    while (popt < ((parser->data) + (parser->dhcp_len)))
    {
        uint16_t optid = 0, optlen = 0;

        /* option id */
        memcpy(&optid, popt, 2);
        optid = ntohs(optid);
        popt += 2;

        /* option len */
        memcpy(&optlen, popt, 2);
        optlen = ntohs(optlen);

        LOGT("%s: Received DHCPv6 Option: %d(%d)\n", __func__, optid, optlen);
        popt += optlen + 2;
    }

    /* Insert Relay Options */
    relay_options_len = dhcp_relay_insert_dhcpv6_options(net_parser, popt);
    dhcp_relay_update_headers(net_parser, popt, relay_options_len);

    /* Update the pcap_pkthdr caplen and len, required for forwarding  */
    header->caplen = net_parser->caplen + relay_options_len;
    header->len = net_parser->caplen + relay_options_len;
    parser->relay_options_len = relay_options_len;

    return;
}
