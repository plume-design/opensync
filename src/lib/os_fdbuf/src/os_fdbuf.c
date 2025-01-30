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

/**
 * os_fdbuf - OpenSync FDB Update Frame
 *
 * Provides helpers to:
 * - transmit (announce) FDB update to neighbor nodes
 * - receive (respect) FDB update locally
 *
 * Intention:
 * This special frame is intended to allow
 * explicit control over flushing of FDB
 * (Forwarding Database) of L2 neighbors.
 *
 * This is necessary to facilitate smooth and
 * correct reprogramming of mesh topologies where
 * datapaths change.
 */

#define _GNU_SOURCE

#include <log.h>
#include <const.h>
#include <util.h>
#include <os_nif.h>
#include <net/ethernet.h>
#include <os_send_raw.h>
#include <os_llc_snap.h>
#include <hw_acc.h>
#include <ff_lib.h>
#include <glob.h>

#define LOG_PREFIX(fmt, ...) "os_fdbuf: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_IFNAME(if_name, fmt, ...) LOG_PREFIX("%s: " fmt, if_name, ##__VA_ARGS__)
#define LOG_PREFIX_ACCEL(if_name, fmt, ...) LOG_PREFIX_IFNAME(if_name, "accel: " fmt, ##__VA_ARGS__)
#define LOG_PREFIX_FDB(if_name, fmt, ...) LOG_PREFIX_IFNAME(if_name, "flush: " fmt, ##__VA_ARGS__)
#define LOG_PREFIX_FDB_LINUX(if_name, fmt, ...) LOG_PREFIX_FDB(if_name, "linux: " fmt, ##__VA_ARGS__)
#define LOG_PREFIX_FDB_OVS(if_name, fmt, ...) LOG_PREFIX_FDB(if_name, "ovs: " fmt, ##__VA_ARGS__)

struct os_fdbuf_frame {
    struct ether_header eh;
    struct os_llc_snap_header llc_snap;
} __attribute__((packed));

static int
os_fdbuf_fill_eh(const char *if_name,
                 struct os_fdbuf_frame *frm)
{
    os_macaddr_t mac;
    const bool ok = os_nif_macaddr_get(if_name, &mac);
    const bool failed = (ok == false);
    if (failed) return -1;

    memset(frm->eh.ether_dhost, 0xff, ETHER_ADDR_LEN);
    memcpy(frm->eh.ether_shost, mac.addr, ETHER_ADDR_LEN);
    frm->eh.ether_type = htons(sizeof(struct os_llc_snap_header));
    return 0;
}

static char *
strsep_nth(char *bridge_port_path, const int word_number, const char *separator) {
    int i = 0;
    while(i < word_number - 1) // Minus one because last word is processed after loop
    {
        strsep(&bridge_port_path, separator);
        i++;
    }
    return strsep(&bridge_port_path, separator);
}

static void
os_fdbuf_flush_bridge_ports()
{
    glob_t g;
    const int bridge_port_name_word_number = 5; // Bridge port name word number in system path
    int ret = glob("/sys/class/net/*/master", 0, NULL, &g);
    if (ret != 0) {
        LOGE("os_fdbuf: glob() failed with error code %d", ret);
        return;
    }

    for (size_t i = 0; i < g.gl_pathc; i++) {
        char *bridge_port_name = strsep_nth(g.gl_pathv[i], bridge_port_name_word_number, "/");
        const char *result = strexa("ip", "link", "set", "dev", bridge_port_name, "type", "bridge_slave", "fdb_flush");
        LOGD(LOG_PREFIX_FDB_LINUX(bridge_port_name, "%s", result == NULL ? "not flushed" : "flushed"));
    }
    globfree(&g);
}

static void
os_fdbuf_flush_linux(const char *if_name)
{
    const char *result = strexa("bridge", "fdb", "flush", "dev", if_name);
    LOGD(LOG_PREFIX_FDB_LINUX(if_name, "%s", result == NULL ? "not flushed" : "flushed"));
    if (result == NULL) {
        os_fdbuf_flush_bridge_ports();
    }
}

static void
os_fdbuf_flush_accel(const char *if_name)
{
    /* hw_acc_flush_flow_per_device() expects unknown devid.
     * Just flush everything for good measure.
     */
    LOGD(LOG_PREFIX_ACCEL(if_name, "flushing everything"));
    hw_acc_flush_all_flows();
}

static void
os_fdbuf_flush(const char *if_name)
{
    LOGD(LOG_PREFIX_FDB(if_name, "flushing"));
    os_fdbuf_flush_linux(if_name);
    os_fdbuf_flush_accel(if_name);
}

static void
os_fdbuf_ingest_dump(const char *if_name,
                     const void *pkt,
                     size_t len)
{
    const size_t hex_size = (len * 2) + 1;
    char *hex = MALLOC(hex_size);
    const int rv = bin2hex(pkt, len, hex, hex_size);
    const bool ok = (rv == 0);
    const bool bin2hex_failed = !ok;
    WARN_ON(bin2hex_failed);
    if (ok) {
        LOGT(LOG_PREFIX_IFNAME(if_name, "hex: %s", hex));
    }
    FREE(hex);
}

void
os_fdbuf_ingest(const char *if_name,
                const void *pkt,
                size_t len)
{
    LOGD(LOG_PREFIX_IFNAME(if_name, "ingesting"));
    os_fdbuf_ingest_dump(if_name, pkt, len);

    const struct os_fdbuf_frame *frm = pkt;
    const bool too_short = (len < sizeof(*frm));
    if (too_short) return;
    const uint16_t protocol = OS_LLC_SNAP_PROTOCOL_FDBUF;
    const bool llc_snap_mismatch = (os_llc_snap_header_matches(&frm->llc_snap, protocol) == false);
    if (llc_snap_mismatch) return;

    os_fdbuf_flush(if_name);
}

int
os_fdbuf_announce(const char *if_name)
{
    if (ff_is_flag_enabled("fdb_update_frame_handling") == false) return 0;
    struct os_fdbuf_frame frm = {0};

    LOGD(LOG_PREFIX_IFNAME(if_name, "announcing"));

    const int err = os_fdbuf_fill_eh(if_name, &frm);
    if (err) return err;
    os_llc_snap_header_fill(&frm.llc_snap, OS_LLC_SNAP_PROTOCOL_FDBUF);

    return os_send_raw_eth_packet_ifname(if_name, &frm, sizeof(frm));
}
