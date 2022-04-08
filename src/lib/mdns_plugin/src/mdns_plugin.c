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

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "ds_tree.h"
#include "log.h"
#include "memutil.h"
#include "sockaddr_storage.h"
#include "mdns_plugin.h"
#include "mdns_records.h"


static struct mdns_plugin_mgr
mgr =
{
    .initialized = false,
};

struct mdns_plugin_mgr *
mdns_get_mgr(void)
{
    return &mgr;
}

/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int
mdns_session_cmp(const void *a, const void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a == p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}

/**
 * @brief get a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct mdns_session *
mdns_get_session(struct fsm_session *session)
{
    struct mdns_plugin_mgr *mgr;
    struct mdns_session *md_session;
    ds_tree_t *sessions;

    mgr = mdns_get_mgr();
    sessions = &mgr->fsm_sessions;

    md_session = ds_tree_find(sessions, session);
    if (md_session != NULL) return md_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    md_session = CALLOC(1, sizeof(struct mdns_session));

    md_session->initialized = false;
    ds_tree_insert(sessions, md_session, session);

    return md_session;
}


void
mdns_mgr_init(void)
{
    struct mdns_plugin_mgr *mgr;

    mgr = mdns_get_mgr();
    if (mgr->initialized) return;

    ds_tree_init(&mgr->fsm_sessions, mdns_session_cmp,
                 struct mdns_session, session_node);
    mgr->ovsdb_init = mdns_ovsdb_init;
    mgr->ovsdb_exit = mdns_ovsdb_exit;
    mgr->ctxt = NULL;

    mgr->initialized = true;
}

/**
 * @brief Frees a mdns session
 *
 * @param d_session the dns session to delete
 */
void
mdns_free_session(struct mdns_session *md_session)
{
    FREE(md_session);
}


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the mdns session to delete
 */
void
mdns_delete_session(struct fsm_session *session)
{
    struct mdns_plugin_mgr *mgr;
    struct mdns_session *md_session;
    ds_tree_t *sessions;

    mgr = mdns_get_mgr();
    sessions = &mgr->fsm_sessions;

    md_session = ds_tree_find(sessions, session);
    if (md_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);

    ds_tree_remove(sessions, md_session);
    mdns_free_session(md_session);
}

/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
mdns_plugin_exit(struct fsm_session *session)
{
    struct mdns_plugin_mgr *mgr;

    mgr = mdns_get_mgr();
    if (!mgr->initialized) return;

    mgr->ovsdb_exit();
    mdnsd_ctxt_exit();
    mdns_delete_session(session);

    mdns_records_exit();

    mgr->initialized = false;
}

static void
create_hex_dump(const char *fname, const uint8_t *buf, size_t len)
{
    int line_number = 0;
    bool new_line = true;
    size_t i;
    FILE *f;

    if (!LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))  return;

    f = fopen(fname, "w+");
    if (f == NULL) return;

    for (i = 0; i < len; i++)
    {
        new_line = (i == 0 ? true : ((i % 8) == 0));
        if (new_line)
        {
            if (line_number) fprintf(f, "\n");
            fprintf(f, "%06x", line_number);
            line_number += 8;
        }

        fprintf(f, " %02x", buf[i]);
    }

    fprintf(f, "\n");
    fclose(f);
}

static void
mdns_plugin_send_mdns_response(struct mdns_session *m_session)
{
    struct mdns_plugin_mgr      *mgr;
    struct mdnsd_context        *pctxt;

    unsigned short int          port;
    struct sockaddr_in          to;
    struct in_addr              ip;
    struct message              m;

    if (!m_session) return;

    mgr = mdns_get_mgr();
    pctxt = mgr->ctxt;
    if (!pctxt) return;

    while (mdnsd_out(pctxt->dmn, &m, &ip, &port))
    {
        unsigned char *buf;
        ssize_t sent_len;
        ssize_t len;

        memset(&to, 0, sizeof(to));
        to.sin_family = AF_INET;
        to.sin_port = htons(5353);
        to.sin_addr = ip;

        len = message_packet_len(&m);
        buf = message_packet(&m);

        create_hex_dump("/tmp/mdns_response.txtpcap", buf, len);

        sent_len = sendto(pctxt->ipv4_mcast_fd, buf, len, 0, (struct sockaddr *)&to,
                          sizeof(struct sockaddr_in));
        if (sent_len != len)
        {
            LOGD("%s: sending failed, error: '%s'", __func__, strerror(errno));
            return;
        }
    }
}

bool
mdns_populate_sockaddr(struct net_header_parser *parser,
                       struct sockaddr_storage *dst)
{
    struct ip6_hdr *hdrv6;
    struct iphdr *hdrv4;
    void *ip;

    if (parser->ip_version == 4)
    {
        hdrv4 = net_header_get_ipv4_hdr(parser);
        ip = &hdrv4->saddr;
        sockaddr_storage_populate(AF_INET, ip, dst);
        return true;
    }

    if (parser->ip_version == 6)
    {
        hdrv6 = net_header_get_ipv6_hdr(parser);
        ip = &hdrv6->ip6_src;
        sockaddr_storage_populate(AF_INET6, ip, dst);
        return true;
    }

    return false;
}

static void
mdns_plugin_process_message(struct mdns_session *m_session)
{
    char ip_src_str[INET_ADDRSTRLEN + 1] = { 0 };
    struct mdns_plugin_mgr      *mgr;
    struct mdnsd_context        *pctxt;
    struct mdns_parser          *mdns_parser;
    struct net_header_parser    *net_parser;
    uint16_t                    ethertype;
    int                         ip_protocol;
    struct udphdr               *hdr;

    struct sockaddr_storage ss;
    unsigned char *data;
    uint16_t mdns_default_port;
    struct message m;
    bool is_ip;
    bool ret;
    int rc = 0;

    if (!m_session) return;

    mdns_parser = &m_session->parser;
    net_parser = mdns_parser->net_parser;

    mgr = mdns_get_mgr();
    pctxt = mgr->ctxt;
    if (!pctxt) return;

    /* Some basic validation */
    /* Check ethertype */
    ethertype = net_header_get_ethertype(net_parser);
    is_ip = ((ethertype == ETH_P_IP) || (ethertype == ETH_P_IPV6));
    if (!is_ip) return;

    /* Check for UDP protocol */
    ip_protocol = net_parser->ip_protocol;
    if (ip_protocol != IPPROTO_UDP) return;

    /* Check the UDP src and dst ports (both need to be 5353) */
    hdr = net_parser->ip_pld.udphdr;
    mdns_default_port = htons(5353);
    if (!(hdr->source == mdns_default_port && hdr->dest == mdns_default_port)) return;

    /* check src ip same as POD ip */
    net_header_srcip_str(net_parser, ip_src_str, sizeof(ip_src_str));
    rc = strcmp(pctxt->srcip, ip_src_str);
    if (!rc)
    {
        LOGT("%s: Ignore packet from Pod with Source IP: %s", __func__, ip_src_str);
        return;
    }

    mdns_parser->parsed = net_parser->parsed;
    mdns_parser->data = net_parser->data;

    memset(&ss, 0, sizeof(ss));
    ret = mdns_populate_sockaddr(net_parser, &ss);
    if (!ret)
    {
        LOGE("%s: populate sockaddr failed", __func__);
        return;
    }

    memset(&m, 0, sizeof(m));

    /* Access udp data */
    data = net_parser->ip_pld.payload;
    data += sizeof(struct udphdr);

    /* Parse the message */
    message_parse(&m, data);
    rc = mdnsd_in(pctxt->dmn, &m, &ss);
    if (!rc)
    {
        LOGT("%s: Sending back the MDNS response", __func__);
        /* Send back a response */
        mdns_plugin_send_mdns_response(m_session);
    }
}

/**
 * @brief session packet processing entry point
 *
 * packet processing handler.
 * @param args the fsm session
 * @param h the pcap capture header
 * @param bytes a pointer to the captured packet
 */
void
mdns_plugin_handler(struct fsm_session *session,
                    struct net_header_parser *net_parser)
{
    struct mdns_session     *m_session;
    struct mdns_parser      *mdns_parser;

    if (!session || !net_parser)    return;

    m_session = (struct mdns_session *)session->handler_ctxt;

    mdns_parser = &m_session->parser;
    mdns_parser->caplen = net_parser->caplen;

    mdns_parser->net_parser = net_parser;

    mdns_plugin_process_message(m_session);
}

static void
mdns_plugin_update(struct fsm_session *session)
{
    struct mdns_session *f_session;
    char *mdns_report_interval;
    char *report_records;
    bool prev_enabled;
    long interval;
    int cmp;

    if (!session) return;

    f_session = (struct mdns_session *)session->handler_ctxt;

    prev_enabled = f_session->report_records;

    /* Check if MDNS records need to be reported */
    f_session->report_records = false; /* set the default value explicitly */
    report_records = session->ops.get_config(session, "report_records");
    if (report_records)
    {
        cmp = strcmp(report_records, "true");
        if (cmp == 0) f_session->report_records = true;
    }

    /* If report_records enabled now, initialize the report */
    if (!prev_enabled && f_session->report_records)
    {
        (void)mdns_records_init(f_session);
    }

    /* Get the MDNS record report interval and MQTT topic */
    mdns_report_interval = session->ops.get_config(session, "records_report_interval");
    if (mdns_report_interval)
    {
        interval = strtol(mdns_report_interval, NULL, 10);
        f_session->records_report_interval = interval;
    }
    else
    {
        /* No value provided, take default */
        f_session->records_report_interval = (long)DEFAULT_MDNS_RECORDS_REPORT_INTERVAL;
    }

    /* Get the targeted_devices and excluded_devices */
    f_session->targeted_devices = session->ops.get_config(session, "targeted_devices");
    f_session->excluded_devices = session->ops.get_config(session, "excluded_devices");

    /* Update mdnsd ctxt. */
    mdnsd_ctxt_update(f_session);
}

/**
 * @brief session packet periodic processing entry point
 *
 * Periodically called by the fsm manager
 * Sends a flow stats report.
 * @param session the fsm session
 */
void
mdns_plugin_periodic(struct fsm_session *session)
{
    struct mdns_session *f_session;
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct mdnsd_context *pctxt = mgr->ctxt;

    time_t now = time(NULL);
    double cmp_report;
    bool   send_report = false;

    if (session->topic == NULL || !pctxt) return;

    f_session = session->handler_ctxt;

    /* Report records only if enabled */
    if (f_session->report_records)
    {
        cmp_report  = now - f_session->records_report_ts;
        send_report = (cmp_report >= f_session->records_report_interval);

        /* Report to cloud via mqtt */
        if (send_report)    mdns_records_send_records(f_session);
    }
}

/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the packet parsing handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int
mdns_plugin_init(struct fsm_session *session)
{
    struct fsm_parser_ops *parser_ops;
    struct mdns_session *md_session;
    struct mdnsd_context *pctxt;
    struct mdns_plugin_mgr *mgr;
    char *mdns_report_interval;
    char *report_records;
    long interval;
    time_t now;
    int cmp;

    if (session == NULL) return -1;

    /* Initialize the manager on first call */
    mdns_mgr_init();
    mgr = mdns_get_mgr();
    mgr->loop = session->loop;
    mgr->ovsdb_init();

    /* Look up the dns session */
    md_session = mdns_get_session(session);
    if (md_session == NULL)
    {
        LOGE("%s: could not allocate mdns plugin", __func__);
        return -1;
    }

    /* Bail if the session is already initialized */
    if (md_session->initialized) return 0;

    /* Set the fsm session */
    session->ops.update = mdns_plugin_update;
    session->ops.periodic = mdns_plugin_periodic;
    session->ops.exit = mdns_plugin_exit;
    session->handler_ctxt = md_session;

    /* Set the handler ops */
    parser_ops = &session->p_ops->parser_ops;
    parser_ops->handler = mdns_plugin_handler;

    /* Wrap up the session initialization */
    md_session->session = session;

    /* Initialize mdnsd. */
    if (!mdnsd_ctxt_init(md_session)) goto err_plugin;

    /* Start the daemon */
    pctxt = mgr->ctxt;
    if (!pctxt)
    {
        LOGE("%s: mdnsd context is NULL", __func__);
        goto err_plugin;
    }

    if (!mdnsd_ctxt_start(pctxt))
    {
        LOGE("%s: mdnsd_daemon: Couldn't start the mdnsd daemon", __func__);
        goto err_plugin;
    }

    /* Check if MDNS records need to be reported */
    md_session->report_records = false;
    report_records = session->ops.get_config(session, "report_records");
    if (report_records)
    {
        cmp = strcmp(report_records, "true");
        if (cmp == 0) md_session->report_records = true;
    }

    /* Get the MDNS record report interval */
    mdns_report_interval = session->ops.get_config(session, "records_report_interval");
    if (mdns_report_interval)
    {
        interval = strtol(mdns_report_interval, NULL, 10);
        md_session->records_report_interval = interval;
    }
    else
    {
        /* No value provided, take default */
        md_session->records_report_interval = (long)DEFAULT_MDNS_RECORDS_REPORT_INTERVAL;
    }

    /* Get the targeted_devices and excluded_devices tags */
    md_session->targeted_devices = session->ops.get_config(session, "targeted_devices");
    md_session->excluded_devices = session->ops.get_config(session, "excluded_devices");

    now = time(NULL);
    md_session->records_report_ts = now;

    if (!mdns_records_init(md_session))
    {
        LOGE("%s: mdns_records_init() failed", __func__);
        goto err_plugin;
    }

    md_session->initialized = true;
    LOGD("%s: added session %s", __func__, session->name);

    return 0;

err_plugin:
    FREE(md_session);
    return -1;
}
