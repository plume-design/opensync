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

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <errno.h>

#include "ds_tree.h"
#include "fsm.h"
#include "fsm_dpi_client_plugin.h"
#include "fsm_dpi_utils.h"
#include "kconfig.h"
#include "log.h"
#include "memutil.h"
#include "net_header_parse.h"
#include "os.h"
#include "util.h"
#include "fsm_csum_utils.h"
#include "fsm_fn_trace.h"

#include "fsm_dpi_dhcp_relay.h"

static struct dpi_dhcp_client main_data = {
    .initialized = false,
    .cur_rec_processed.next_state = BEGIN_DHCP,
};

struct dpi_dhcp_client *fsm_dpi_dhcp_get_mgr(void)
{
    return &main_data;
}

const char *const dhcp_state_str[] = {
    [UNDEFINED] = "undefined",
    [BEGIN_DHCP] = "begin",
    [DHCP_MESSAGE_TYPE] = "dhcp.message_type",
    [END_DHCP] = "end",
};

const char *dhcp_attr_value = "dhcp";

static enum dhcp_state get_dhcp_state(const char *attr)
{
#define GET_DHCP_STATE(attr, x)                \
    do                                         \
    {                                          \
        int cmp;                               \
        cmp = strcmp(attr, dhcp_state_str[x]); \
        if (cmp == 0) return x;                \
    } while (0)

    GET_DHCP_STATE(attr, BEGIN_DHCP);
    GET_DHCP_STATE(attr, DHCP_MESSAGE_TYPE);
    GET_DHCP_STATE(attr, END_DHCP);

    return UNDEFINED;
#undef GET_DHCP_STATE
}

void fsm_dpi_dhcp_reset_state(struct fsm_session *session)
{
    struct dhcp_relay_session *d_session;
    struct dpi_dhcp_client *mgr;
    struct dhcp_record *rec;
    ds_tree_t *sessions;

    mgr = fsm_dpi_dhcp_get_mgr();
    sessions = &mgr->fsm_sessions;

    d_session = ds_tree_find(sessions, session);
    if (d_session == NULL) return;

    rec = &mgr->cur_rec_processed;
    MEMZERO(*rec);
    rec->next_state = BEGIN_DHCP;
}

static ds_dlist_t dhcp_relay_options;

ds_dlist_t *dhcp_get_relay_options(void)
{
    return &dhcp_relay_options;
}

/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int dhcp_session_cmp(const void *a, const void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    return (p_a - p_b);
}

/**
 * checksum Return the checksum of a specific buffer
 */

uint32_t checksum(unsigned char *buf, unsigned nbytes, uint32_t sum)
{
    unsigned i;

    /* Checksum all the pairs of bytes first... */
    for (i = 0; i < (nbytes & ~1U); i += 2)
    {
        sum += (uint16_t)ntohs(*((uint16_t *)(buf + i)));
        if (sum > 0xFFFF) sum -= 0xFFFF;
    }

    /* If there's a single byte left over, checksum it, too.   Network
    byte order is big-endian, so the remaining byte is the high byte. */

    if (i < nbytes)
    {
        sum += buf[i] << 8;
        if (sum > 0xFFFF) sum -= 0xFFFF;
    }

    return (sum);
}

/**
 *  wrapsum: Return the wrapsum
 */

uint32_t wrapsum(uint32_t sum)
{
    sum = ~sum & 0xFFFF;
    return (htons(sum));
}

/**
 *  Update packet Headers
 */
void dhcp_relay_update_headers(struct net_header_parser *net_parser, uint8_t *popt, uint16_t size_options)
{
    struct iphdr *ip_hdr;
    struct ip6_hdr *ip6_hdr;
    struct udphdr *udp_hdr;
    struct dhcp_hdr *dhcp;
    unsigned dhcp_len = 0;

    if (size_options == 0) return;

    udp_hdr = net_parser->ip_pld.udphdr;

    udp_hdr->len = htons(ntohs(udp_hdr->len) + size_options);
    udp_hdr->check = 0;

    if (net_parser->ip_version == 4)
    {
        /* Extract DHCPv4 struct */
        dhcp = (void *)(net_parser->data);
        dhcp_len = popt - (unsigned char *)dhcp;
        /* Update IP header Checksum */
        ip_hdr = net_header_get_ipv4_hdr(net_parser);
        ip_hdr->check = 0;
        ip_hdr->tot_len = htons(ntohs(ip_hdr->tot_len) + size_options);
        ip_hdr->check = wrapsum(checksum((unsigned char *)ip_hdr, ((uint8_t)ip_hdr->ihl) * 4, 0));

        /* Update UDP header Checksum */
        udp_hdr->check = wrapsum(checksum(
                (unsigned char *)udp_hdr,
                sizeof(udp_hdr),
                checksum(
                        (unsigned char *)dhcp,
                        dhcp_len,
                        checksum(
                                (unsigned char *)&ip_hdr->saddr,
                                2 * sizeof(&ip_hdr->saddr),
                                IPPROTO_UDP + (u_int32_t)ntohs(udp_hdr->len)))));
    }
    else if (net_parser->ip_version == 6)
    {
        /* IPv6 : Calculate the checksum TO DO*/
        ip6_hdr = net_header_get_ipv6_hdr(net_parser);
        ip6_hdr->ip6_plen = htons(ntohs(ip6_hdr->ip6_plen) + size_options);
    }

    return;
}

/*
 * Add a relay option to the injection list. Returns a pointer to the option.
 */

static struct dhcp_relay_conf_options *dhcp_relay_options_store(
        ds_dlist_t *options,
        int ip_version,
        int option_id,
        const char *value)
{
    struct dhcp_relay_conf_options *opt = NULL;
    char *value_copy = NULL;

    value_copy = STRDUP(value);
    if (value_copy == NULL)
    {
        return NULL;
    }

    opt = CALLOC(1, sizeof(struct dhcp_relay_conf_options));
    if (opt == NULL)
    {
        LOGE("%s: Unable to allocate dhcp_relay_conf_options!", __func__);
        FREE(value_copy);
        return NULL;
    }

    opt->version = ip_version;
    opt->id = option_id;
    opt->value = value_copy;

    ds_dlist_insert_tail(options, opt);
    LOGT("%s: Storing id '%hhu' version: '%hhu' value: '%s'", __func__, opt->id, opt->version, opt->value);

    return opt;
}

/**
 * fsm_dpi_dhcp_relay_options_init: dso initialization entry point
 */

static int fsm_dpi_dhcp_relay_options_init(void)
{
    char buf[300];
    FILE *conf = NULL;
    ds_dlist_t *options = NULL;
    int rc;

    /* Init DHCP Options Relay */
    options = dhcp_get_relay_options();
    ds_dlist_init(options, struct dhcp_relay_conf_options, d_node);

    /* Open the options file */
    conf = fopen(DHCP_RELAY_CONF_FILE, "r");
    if (!conf)
    {
        LOGW("%s: Cannot open DHCP Relay Conf file '%s', errno: '%s'", __func__, DHCP_RELAY_CONF_FILE, strerror(errno));
        return -1;
    }

    while (fgets(buf, sizeof(buf), conf) != NULL)
    {
        int version, id;
        char *p = buf;
        char *str = NULL;
        char *value = NULL;

        /* Check IPv6 or IPv4 option and select version */
        rc = strncmp(p, "DHCPv4_OPTION:", strlen("DHCPv4_OPTION:"));
        if (!rc)
        {
            /* Skip the "export keyword" */
            p += strlen("DHCPv4_OPTION:");
            version = 4;
        }
        else
        {
            /* Skip the "export keyword" */
            p += strlen("DHCPv6_OPTION:");
            version = 6;
        }

        str = strsep(&p, "=");
        if (str == NULL)
        {
            continue;
        }

        id = atoi(str);

        /* Skip the first ' */
        value = strsep(&p, "'");
        if (value == NULL || value[0] != '\0')
        {
            continue;
        }

        value = strsep(&p, "'");
        if (dhcp_relay_options_store(options, version, id, value) == NULL)
        {
            return -1;
        }
    }

    return 1;
}

/*************************************************************************************************/
// DHCP packet re-injection framework

void dhcp_prepare_forward(struct dhcp_relay_session *d_session, uint8_t *packet)
{
    struct net_header_parser *net_parser;
    struct dhcp_parser *parser;
    struct eth_header *eth_header;
    struct udphdr *udp_hdr;
    char src_ip[128] = {0};
    char dst_ip[128] = {0};

    if (d_session == NULL) return;

    parser = &d_session->parser;
    net_parser = parser->net_parser;

    eth_header = net_header_get_eth(net_parser);
    LOGD("%s: source mac: " PRI_os_macaddr_lower_t ", dst mac: " PRI_os_macaddr_lower_t,
         __func__,
         FMT_os_macaddr_pt(eth_header->srcmac),
         FMT_os_macaddr_pt(eth_header->dstmac));

    udp_hdr = net_parser->ip_pld.udphdr;
    udp_hdr->check = 0;

    if (net_parser->ip_version == 4)
    {
        /* UDP checksum to 0 */
        (void)net_header_dstip_str(net_parser, dst_ip, sizeof(dst_ip));
        LOGD("%s: dst address %s, dst_port %d, checksum 0x%x", __func__, dst_ip, ntohs(udp_hdr->dest), udp_hdr->check);

        (void)net_header_srcip_str(net_parser, src_ip, sizeof(src_ip));
        LOGD("%s: src address %s, src port %d", __func__, src_ip, ntohs(udp_hdr->source));
    }
    else if (net_parser->ip_version == 6)
    {
        /* IPv6 */
        uint16_t csum = fsm_compute_udp_checksum(packet, net_parser);
        udp_hdr->check = htons(csum);
    }

    LOGT("%s: marking the following net header for re-injection", __func__);
    net_header_logt(net_parser);

    MEM_CPY(d_session->raw_dst.sll_addr, eth_header->dstmac, sizeof(os_macaddr_t));
    if (kconfig_enabled(DHCP_RELAY_SPOOF_SOURCE_MAC))
    {
        MEM_CPY(packet + 6, d_session->src_eth_addr.addr, sizeof(d_session->src_eth_addr.addr));
    }
}

void dhcp_forward(struct dhcp_relay_session *d_session)
{
    struct dpi_dhcp_client *mgr;
    struct net_header_parser *net_parser;
    struct dhcp_parser *parser;
    struct pcap_pkthdr *header;
    uint8_t *packet;
    int len;

    mgr = fsm_dpi_dhcp_get_mgr();
    if (mgr == NULL) return;

    parser = &d_session->parser;
    net_parser = parser->net_parser;
    header = &parser->header;
    len = header->caplen;
    /* Update net_parser with the len after adding option82 */
    net_parser->caplen = len;

    packet = (uint8_t *)net_parser->start;

    /* Prepare the packet for forwarding */
    dhcp_prepare_forward(d_session, packet);

    /* dhcp_relay plugin should modify the packet and notify fsm of the modified payload */
    net_parser->payload_updated = true;
    return;
}

/*************************************************************************************************/
// DHCP content and message parsing

static size_t fsm_dpi_dhcp_parse_content(struct dhcp_parser *parser)
{
    struct net_header_parser *net_parser;
    uint16_t ethertype;
    int ip_protocol;
    struct udphdr *hdr;

    if (parser == NULL) return 0;

    net_parser = parser->net_parser;

    // Check ethertype
    ethertype = net_header_get_ethertype(net_parser);
    if ((ethertype != ETH_P_IP) && (ethertype != ETH_P_IPV6)) return 0;

    // Check for IP protocol
    if ((net_parser->ip_version != 4) && (net_parser->ip_version != 6)) return 0;

    // Check for UDP protocol
    ip_protocol = net_parser->ip_protocol;
    if (ip_protocol != IPPROTO_UDP) return 0;

    // Check the UDP src and dst ports
    hdr = net_parser->ip_pld.udphdr;
    if (((ntohs(hdr->source) != 68 || ntohs(hdr->dest) != 67) && (ntohs(hdr->source) != 67 || ntohs(hdr->dest) != 68))
        && ((ntohs(hdr->source) != 546 || ntohs(hdr->dest) != 547)
            && (ntohs(hdr->source) != 547 || ntohs(hdr->dest) != 546)))
    {
        LOGD("%s: dhcp_parse_content: UDP src/dst port range mismatch,"
             " source port:%d, dest port:%d",
             __func__,
             ntohs(hdr->source),
             ntohs(hdr->dest));
        return 0;
    }

    return parser->dhcp_len;
}

/**
 * @brief parses a dhcp message
 *
 * @param parser the parsed data container
 * @return the size of the parsed message, or 0 on parsing error.
 */
size_t fsm_dpi_dhcp_parse_message(struct dhcp_parser *parser)
{
    struct net_header_parser *net_parser;
    size_t len;
    int ip_protocol;

    if (parser == NULL) return 0;

    net_parser = parser->net_parser;

    /* Some basic validation */
    ip_protocol = net_parser->ip_protocol;
    if (ip_protocol != IPPROTO_UDP) return 0;

    /* Parse the dhcp content */
    parser->parsed = net_parser->parsed;
    parser->data = net_parser->data;
    parser->dhcp_len = net_parser->packet_len - net_parser->parsed;
    len = fsm_dpi_dhcp_parse_content(parser);
    if (len == 0) return 0;

    return len;
}

/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the dhcp session to delete
 */
void fsm_dpi_dhcp_delete_session(struct fsm_session *session)
{
    struct dhcp_relay_session *d_session;
    struct dpi_dhcp_client *mgr;
    ds_tree_t *sessions;

    mgr = fsm_dpi_dhcp_get_mgr();
    sessions = &mgr->fsm_sessions;

    d_session = ds_tree_find(sessions, session);
    if (d_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, d_session);

    return;
}

/**
 * @brief process a flow attribute
 *
 * @param session the fsm session
 * @param attr the attribute flow
 * @param value the attribute flow value
 * @param acc the flow
 */
int fsm_dpi_dhcp_process_attr(
        struct fsm_session *session,
        const char *attr,
        uint8_t type,
        uint16_t length,
        const void *value,
        struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    struct net_header_parser *net_parser;
    struct net_md_stats_accumulator *acc;
    struct dpi_dhcp_client *mgr;
    struct dhcp_record *rec;
    unsigned int curr_state;
    int action;
    int cmp;
    int ret = -1;
    size_t sz;
    char *val;

    /* Process the generic part (e.g., logging, include, exclude lists) */
    action = fsm_dpi_client_process_attr(session, attr, type, length, value, pkt_info);
    if (action == FSM_DPI_IGNORED) return action;

    mgr = fsm_dpi_dhcp_get_mgr();
    if (!mgr->initialized) return -1;
    if (pkt_info == NULL) return FSM_DPI_IGNORED;

    acc = pkt_info->acc;
    if (acc == NULL) return FSM_DPI_IGNORED;

    net_parser = pkt_info->parser;
    if (net_parser == NULL) return FSM_DPI_IGNORED;

    rec = &mgr->cur_rec_processed;

    curr_state = get_dhcp_state(attr);

    switch (curr_state)
    {
        case BEGIN_DHCP: {
            if (type != RTS_TYPE_STRING)
            {
                LOGD("%s: value for %s should be a string ", __func__, attr);
                goto reset_state_machine;
            }
            cmp = strncmp(value, dhcp_attr_value, length);
            if (cmp)
            {
                LOGD("%s: value for %s should be %s", __func__, attr, dhcp_attr_value);
                goto reset_state_machine;
            }
            if (rec->next_state != UNDEFINED && rec->next_state != curr_state) goto wrong_state;

            rec->next_state = DHCP_MESSAGE_TYPE;

            LOGT("%s: start new DHCP Request - next is %s", __func__, dhcp_state_str[rec->next_state]);
            break;
        }

        case DHCP_MESSAGE_TYPE: {
            if (type != RTS_TYPE_BINARY)
            {
                LOGD("%s: value for %s should be a binary with single raw byte", __func__, attr);
                goto reset_state_machine;
            }
            if (rec->next_state != curr_state) goto wrong_state;

            sz = 2 * length + 1;
            val = CALLOC(1, sz);
            if (val == NULL) goto reset_state_machine;
            (void)bin2hex(value, length, val, sz);

            rec->dhcp_message_type = *(uint8_t *)val;
            rec->next_state = END_DHCP;
            LOGT("%s: received %s, value %s - next is %s",
                 __func__,
                 dhcp_state_str[curr_state],
                 val,
                 dhcp_state_str[rec->next_state]);
            FREE(val);
            break;
        }

        case END_DHCP: {
            if (type != RTS_TYPE_STRING)
            {
                LOGD("%s: value for %s should be a string", __func__, attr);
                goto reset_state_machine;
            }
            cmp = strncmp(value, dhcp_attr_value, length);
            if (cmp)
            {
                LOGD("%s: value for %s should be %s", __func__, attr, dhcp_attr_value);
                goto reset_state_machine;
            }
            if (rec->next_state != curr_state) goto wrong_state;
            rec->next_state = BEGIN_DHCP;

            /* Now we can process the DHCP packet */
            ret = fsm_dpi_process_dhcp_packet(session, net_parser);
            break;
        }

        default: {
            LOGD("%s: Unexpected attr '%s' (expected state '%s')", __func__, attr, dhcp_state_str[rec->next_state]);
            goto reset_state_machine;
        }
    }

    return ret;

wrong_state:
    LOGD("%s: Failed when processing attr '%s' (expected state '%s')", __func__, attr, dhcp_state_str[rec->next_state]);

reset_state_machine:
    fsm_dpi_dhcp_reset_state(session);
    return ret;
}

bool fsm_dpi_process_dhcp_packet(struct fsm_session *session, struct net_header_parser *net_parser)
{
    struct dhcp_relay_session *d_session;
    struct dhcp_parser *parser;
    struct dpi_dhcp_client *mgr;
    ds_tree_t *sessions;
    size_t len;

    mgr = fsm_dpi_dhcp_get_mgr();
    sessions = &mgr->fsm_sessions;

    d_session = ds_tree_find(sessions, session);
    if (d_session == NULL) return false;

    parser = &d_session->parser;
    parser->caplen = net_parser->caplen;

    parser->net_parser = net_parser;
    len = fsm_dpi_dhcp_parse_message(parser);
    if (len == 0) return false;

    /* Process the DHCP message, and insert required options */
    if (net_parser->ip_version == 4)
        fsm_dpi_dhcp_relay_process_dhcpv4_message(d_session);
    else
        fsm_dpi_dhcp_relay_process_dhcpv6_message(d_session);

    /* Forward the packet */
    mgr->dhcp_forward(d_session);

    return true;
}

void fsm_dpi_dhcp_relay_periodic(struct fsm_session *session)
{
    bool need_periodic;

    need_periodic = fsm_dpi_client_periodic_check(session);

    if (need_periodic)
    {
        /* Nothing specific to be done */
    }
}

void fsm_dpi_dhcp_relay_update(struct fsm_session *session)
{
    /* Generic config first */
    fsm_dpi_client_update(session);

    /* DHCP specific entries */
    LOGD("%s: Updating DHCP config", __func__);
}

/**
 * @brief session initialization entry point
 *
 * Initializes the plugin generic fields of the session,
 * like the packet parsing handler and the periodic routines called
 * by fsm
 *
 * @param session pointer provided by fsm
 */
int fsm_dpi_dhcp_relay_init(struct fsm_session *session)
{
    struct fsm_dpi_plugin_client_ops *client_ops;
    struct dhcp_relay_session *dhcp_session;
    struct dpi_dhcp_client *mgr;
    int ret = -1;

    if (session == NULL) return ret;

    mgr = fsm_dpi_dhcp_get_mgr();
    if (mgr->initialized) return 0;

    ds_tree_init(&mgr->fsm_sessions, dhcp_session_cmp, struct dhcp_relay_session, session_node);
    mgr->dhcp_forward = dhcp_forward;
    mgr->initialized = true;

    /* Initialize the config of relay options */
    ret = fsm_dpi_dhcp_relay_options_init();
    if (ret < 0)
    {
        LOGE("%s: Error in initializing DHCP relay options", __func__);
        return ret;
    }

    /* Look up the dhcp session */
    dhcp_session = fsm_dpi_dhcp_get_session(session);
    if (dhcp_session == NULL)
    {
        LOGE("%s: could not allocate dhcp session", __func__);
        return -1;
    }

    /* Bail if the session is already initialized */
    if (dhcp_session->initialized) return 0;

    /* Initialize generic client */
    ret = fsm_dpi_client_init(session);
    if (ret != 0)
    {
        goto error;
    }

    /* And now all the DHCP specific calls */
    session->ops.update = fsm_dpi_dhcp_relay_update;
    session->ops.periodic = fsm_dpi_dhcp_relay_periodic;
    session->ops.exit = fsm_dpi_dhcp_relay_exit;

    /* Set the plugin specific ops */
    client_ops = &session->p_ops->dpi_plugin_client_ops;
    client_ops->process_attr = fsm_dpi_dhcp_process_attr;
    FSM_FN_MAP(fsm_dpi_dhcp_process_attr);
    dhcp_session->session = session;

    /* Fetch the specific updates for this client plugin */
    session->ops.update(session);

    dhcp_session->initialized = true;
    LOGD("%s: added session %s", __func__, session->name);

    return 0;
error:
    fsm_dpi_dhcp_delete_session(session);
    return -1;
}

/*
 * Provided for compatibility
 */
int dpi_dhcp_relay_plugin_init(struct fsm_session *session)
{
    return fsm_dpi_dhcp_relay_init(session);
}

void fsm_dpi_dhcp_relay_exit(struct fsm_session *session)
{
    struct dpi_dhcp_client *mgr;

    mgr = fsm_dpi_dhcp_get_mgr();
    if (!mgr->initialized) return;

    /* Free the generic client */
    fsm_dpi_client_exit(session);
    fsm_dpi_dhcp_delete_session(session);
    return;
}

/**
 * @brief get a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct dhcp_relay_session *fsm_dpi_dhcp_get_session(struct fsm_session *session)
{
    struct dpi_dhcp_client *mgr;
    struct dhcp_relay_session *d_session;
    ds_tree_t *sessions;

    mgr = fsm_dpi_dhcp_get_mgr();
    sessions = &mgr->fsm_sessions;

    d_session = ds_tree_find(sessions, session);
    if (d_session != NULL) return d_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    d_session = CALLOC(1, sizeof(struct dhcp_relay_session));
    if (d_session == NULL) return NULL;

    d_session->initialized = false;
    ds_tree_insert(sessions, d_session, session);

    return d_session;
}
