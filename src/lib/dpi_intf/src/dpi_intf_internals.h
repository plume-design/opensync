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

#ifndef DPI_INTF_INTERNALS_H_INCLUDED
#define DPI_INTF_INTERNALS_H_INCLUDED

#include <stdbool.h>
#include <pcap.h>
#include <ev.h>
#include <linux/if_packet.h>

#include "os_types.h"
#include "ds_tree.h"
#include "ovsdb_utils.h"
#include "dpi_intf.h"

#define DPI_INTF_IF_NAME_LEN 32
#define DPI_INTF_FILTER_LEN 512

struct dpi_intf_pcaps
{
    pcap_t *pcap;
    struct bpf_program *bpf;
    int pcap_fd;
    ev_io dpi_intf_evio;
    int pcap_datalink;
    int buffer_size;
    int cnt;
    int snaplen;
    int immediate;
    int started;
};


struct dpi_intf_entry
{
    char tap_if_name[DPI_INTF_IF_NAME_LEN];
    char tx_if_name[DPI_INTF_IF_NAME_LEN];
    char pcap_filter[DPI_INTF_FILTER_LEN];
    void *context;
    struct dpi_intf_pcaps *pcaps;
    ds_tree_t *other_config;
    os_macaddr_t src_eth_addr;
    struct sockaddr_ll raw_dst;
    ds_tree_node_t node;
    int sock_fd;
    bool active;
};


typedef bool (*init_forward_context_t)(struct dpi_intf_entry *entry);
typedef bool (*enable_pcap_t)(struct dpi_intf_entry *entry);
typedef void (*disable_pcap_t)(struct dpi_intf_entry *entry);

struct dpi_intf_ops
{
    init_forward_context_t init_forward_context;
    enable_pcap_t enable_pcap;
    disable_pcap_t disable_pcap;
};

struct dpi_intf_mgr
{
    void *registered_context;
    struct dpi_intf_ops ops;
    struct ev_loop *loop;
    char registrar_id[128];
    registrar_handler handler;
    ds_tree_t dpi_intfs;
    bool initialized;
};


struct dpi_intf_mgr *
dpi_intf_get_mgr(void);

void
dpi_intf_init_manager(void);

bool
dpi_intf_context_registered(void);

#endif /* DPI_INTF_INTERNALS_H_INCLUDED */
