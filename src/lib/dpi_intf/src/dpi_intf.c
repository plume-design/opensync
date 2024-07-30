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

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include "os_types.h"
#include "ovsdb_utils.h"
#include "os.h"
#include "os_nif.h"
#include "util.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_utils.h"
#include "schema.h"
#include "log.h"
#include "memutil.h"
#include "dpi_intf.h"
#include "dpi_stats.h"
#include "dpi_intf_internals.h"
#include "net_header_parse.h"

static ovsdb_table_t table_Dpi_Interface_Map;

/* Set of default values for pcaps settings */
static int g_buf_size = 0;
static int g_cnt = 1;
static int g_immediate_mode = 1;

#if defined(CONFIG_FSM_PCAP_SNAPLEN) && (CONFIG_FSM_PCAP_SNAPLEN > 0)
static int g_snaplen = CONFIG_FSM_PCAP_SNAPLEN;
#else
static int g_snaplen = 2048;
#endif

static struct dpi_intf_mgr dpi_intf_mgr =
{
    .initialized = false,
};

static int
dpi_intf_if_name_cmp(const void *a, const void *b)
{
    return strcmp(a, b);
}


struct dpi_intf_mgr *
dpi_intf_get_mgr(void)
{
    return &dpi_intf_mgr;
}


/**
 * @brief retrieves the value from the provided key in the
 * other_config value
 *
 * @param entry the dpi intf entry owning the other_config
 * @param conf_key other_config key to look up
 */
char *
dpi_intf_get_other_config_val(struct dpi_intf_entry *entry, char *key)
{
    struct str_pair *pair;
    ds_tree_t *tree;

    if (entry == NULL) return NULL;

    tree = entry->other_config;
    if (tree == NULL) return NULL;

    pair = ds_tree_find(tree, key);
    if (pair == NULL) return NULL;

    return pair->value;
}


/**
 * @brief parse a session's pcap options from ovsdb.
 *
 * @param session the session
 * @return true if the pcap session needs to be restarted, false otherwise.
 */
bool
dpi_intf_get_pcap_options(struct dpi_intf_entry *entry)
{
    struct dpi_intf_pcaps *pcaps;
    char *buf_size_str;
    char *snaplen_str;
    char *mode_str;
    int prev_value;
    char *cnt_str;
    bool started;
    bool restart;
    char *iface;
    long value;

    pcaps = entry->pcaps;
    if (pcaps == NULL) return false;

    started = (pcaps->started != 0);

    iface = entry->tap_if_name;
    if (iface == NULL) return false;

    restart = false;

    /* Check the buffer size option */
    prev_value = (started ? pcaps->buffer_size : g_buf_size);
    if (!started) pcaps->buffer_size = g_buf_size;
    buf_size_str = dpi_intf_get_other_config_val(entry, "pcap_bsize");
    if (buf_size_str != NULL)
    {
        errno = 0;
        value = strtol(buf_size_str, NULL, 10);
        if (errno != 0)
        {
            LOGD("%s: error reading value %s: %s", __func__,
                 buf_size_str, strerror(errno));
        }
        else pcaps->buffer_size = (int)value;
    }
    LOGD("%s: %s: buffer size: %d", __func__, iface, pcaps->buffer_size);
    restart |= (started && (prev_value != pcaps->buffer_size));
    if (restart)
    {
        LOGI("%s: %s: buf size changed from %d to %d. Will restart pcap socket",
             __func__, iface, prev_value, pcaps->buffer_size);
    }

    /* Check the snaplen option */
    prev_value = (started ? pcaps->snaplen : g_snaplen);
    if (!started) pcaps->snaplen = g_snaplen;
    snaplen_str = dpi_intf_get_other_config_val(entry, "pcap_snaplen");
    if (snaplen_str != NULL)
    {
        errno = 0;
        value = strtol(snaplen_str, NULL, 10);
        if (errno != 0)
        {
            LOGD("%s: error reading value %s: %s", __func__,
                 snaplen_str, strerror(errno));
        }
        else pcaps->snaplen = (int)value;
    }
    LOGD("%s: %s: snaplen: %d", __func__, iface, pcaps->snaplen);
    restart |= (started && (prev_value != pcaps->snaplen));
    if (restart)
    {
        LOGI("%s: %s: snaplen changed from %d to %d. Will restart pcap socket",
             __func__, iface, prev_value, pcaps->snaplen);
    }

    /* Check the immediate mode option */
    prev_value = (started ? pcaps->immediate : g_immediate_mode);
    if (!started) pcaps->immediate = g_immediate_mode;
    mode_str = dpi_intf_get_other_config_val(entry, "pcap_immediate");
    if (mode_str != NULL)
    {
        pcaps->immediate = (strcmp(mode_str, "no") ? 1 : 0);
    }
    LOGD("%s: %s: immediate mode: %d", __func__, iface, pcaps->immediate);
    restart |= (started && (prev_value != pcaps->immediate));
    if (restart)
    {
        LOGI("%s: %s: immediate mode changed from %d to %d. "
             "Will restart pcap socket",
             __func__, iface, prev_value, pcaps->immediate);
    }

    pcaps->cnt = g_cnt;
    cnt_str = dpi_intf_get_other_config_val(entry, "pcap_cnt");
    if (cnt_str != NULL)
    {
        errno = 0;
        value = strtol(cnt_str, NULL, 10);
        if (errno != 0)
        {
            LOGD("%s: error reading value %s: %s", __func__,
                 cnt_str, strerror(errno));
        }
        else pcaps->cnt = (int)value;
    }
    LOGD("%s: %s: delivery count: %d", __func__, iface, pcaps->cnt);

    if (restart)
    {
        LOGEM("%s: detected pcap settings changes, restarting", __func__);
        exit(EXIT_SUCCESS);
    }

    return restart;
}


bool
dpi_intf_set_pcap_options(struct dpi_intf_entry *entry)
{
    struct dpi_intf_pcaps *pcaps;
    pcap_t *pcap;
    char *iface;
    bool ret;
    int rc;

    pcaps = entry->pcaps;
    if (pcaps == NULL) return false;

    pcap = pcaps->pcap;
    if (pcap == NULL) return false;

    iface = entry->tap_if_name;
    if (iface == NULL) return false;

    ret = true;

    /* set pcap buffer size */
    LOGI("%s: setting %s buffer size to %d",
         __func__, iface, pcaps->buffer_size);
    rc = pcap_set_buffer_size(pcap, pcaps->buffer_size);
    if (rc != 0)
    {
        LOGW("%s: Unable to set %s pcap buffer size",
             __func__, iface);
        ret = false;
    }

    /* set pcap immediate mode */
    LOGI("%s: %ssetting %s in immediate mode",
         __func__, (pcaps->immediate == 1) ? "" : "_not_ ", iface);
    rc = pcap_set_immediate_mode(pcap, pcaps->immediate);
    if (rc != 0)
    {
        LOGW("%s: Unable to set %s pcap immediate mode",
             __func__, iface);
        ret = false;
    }

    /* set pcap snap length */
    LOGI("%s: setting %s snap length to %d",
         __func__, iface, pcaps->snaplen);
    rc = pcap_set_snaplen(pcap, pcaps->snaplen);
    if (rc != 0)
    {
        LOGW("%s: unable to set %s snaplen to %d", __func__,
             iface, pcaps->snaplen);
        ret = false;
    }

    return ret;
}

static void
dpi_intf_pcap_handler(uint8_t * args, const struct pcap_pkthdr *header,
                      const uint8_t *bytes)
{
    struct net_header_parser net_parser;
    struct dpi_intf_entry *entry;
    struct dpi_intf_mgr *mgr;
    size_t len;

    entry = (struct dpi_intf_entry *)args;
    mgr = dpi_intf_get_mgr();

    MEMZERO(net_parser);
    net_parser.packet_len = header->caplen;
    net_parser.caplen = header->caplen;
    net_parser.data = (uint8_t *)bytes;
    net_parser.pcap_datalink = entry->pcaps->pcap_datalink;
    net_parser.raw_dst = &entry->raw_dst;
    net_parser.src_eth_addr = &entry->src_eth_addr;
    net_parser.sock_fd = entry->sock_fd;
    net_parser.payload_updated = false;
    net_parser.tap_intf = entry->tap_if_name;
    len = net_header_parse(&net_parser);
    if (len == 0) return;

    mgr->handler(entry->context, &net_parser);
}


static void
dpi_intf_pcap_recv_fn(EV_P_ ev_io *ev, int revents)
{
    (void)loop;
    (void)revents;

    struct dpi_intf_entry *entry;
    struct dpi_intf_pcaps *pcaps;
    pcap_t *pcap;

    entry = ev->data;
    if (entry == NULL) return;

    pcaps = entry->pcaps;
    if (pcaps == NULL) return;

    pcap = pcaps->pcap;
    pcap_dispatch(pcap, pcaps->cnt, dpi_intf_pcap_handler, (void *)entry);
}


bool
dpi_intf_pcap_open(struct dpi_intf_entry *entry)
{
    char pcap_err[PCAP_ERRBUF_SIZE];
    struct dpi_intf_pcaps *pcaps;
    struct dpi_intf_mgr *mgr;
    struct bpf_program *bpf;
    char *pkt_filter;
    pcap_t *pcap;
    char *iface;
    bool ret;
    int rc;

    iface = entry->tap_if_name;
    if (iface == NULL) return false;

    mgr = dpi_intf_get_mgr();

    pcaps = entry->pcaps;
    if (pcaps == NULL) return false;

    pcaps->pcap = pcap_create(iface, pcap_err);
    if (pcaps->pcap == NULL)
    {
        LOGE("%s: PCAP initialization failed for interface %s", __func__,
             iface);
        goto error;
    }

    ret = dpi_intf_set_pcap_options(entry);
    if (!ret) goto error;

    pcap = pcaps->pcap;

    rc = pcap_setnonblock(pcap, 1, pcap_err);
    if (rc == -1)
    {
        LOGE("%s: Unable to set non-blocking mode: %s", __func__,
             pcap_err);
        goto error;
    }

    /*
     * We do not want to block forever on receive. A timeout 0 means block
     * forever, so use 1ms for the timeout.
     */
    rc = pcap_set_timeout(pcap, 1);
    if (rc != 0)
    {
        LOGE("%s: %s: Error setting buffer timeout.", __func__,
            iface);
        goto error;
    }

    /* Activate the interface */
    rc = pcap_activate(pcap);
    if (rc != 0)
    {
        LOGE("%s: Error activating interface %s: %s", __func__,
             iface, pcap_geterr(pcap));
        goto error;
    }

    if ((pcap_datalink(pcap) != DLT_EN10MB) &&
        (pcap_datalink(pcap) != DLT_LINUX_SLL))
    {
        LOGE("%s: unsupported data link layer: %d", __func__,
             pcap_datalink(pcap));
        goto error;
    }
    pcaps->pcap_datalink = pcap_datalink(pcap);

    bpf = pcaps->bpf;
    pkt_filter = entry->pcap_filter;
    rc = pcap_compile(pcap, bpf, pkt_filter, 0, PCAP_NETMASK_UNKNOWN);
    if (rc != 0)
    {
        LOGE("%s: Error compiling capture filter: '%s'. PCAP error: %s", __func__,
             pkt_filter, pcap_geterr(pcap));
        goto error;
    }

    rc = pcap_setfilter(pcap, bpf);
    if (rc != 0)
    {
        LOGE("%s: Error setting the capture filter, error: %s", __func__,
             pcap_geterr(pcap));
        goto error;
    }

    /* We need a selectable fd for libev */
    pcaps->pcap_fd = pcap_get_selectable_fd(pcap);
    if (pcaps->pcap_fd < 0)
    {
        LOGE("%s: Error getting selectable FD (%d). PCAP error: %s", __func__,
             pcaps->pcap_fd, pcap_geterr(pcap));
        goto error;
    }

    /* Register FD for libev events */
    ev_io_init(&pcaps->dpi_intf_evio, dpi_intf_pcap_recv_fn, pcaps->pcap_fd, EV_READ);

    /* Set user data */
    pcaps->dpi_intf_evio.data = (void *)entry;

    /* Start watching it on the default queue */
    ev_io_start(mgr->loop, &pcaps->dpi_intf_evio);
    pcaps->started = 1;

    return true;

 error:
    LOGE("%s: Interface %s registered for snooping returning error.", __func__,
         iface);

    return false;
}


static void
dpi_intf_pcap_close(struct dpi_intf_entry *entry)
{
    struct dpi_intf_pcaps *pcaps;
    struct dpi_intf_mgr *mgr;
    pcap_t *pcap;

    mgr = dpi_intf_get_mgr();

    pcaps = entry->pcaps;
    if (pcaps == NULL) return;

    pcap = pcaps->pcap;

    if (ev_is_active(&pcaps->dpi_intf_evio))
    {
        ev_io_stop(mgr->loop, &pcaps->dpi_intf_evio);
    }

    if (pcaps->bpf != NULL) {
        pcap_freecode(pcaps->bpf);
        FREE(pcaps->bpf);
        pcaps->bpf = NULL;
    }

    if (pcap != NULL) {
        pcap_close(pcap);
        pcaps->pcap = NULL;
    }
    FREE(pcaps);
    entry->pcaps = NULL;
}


static bool
dpi_intf_enable_pcap(struct dpi_intf_entry *entry)
{
    bool ret;

    ret = dpi_intf_get_pcap_options(entry);
    if (entry->pcaps != NULL) dpi_intf_pcap_close(entry);

    if (entry->pcaps == NULL)
    {
        struct dpi_intf_pcaps *pcaps;
        struct bpf_program *bpf;

        pcaps = CALLOC(1, sizeof(struct dpi_intf_pcaps));
        if (pcaps == NULL) return false;
        entry->pcaps = pcaps;

        bpf = CALLOC(1, sizeof(struct bpf_program));
        if (bpf == NULL) goto err_free_pcaps;

        entry->pcaps->bpf = bpf;
    }

    ret = dpi_intf_get_pcap_options(entry);
    ret = dpi_intf_pcap_open(entry);
    if (!ret)
    {
        LOGE("%s: pcap open failed for interface %s, restarting fsm", __func__,
             entry->tap_if_name);

        /* interface might not be configured yet, wait and restart fsm */
        sleep(4);
        exit(EXIT_SUCCESS);
    }

    entry->active = true;

    return true;

err_free_pcaps:
    FREE(entry->pcaps);
    entry->pcaps = NULL;

    return false;
}


/**
 * @brief initialize forward context
 *
 * @param entry the dpi_intf_entry to be initialized
 */
static bool
dpi_intf_init_forward_context(struct dpi_intf_entry *entry)
{
    struct ifreq ifreq_c;
    struct ifreq ifr_i;
    int sockfd;

    sockfd = socket(AF_PACKET, SOCK_RAW, 0);

    if (sockfd == -1)
    {
        LOGE("%s: failed to open socket (%s)", __func__, strerror(errno));
        return false;
    }

    MEMZERO(ifr_i);
    STRSCPY(ifr_i.ifr_name, entry->tx_if_name);

    if ((ioctl(sockfd, SIOCGIFINDEX, &ifr_i)) < 0)
    {
        LOGE("%s: error in index ioctl reading (%s)", __func__, strerror(errno));
        goto err_sockfd;
    }

    entry->sock_fd = sockfd;
    entry->raw_dst.sll_family = PF_PACKET;
    entry->raw_dst.sll_ifindex = ifr_i.ifr_ifindex;
    entry->raw_dst.sll_halen = ETH_ALEN;

    MEMZERO(ifreq_c);
    STRSCPY(ifreq_c.ifr_name, entry->tx_if_name);

    if ((ioctl(sockfd, SIOCGIFHWADDR, &ifreq_c)) < 0)
    {
        LOGE("%s: error in SIOCGIFHWADDR ioctl reading (%s)", __func__, strerror(errno));
        goto err_sockfd;
    }

    memcpy(entry->src_eth_addr.addr, ifreq_c.ifr_hwaddr.sa_data,
           sizeof(entry->src_eth_addr.addr));

    return true;

err_sockfd:
    close(sockfd);
    return false;
}



/**
 * @brief validate an ovsdb to private object conversion
 *
 * Expects a non NULL converted object if the number of elements
 * was not null, a null object otherwise.
 * @param converted converted object
 * @param len number of elements of the ovsdb object
 */
static bool
dpi_intf_check_conversion(void *converted, int len)
{
    if (len == 0) return true;
    if (converted == NULL) return false;

    return true;
}


static void
dpi_intf_add_ovsdb_entry(struct schema_Dpi_Interface_Map *node_cfg)
{
    struct dpi_intf_entry *entry;
    struct dpi_intf_mgr *mgr;
    ds_tree_t *other_config;
    bool check;

    mgr = dpi_intf_get_mgr();

    /* Get the pcap config options */
    other_config = schema2tree(sizeof(node_cfg->other_config_keys[0]),
                               sizeof(node_cfg->other_config[0]),
                               node_cfg->other_config_len,
                               node_cfg->other_config_keys,
                               node_cfg->other_config);
    check = dpi_intf_check_conversion(other_config, node_cfg->other_config_len);
    if (!check) return;

    /* Create an entry */
    entry = CALLOC(1, sizeof(*entry));
    strscpy(entry->tap_if_name, node_cfg->tap_if_name, sizeof(entry->tap_if_name));
    strscpy(entry->tx_if_name, node_cfg->tx_if_name, sizeof(entry->tx_if_name));
    entry->active = false;

    ds_tree_insert(&mgr->dpi_intfs, entry, entry->tap_if_name);

    /* Check if a registrar called in */
    check = dpi_intf_context_registered();
    if (!check) return;

    entry->context = mgr->registered_context;
    mgr->ops.init_forward_context(entry);

    if (strlen(node_cfg->pkt_capt_filter) != 0)
    {
        STRSCPY(entry->pcap_filter, node_cfg->pkt_capt_filter);
    }

    /* Enable tapping on the interface */
    check = mgr->ops.enable_pcap(entry);
    if (!check)
    {
        LOGD("%s: failed to enable tapping on %s", __func__,
             entry->tap_if_name);
    }
}


static void
dpi_intf_delete_entry(struct dpi_intf_entry *entry)
{
    if (entry == NULL) return;

    LOGD("%s: removing entry %s", __func__,
         entry->tap_if_name);

    free_str_tree(entry->other_config);
    FREE(entry);
}


static void
dpi_intf_delete_ovsdb_entry(struct schema_Dpi_Interface_Map *old_rec)
{
    struct dpi_intf_entry *entry;
    struct dpi_intf_mgr *mgr;
    ds_tree_t *tree;
    bool check;

    mgr = dpi_intf_get_mgr();
    tree = &mgr->dpi_intfs;

    entry = ds_tree_find(tree, old_rec->tap_if_name);
    if (entry == NULL) return;

    check = dpi_intf_context_registered();
    if (check)
    {
        mgr->ops.disable_pcap(entry);
    }

    ds_tree_remove(tree, entry);
    dpi_intf_delete_entry(entry);
}


/**
 * @brief registered callback for Data_Report_Tags events
 */
static void
callback_Dpi_Interface_Map(ovsdb_update_monitor_t *mon,
                           struct schema_Dpi_Interface_Map *old_rec,
                           struct schema_Dpi_Interface_Map *node_cfg)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        LOGI("%s: new dpi tap interface entry: %s", __func__, node_cfg->tap_if_name);
        dpi_intf_add_ovsdb_entry(node_cfg);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        LOGI("%s: deleted dpi tap interface entry: %s", __func__, old_rec->tap_if_name);
        dpi_intf_delete_ovsdb_entry(old_rec);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        LOGI("%s: updated dpi tap intf entry %s", __func__, node_cfg->tap_if_name);
    }
}

void
dpi_intf_get_pcap_stats(void)
{
    struct dpi_intf_entry *entry;
    struct dpi_intf_pcaps *pcaps;
    struct dpi_intf_mgr *mgr;
    struct pcap_stat stats;
    ds_tree_t *tree;
    pcap_t *pcap;
    int rc;

    mgr = dpi_intf_get_mgr();
    tree = &mgr->dpi_intfs;

    /* loop through all the configured interfaces */
    ds_tree_foreach(tree, entry)
    {
        pcaps = entry->pcaps;
        if (pcaps == NULL) continue;

        pcap = pcaps->pcap;
        memset(&stats, 0, sizeof(stats));
        /* get the pcap stats */
        rc = pcap_stats(pcap, &stats);
        if (rc < 0)
        {
            LOGT("%s: pcap_stats failed: %s",
                __func__, pcap_geterr(pcap));
            return;
        }

        LOGI("%s: %s: packets received: %u, dropped: %u",
         __func__, entry->tap_if_name, stats.ps_recv, stats.ps_drop);

        /* store the read pcap stats */
        dpi_stats_store_pcap_stats(&stats, entry->tap_if_name);
    }
}

bool
dpi_intf_context_registered(void)
{
    struct dpi_intf_mgr *mgr;

    mgr = dpi_intf_get_mgr();

    if (!mgr->initialized) return false;

    return (mgr->registered_context != NULL);
}


/**
 * @brief register pcap callback ant its context
 */
void
dpi_intf_register_context(struct dpi_intf_registration *context)
{
    struct dpi_intf_entry *entry;
    struct dpi_intf_mgr *mgr;
    ds_tree_t *tree;
    bool check;

    mgr = dpi_intf_get_mgr();

    if (!mgr->initialized)
    {
        LOGD("%s: module not initialized", __func__);
        return;
    }

    check = dpi_intf_context_registered();
    if (check)
    {
        LOGD("%s: %s has already registered", __func__, mgr->registrar_id);
        return;
    }

    if (context->id == NULL)
    {
        LOGD("%s: %s has already registered", __func__, mgr->registrar_id);
        return;
    }

    mgr->loop = context->loop;
    mgr->registered_context = context->context;
    mgr->handler = context->handler;
    strscpy(mgr->registrar_id, context->id, sizeof(mgr->registrar_id));

    tree = &mgr->dpi_intfs;
    entry = ds_tree_head(tree);
    while (entry != NULL)
    {
        entry->context = mgr->registered_context;
        check = mgr->ops.init_forward_context(entry);
        if (check)
        {
            check = mgr->ops.enable_pcap(entry);
        }
        if (!check)
        {
            LOGD("%s: failed to enable tapping on %s", __func__,
                 entry->tap_if_name);
        }
        entry = ds_tree_next(tree, entry);
    }

    LOGI("%s: registered %s", __func__, mgr->registrar_id);
}


void
dpi_intf_init_manager(void)
{
    struct dpi_intf_mgr *mgr;

    mgr = dpi_intf_get_mgr();

    if (mgr->initialized) return;

    LOGD("%s: initializing", __func__);

    ds_tree_init(&mgr->dpi_intfs, dpi_intf_if_name_cmp,
                 struct dpi_intf_entry, node);

    mgr->registered_context = NULL;
    mgr->ops.init_forward_context = dpi_intf_init_forward_context;
    mgr->ops.enable_pcap = dpi_intf_enable_pcap;
    mgr->ops.disable_pcap = dpi_intf_pcap_close;
    mgr->initialized = true;
}


void
dpi_intf_init(void)
{
    struct dpi_intf_mgr *mgr;

    mgr = dpi_intf_get_mgr();

    if (mgr->initialized)
    {
        LOGT("%s: already initialized", __func__);
        return;
    }

    dpi_intf_init_manager();

    OVSDB_TABLE_INIT_NO_KEY(Dpi_Interface_Map);
    OVSDB_TABLE_MONITOR(Dpi_Interface_Map, false);
}


void
dpi_intf_exit(void)
{
    struct dpi_intf_entry *entry;
    struct dpi_intf_mgr *mgr;
    ds_tree_t *tree;

    mgr = dpi_intf_get_mgr();

    if (mgr->initialized == false) return;

    /* Delete all dpi_intf entries */
    tree = &mgr->dpi_intfs;
    entry = ds_tree_head(tree);

    while (entry != NULL)
    {
        struct dpi_intf_entry *remove;
        struct dpi_intf_entry *next;

        next = ds_tree_next(tree, entry);
        remove = entry;
        entry = next;
        ds_tree_remove(tree, remove);
        dpi_intf_delete_entry(remove);
    }


    mgr->initialized = false;
}
