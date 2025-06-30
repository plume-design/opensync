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

#include "pm_hw_acc_load_netstats.h"

#include <string.h>

#include <memutil.h>
#include <util.h>
#include <ds_tree.h>

struct pm_hw_acc_load_netstats_netdev
{
    ds_tree_node_t node;
    struct pm_hw_acc_load_netstats *stats;
    char *if_name;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t tx_pkts;
    uint64_t rx_pkts;
};

struct pm_hw_acc_load_netstats
{
    ds_tree_t netdevs;
};

static struct pm_hw_acc_load_netstats_netdev *pm_hw_acc_load_netstats_netdev_alloc(
        struct pm_hw_acc_load_netstats *stats,
        const char *if_name)
{
    struct pm_hw_acc_load_netstats_netdev *netdev = CALLOC(1, sizeof(*netdev));
    netdev->if_name = STRDUP(if_name);
    netdev->stats = stats;
    ds_tree_insert(&stats->netdevs, netdev, netdev->if_name);
    return netdev;
}

static struct pm_hw_acc_load_netstats_netdev *pm_hw_acc_load_netstats_netdev_get(
        struct pm_hw_acc_load_netstats *stats,
        const char *if_name)
{
    return ds_tree_find(&stats->netdevs, if_name) ?: pm_hw_acc_load_netstats_netdev_alloc(stats, if_name);
}

static void pm_hw_acc_load_netstats_netdev_drop(struct pm_hw_acc_load_netstats_netdev *netdev)
{
    if (netdev == NULL) return;
    ds_tree_remove(&netdev->stats->netdevs, netdev);
    FREE(netdev->if_name);
    FREE(netdev);
}

static void pm_hw_acc_load_netstats_parse(struct pm_hw_acc_load_netstats *stats, char *lines)
{
    if (lines == NULL) return;
    char *line;
    const char *hdr1 = strsep(&lines, "\n");
    const char *hdr2 = strsep(&lines, "\n");
    if (hdr1 == NULL) return;
    if (hdr2 == NULL) return;

    while ((line = strsep(&lines, "\n")) != NULL)
    {
        const char *delim = " :\t";
        char *tmp = NULL;
        char *if_name = strtok_r(line, delim, &tmp);

        if (if_name == NULL) continue;
        if (if_name[0] == '\0') continue;

        struct pm_hw_acc_load_netstats_netdev *netdev = pm_hw_acc_load_netstats_netdev_get(stats, if_name);
        if (netdev == NULL) continue;

        const char *rx_bytes = strtok_r(NULL, " \t", &tmp);
        const char *rx_pkts = strtok_r(NULL, " \t", &tmp);
        const char *rx_errs = strtok_r(NULL, " \t", &tmp);
        const char *rx_drop = strtok_r(NULL, " \t", &tmp);
        const char *rx_fifo = strtok_r(NULL, " \t", &tmp);
        const char *rx_frame = strtok_r(NULL, " \t", &tmp);
        const char *rx_compressed = strtok_r(NULL, " \t", &tmp);
        const char *rx_multicast = strtok_r(NULL, " \t", &tmp);

        const char *tx_bytes = strtok_r(NULL, " \t", &tmp);
        const char *tx_pkts = strtok_r(NULL, " \t", &tmp);
        const char *tx_errs = strtok_r(NULL, " \t", &tmp);
        const char *tx_drop = strtok_r(NULL, " \t", &tmp);
        const char *tx_fifo = strtok_r(NULL, " \t", &tmp);
        const char *tx_colls = strtok_r(NULL, " \t", &tmp);
        const char *tx_carrier = strtok_r(NULL, " \t", &tmp);
        const char *tx_compressed = strtok_r(NULL, " \t", &tmp);

        if (rx_bytes == NULL) continue;
        if (rx_pkts == NULL) continue;
        if (tx_bytes == NULL) continue;
        if (tx_pkts == NULL) continue;

        netdev->tx_bytes = strtoull(tx_bytes, NULL, 10);
        netdev->rx_bytes = strtoull(rx_bytes, NULL, 10);
        netdev->tx_pkts = strtoull(tx_pkts, NULL, 10);
        netdev->rx_pkts = strtoull(rx_pkts, NULL, 10);

        (void)rx_errs;
        (void)rx_drop;
        (void)rx_fifo;
        (void)rx_frame;
        (void)rx_compressed;
        (void)rx_multicast;

        (void)tx_errs;
        (void)tx_drop;
        (void)tx_fifo;
        (void)tx_colls;
        (void)tx_carrier;
        (void)tx_compressed;
    }
}

struct pm_hw_acc_load_netstats *pm_hw_acc_load_netstats_get_from_str(char *lines)
{
    struct pm_hw_acc_load_netstats *stats = CALLOC(1, sizeof(*stats));
    ds_tree_init(&stats->netdevs, ds_str_cmp, struct pm_hw_acc_load_netstats_netdev, node);
    pm_hw_acc_load_netstats_parse(stats, lines);
    return stats;
}

struct pm_hw_acc_load_netstats *pm_hw_acc_load_netstats_get(void)
{
    return pm_hw_acc_load_netstats_get_from_str(file_geta("/proc/net/dev"));
}

void pm_hw_acc_load_netstats_drop(struct pm_hw_acc_load_netstats *stats)
{
    if (stats == NULL) return;
    struct pm_hw_acc_load_netstats_netdev *netdev;
    while ((netdev = ds_tree_head(&stats->netdevs)) != NULL)
    {
        pm_hw_acc_load_netstats_netdev_drop(netdev);
    }
    FREE(stats);
}

void pm_hw_acc_load_netstats_compare(
        const struct pm_hw_acc_load_netstats *prev_stats,
        const struct pm_hw_acc_load_netstats *next_stats,
        uint64_t *max_tx_bytes,
        uint64_t *max_rx_bytes,
        uint64_t *max_tx_pkts,
        uint64_t *max_rx_pkts)
{
    struct pm_hw_acc_load_netstats_netdev *next_netdev;

    *max_tx_bytes = 0;
    *max_rx_bytes = 0;
    *max_tx_pkts = 0;
    *max_rx_pkts = 0;

    ds_tree_foreach (((ds_tree_t *)&next_stats->netdevs), next_netdev)
    {
        const struct pm_hw_acc_load_netstats_netdev *prev_netdev =
                ds_tree_find((ds_tree_t *)&prev_stats->netdevs, next_netdev->if_name);
        if (prev_netdev == NULL) continue;

        const uint64_t tx_bytes = next_netdev->tx_bytes - prev_netdev->tx_bytes;
        const uint64_t rx_bytes = next_netdev->rx_bytes - prev_netdev->rx_bytes;
        const uint64_t tx_pkts = next_netdev->tx_pkts - prev_netdev->tx_pkts;
        const uint64_t rx_pkts = next_netdev->rx_pkts - prev_netdev->rx_pkts;

        /* If an interface is re-created it can underflow
         * into a very large number. Ignore very large
         * deltas. It's unlikely the compared values will
         * ever be far apart enough to amount to such big
         * deltas.
         *
         * This is a result of not being able to tell
         * ifindex had changed when parsing /proc/net/dev.
         * ifindex would guarantee matching statistics, but
         * that would require changing how stats are
         * acquired.
         *
         * The /proc/net/dev seems good enough for the case
         * this is used in.
         */
        const bool underflow_maybe = (tx_bytes > (UINT64_MAX / 2)) || (rx_bytes > (UINT64_MAX / 2))
                                     || (tx_pkts > (UINT64_MAX / 2)) || (rx_pkts > (UINT64_MAX / 2));
        if (underflow_maybe) continue;

        if (tx_bytes > *max_tx_bytes) *max_tx_bytes = tx_bytes;
        if (rx_bytes > *max_rx_bytes) *max_rx_bytes = rx_bytes;
        if (tx_pkts > *max_tx_pkts) *max_tx_pkts = tx_pkts;
        if (rx_pkts > *max_rx_pkts) *max_rx_pkts = rx_pkts;
    }
}
