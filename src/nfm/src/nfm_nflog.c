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

#include "log.h"
#include "qm_conn.h"

#include "memutil.h"
#include "nfm_mqtt.h"
#include "nfm_nflog.h"
#include "osn_nflog.h"

#include "opensync_nflog.pb-c.h"

static void nfm_nflog_packet_to_protobuf(Nflog *nm, struct osn_nflog_packet *np);
static osn_nflog_fn_t nfm_nflog_fn;

/*
 * This is the NFLOG group as specified by the --nflog-group iptables command.
 * The default value is 0.
 *
 * NFM will use this value to subscribe to NFLOG events.
 */
#define NFM_NFLOG_GROUP     0

static osn_nflog_t *nfm_nflog;

bool nfm_nflog_init(void)
{
    nfm_nflog = osn_nflog_new(0, nfm_nflog_fn);

    return (nfm_nflog != NULL);
}

void nfm_nflog_fini(void)
{
    if (nfm_nflog == NULL) return;

    osn_nflog_stop(nfm_nflog);
    osn_nflog_del(nfm_nflog);
}

void nfm_nflog_start(void)
{
    if (nfm_nflog == NULL) return;

    if (!osn_nflog_start(nfm_nflog))
    {
        LOG(ERR, "nflog: Error starting NFLOG.");
    }
}

void nfm_nflog_stop(void)
{
    if (nfm_nflog == NULL) return;

    osn_nflog_stop(nfm_nflog);
}

/*
 * ev_io watcher callback, process NFLOG messages here
 */
void nfm_nflog_fn(osn_nflog_t *nflog, struct osn_nflog_packet *np)
{
    (void)nflog;

    size_t bufsz;
    qm_response_t qr;

    Nflog nm = NFLOG__INIT;

    /* Do nothing if there's no MQTT topic */
    if (nfm_mqtt_topic[0] == '\0') return;

    nfm_nflog_packet_to_protobuf(&nm, np);

    bufsz = nflog__get_packed_size(&nm);
    uint8_t buf[bufsz];
    nflog__pack(&nm, buf);

    if (!qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, nfm_mqtt_topic, buf, bufsz, &qr))
    {
        LOG(ERR, "nflog: Error posting MQTT message.");
    }
}

void nfm_nflog_packet_to_protobuf(Nflog *nm, struct osn_nflog_packet *np)
{
    /*
     * Pack the received NFLOG message to a protobuf buffer
     */
    nm->node_id = nfm_mqtt_node_id[0] == '\0' ? NULL : nfm_mqtt_node_id;
    nm->node_id = nfm_mqtt_location_id[0] == '\0' ? NULL : nfm_mqtt_location_id;

    nm->ingress_ifname = np->nfp_indev[0] == '\0' ? NULL : np->nfp_indev;
    nm->egress_ifname = np->nfp_outdev[0] == '\0' ? NULL : np->nfp_outdev;
    nm->ingress_phyifname = np->nfp_physindev[0] == '\0' ? NULL : np->nfp_physindev;
    nm->egress_phyifname = np->nfp_physoutdev[0] == '\0' ? NULL : np->nfp_physoutdev;

    if (np->nfp_timestamp > 0)
    {
        nm->has_timestamp = true;
        nm->timestamp = np->nfp_timestamp;
    }

    if (np->nfp_fwmark != 0)
    {
        nm->has_fw_mark = true;
        nm->fw_mark = np->nfp_fwmark;
    }

    nm->hw_addr = np->nfp_hwaddr[0] == '\0' ? NULL : np->nfp_hwaddr;
    nm->prefix = np->nfp_prefix[0] == '\0' ? NULL : np->nfp_prefix;

    nm->has_nl_group_id = true;
    nm->nl_group_id = np->nfp_group_id;

    nm->has_payload = (np->nfp_payload_len > 0);
    nm->payload.data = np->nfp_payload;
    nm->payload.len = np->nfp_payload_len;

    nm->has_hwheader = (np->nfp_hwheader_len > 0);
    nm->hwheader.data = np->nfp_hwheader;
    nm->hwheader.len = np->nfp_hwheader_len;

    LOG(INFO, "nfm_nflog: Received packet: prefix=%s ingress=%s egress=%s hwaddr=%s hwheader_len=%zd payload_len=%zd",
            nm->prefix == NULL ? "/" : nm->prefix,
            nm->ingress_ifname == NULL ? "/" : nm->ingress_ifname,
            nm->egress_ifname == NULL ? "/" : nm->egress_ifname,
            nm->hw_addr == NULL ? "/" : nm->hw_addr,
            nm->hwheader.len,
            nm->payload.len);
}

