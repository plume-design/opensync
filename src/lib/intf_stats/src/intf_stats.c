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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <ev.h>

#include <sys/types.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <linux/if_link.h>
#include <errno.h>

#include "os_types.h"
#include "os.h"
#include "log.h"
#include "ds.h"
#include "network_metadata.h"
#include "fcm.h"

#include "ovsdb_sync.h"
#include "ovsdb_table.h"

#include "interface_stats.pb-c.h"
#include "intf_stats.h"
#include "util.h"

static  ds_dlist_t               cloud_intf_list;
static  intf_stats_report_data_t report;
static  int                      report_type;

static  ovsdb_update_monitor_t   intf_stats_inet_config_ovsdb_update;

/******************************************************************************
 *  Helper Functions
 ******************************************************************************/

static void
intf_stats_remove_all_intfs(ds_dlist_t *list)
{
    intf_stats_t    *intf = NULL;
    ds_dlist_iter_t  intf_iter;

    for ( intf = ds_dlist_ifirst(&intf_iter, list);
          intf != NULL;
          intf = ds_dlist_inext(&intf_iter))
    {
        ds_dlist_iremove(&intf_iter);
        intf_stats_intf_free(intf);
        intf = NULL;
    }

    return;
}

void
intf_stats_reset_report(intf_stats_report_data_t *report)
{
    intf_stats_list_t        *window_list = &report->window_list;
    intf_stats_window_list_t *window = NULL;
    intf_stats_window_t      *window_entry = NULL;

    ds_dlist_iter_t           win_iter;

    /* Go through each window in the report, and free the
     * allocated per-window intf_list
     */
    for ( window = ds_dlist_ifirst(&win_iter, window_list);
          window != NULL;
          window = ds_dlist_inext(&win_iter))
    {
        window_entry = &window->entry;
        intf_stats_remove_all_intfs(&window_entry->intf_list);
    }

    /* Free all the allocated windows in the report */
    for ( window = ds_dlist_ifirst(&win_iter, window_list);
          window != NULL;
          window = ds_dlist_inext(&win_iter))
    {
        ds_dlist_iremove(&win_iter);
        intf_stats_window_free(window);
        window = NULL;
    }

    /* node_info does not change. No need to reset it */
    report->reported_at = 0;
    report->num_windows = 0;

    return;
}

static void
intf_stats_dump_stats(ds_dlist_t *list)
{
    intf_stats_t    *intf = NULL;
    ds_dlist_iter_t  intf_iter;

    for ( intf = ds_dlist_ifirst(&intf_iter, list);
          intf != NULL;
          intf = ds_dlist_inext(&intf_iter))
    {
        LOGD("----------------------------------------------");
        LOGD("Intername : %-8s", intf->ifname);
        LOGD("Role      : %-8s", intf->role);
        LOGD("tx_bytes  : %10" PRIu64 "; rx_bytes  : %10" PRIu64 "", intf->tx_bytes, intf->rx_bytes);
        LOGD("tx_packets: %10" PRIu64 "; rx_packets: %10" PRIu64 "", intf->tx_packets, intf->rx_packets);
        LOGD("----------------------------------------------");
    }

    return;
}

void
intf_stats_dump_report(intf_stats_report_data_t *report)
{
    intf_stats_list_t        *window_list = &report->window_list;
    intf_stats_window_list_t *window = NULL;
    intf_stats_window_t      *window_entry = NULL;

    ds_dlist_iter_t           win_iter;

    for ( window = ds_dlist_ifirst(&win_iter, window_list);
          window != NULL;
          window = ds_dlist_inext(&win_iter))
    {
        window_entry = &window->entry;
        LOGD("---- Window Index (%zu) ----", window_entry->window_idx);
        LOGD("Started at : %10" PRIu64 "",   window_entry->started_at);
        LOGD("Ended   at : %10" PRIu64 "",   window_entry->ended_at);

        intf_stats_dump_stats(&window_entry->intf_list);
    }

    return;
}

static intf_stats_t *
intf_stats_find_by_ifname(ds_dlist_t *list, char *ifname)
{
    intf_stats_t    *intf = NULL;
    ds_dlist_iter_t  intf_iter;

    for ( intf = ds_dlist_ifirst(&intf_iter, list);
          intf != NULL;
          intf = ds_dlist_inext(&intf_iter))
    {
        if (!strcmp(intf->ifname, ifname)) return intf;
    }

    return NULL;
}

static int
intf_stats_get_num_intfs(void)
{
    intf_stats_t    *intf = NULL;
    ds_dlist_iter_t  intf_iter;
    int              count = 0;

    for ( intf = ds_dlist_ifirst(&intf_iter, &cloud_intf_list);
          intf != NULL;
          intf = ds_dlist_inext(&intf_iter))
    {
        count ++;
    }

    LOGT("Total number of interfaces: '%d'", count);
    return count;
}

intf_stats_window_t *
intf_stats_get_current_window(intf_stats_report_data_t *report)
{
    intf_stats_list_t        *window_list    = &report->window_list;
    intf_stats_window_list_t *window         = NULL;
    intf_stats_window_t      *window_entry   = NULL;

    size_t                    cur_window_idx = report->num_windows - 1;
    ds_dlist_iter_t           win_iter;

    for ( window = ds_dlist_ifirst(&win_iter, window_list);
          window != NULL;
          window = ds_dlist_inext(&win_iter))
    {
        window_entry = &window->entry;

        if (window_entry->window_idx == cur_window_idx) return window_entry;
    }

    return NULL;
}

/******************************************************************************/

/*
 * Parses the list of interfaces provided by the cloud via the
 * "other_config", and builds the tree
 */
static void
intf_stats_get_intf_names(fcm_collect_plugin_t *collector)
{
    intf_stats_t    *intf       = NULL;
    char            *interfaces = NULL;
    char            *list, *tok;

    interfaces = collector->get_other_config(collector, "intf_list");
    if (!interfaces)
    {
        LOGE("Interface List received from cloud is empty");
        return;
    }

    list = strdupa(interfaces);
    while ((tok = strsep(&list, ",")) != NULL)
    {
        intf = intf_stats_intf_alloc();
        if (!intf)
        {
            LOGE("Interface memory allocation failed");
            return;
        }

        STRSCPY(intf->ifname, tok);

        ds_dlist_insert_tail(&cloud_intf_list, intf);
        LOGI("Monitoring interface '%s'", intf->ifname);
    }

    return;
}

static void
intf_stats_get_node_info(fcm_collect_plugin_t *collector)
{
    node_info_t *node_info = &report.node_info;

    node_info->node_id     = collector->get_mqtt_hdr_node_id();
    node_info->location_id = collector->get_mqtt_hdr_loc_id();

    return;
}

static bool 
intf_stats_get_role(intf_stats_t *intf)
{
    struct schema_Wifi_Inet_Config  inet;
    json_t                          *jrow, *where;
    pjs_errmsg_t                    perr;

    where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Inet_Config, if_name), intf->ifname);
    jrow  = ovsdb_sync_select_where(SCHEMA_TABLE(Wifi_Inet_Config), where);

    if (!schema_Wifi_Inet_Config_from_json(&inet,
                                           json_array_get(jrow, 0),
                                           false,
                                           perr))
    {
        LOGE("Unable to parse Wifi_Inet_Config column: '%s'", perr);
        json_decref(jrow);
        return false;
    }

    if ((!inet.role_exists) || (strlen(inet.role) == 0))
    {
            LOGN("Role not set for interface '%s', setting 'default'", intf->ifname);
            STRSCPY(intf->role, "default");
    }
    else
    {
            LOGI("Interface '%s' has role: '%s'", intf->ifname, inet.role);
            STRSCPY(intf->role, inet.role);
    }


    json_decref(jrow);
    return true;
}

static void
intf_stats_get_intf_roles(void)
{
    intf_stats_t        *intf = NULL;
    ds_dlist_iter_t     intf_iter;

    for ( intf = ds_dlist_ifirst(&intf_iter, &cloud_intf_list);
          intf != NULL;
          intf = ds_dlist_inext(&intf_iter))
    {
        if (!intf_stats_get_role(intf))
        {
            LOGN("Couldn't get role for interface '%s', setting 'default'", intf->ifname);
            STRSCPY(intf->role, "default");
        }
    }

    return;
}

static void
intf_stats_inet_config_ovsdb_update_cb(ovsdb_update_monitor_t *self)
{
    struct schema_Wifi_Inet_Config  inet;
    pjs_errmsg_t                    perr;

    intf_stats_t                    *intf = NULL;

    switch (self->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
        {
            if (!schema_Wifi_Inet_Config_from_json(&inet, self->mon_json_new, false, perr))
            {
                LOGE("Failed to parse new Wifi_Inet_Config row: %s", perr);
                break;
            }

            if (!inet.if_name_exists)  break;

            intf = intf_stats_find_by_ifname(&cloud_intf_list, inet.if_name);
            if (!intf)
            {
                /* This interface is not being tracked for stats */
                break;
            }

            if ((!inet.role_exists) || (strlen(inet.role) == 0))    STRSCPY(intf->role, "default");
            else                                                    STRSCPY(intf->role, inet.role);

            break;
        }

        case OVSDB_UPDATE_DEL:
        default:
            break;
    }

    return;
}

/******************************************************************************/

static void
intf_stats_calculate_stats(intf_stats_t *stats_old, struct rtnl_link_stats *stats_new)
{
    intf_stats_window_t *window_entry     = NULL;
    intf_stats_t        *intf_entry       = NULL;
    ds_dlist_t          *window_intf_list = NULL;

    window_entry = intf_stats_get_current_window(&report);
    if (!window_entry)
    {
        LOGE("%s: Unable to get current active window", __func__);
        return;
    }

    window_intf_list = &window_entry->intf_list;

    intf_entry = intf_stats_find_by_ifname(window_intf_list, stats_old->ifname);
    if (!intf_entry)
    {
        // Create an entry for this interface in the window
        intf_entry = intf_stats_intf_alloc();
        if (!intf_entry)
        {
            LOGE("Unable to allocate interface entry");
            return;
        }

        STRSCPY(intf_entry->ifname, stats_old->ifname);
        STRSCPY(intf_entry->role, stats_old->role);

        ds_dlist_insert_tail(window_intf_list, intf_entry);
        LOGD("Adding interface '%s' into window", intf_entry->ifname);
    }

    if (report_type == FCM_RPT_FMT_DELTA)
    {
        // Calculate the stat deltas
        intf_entry->tx_bytes   = ((stats_new->tx_bytes)   - (stats_old->tx_bytes));
        intf_entry->rx_bytes   = ((stats_new->rx_bytes)   - (stats_old->rx_bytes));
        intf_entry->tx_packets = ((stats_new->tx_packets) - (stats_old->tx_packets));
        intf_entry->rx_packets = ((stats_new->rx_packets) - (stats_old->rx_packets));
    }
    else if (report_type == FCM_RPT_FMT_CUMUL)
    {
        intf_entry->tx_bytes   = stats_new->tx_bytes;
        intf_entry->rx_bytes   = stats_new->rx_bytes;
        intf_entry->tx_packets = stats_new->tx_packets;
        intf_entry->rx_packets = stats_new->rx_packets;
    }

    return;
}

static void
intf_stats_fetch_stats(bool set_baseline)
{
    intf_stats_t     *stats_old = NULL;
    struct  ifaddrs  *ifaddr, *ifa;
    int               family, s;
    char              host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1 )
    {
        LOGE("getifaddrs failed, errno = '%d'", errno);
        return;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL) continue;

        stats_old = intf_stats_find_by_ifname(&cloud_intf_list, ifa->ifa_name);
        if (!stats_old)
        {
            /* This interface is not being tracked for stats */
            continue;
        }

        family = ifa->ifa_addr->sa_family;
        LOGT("%-8s %s (%d)", ifa->ifa_name,
                             (family == AF_PACKET) ? "AF_PACKET" :
                             (family == AF_INET)   ? "AF_INET"   :
                             (family == AF_INET6)  ? "AF_INET6"  : "??",
                             family);

        if (family == AF_INET || family == AF_INET6)
        {
            s = getnameinfo(ifa->ifa_addr,
                            (family == AF_INET) ? sizeof(struct sockaddr_in) :
                                                  sizeof(struct sockaddr_in6),
                            host, NI_MAXHOST,
                            NULL, 0, NI_NUMERICHOST);

            if (s != 0)
            {
                LOGE("getnameinfo() failed: %s", gai_strerror(s));
                goto exit;
            }

            LOGT("address: <%s>", host);
        } 
        else if (family == AF_PACKET && ifa->ifa_data != NULL)
        {
            struct rtnl_link_stats *stats_new = ifa->ifa_data;

            LOGT("------Stats retreived from getifaddrs()-------");
            LOGT("tx_packets = %10u; rx_packets = %10u\n",
                                        stats_new->tx_packets, stats_new->rx_packets);
            LOGT("tx_bytes   = %10u; rx_bytes   = %10u\n",
                                        stats_new->tx_bytes, stats_new->rx_bytes);
            LOGT("----------------------------------------------");

            /* Calculate the deltas */
            if (!set_baseline)
            {
                intf_stats_calculate_stats(stats_old, stats_new);
            }

            /* Replace the old stats */
            stats_old->tx_bytes   = stats_new->tx_bytes;
            stats_old->rx_bytes   = stats_new->rx_bytes;
            stats_old->tx_packets = stats_new->tx_packets;
            stats_old->rx_packets = stats_new->rx_packets;
        }
    }

exit:
    freeifaddrs(ifaddr);

    return;
}

void
intf_stats_activate_window(intf_stats_report_data_t *report)
{
    intf_stats_list_t        *window_list  = &report->window_list;
    intf_stats_window_list_t *window       = NULL;
    intf_stats_window_t      *window_entry = NULL;

    intf_stats_list_t        *intf_list    = NULL;

    window = intf_stats_window_alloc();
    if (!window)
    {
        LOGE("Interface stats window allocation failed");
        return;
    }

    window_entry = &window->entry;

    window_entry->window_idx = report->num_windows;
    window_entry->started_at = time(NULL);
    window_entry->num_intfs  = intf_stats_get_num_intfs();

    // Initialize the interface list
    intf_list = &window_entry->intf_list;
    ds_dlist_init(intf_list, intf_stats_t, node);

    LOGD("Adding a new observation window, idx: (%zu)", window_entry->window_idx);
    ds_dlist_insert_tail(window_list, window);

    report->num_windows++;

    return;
}

void
intf_stats_close_window(intf_stats_report_data_t *report)
{
    intf_stats_window_t     *window_entry = NULL;

    window_entry = intf_stats_get_current_window(report);
    if (!window_entry)
    {
        LOGE("%s: Unable to get current active window", __func__);
        return;
    }

    window_entry->ended_at = time(NULL);
    LOGD("Closing window index:(%zu)", window_entry->window_idx);

    return;
}

static void
intf_stats_send_report_cb(fcm_collect_plugin_t *collector)
{
    intf_stats_close_window(&report);

    intf_stats_fetch_stats(false);
    intf_stats_dump_report(&report);

    LOGD("Sending Interface Stats Report");
    intf_stats_send_report(&report, collector->mqtt_topic);

    intf_stats_reset_report(&report);

    intf_stats_activate_window(&report);

    return;
}

void
intf_stats_plugin_close_cb(fcm_collect_plugin_t *collector)
{
    LOGN("Interface Stats plugin shutting down");
    intf_stats_remove_all_intfs(&cloud_intf_list);
    intf_stats_reset_report(&report);

    return;
}

int intf_stats_plugin_init(fcm_collect_plugin_t *collector)
{
    LOGN("Interface Stats plugin intialization");

    if (!collector)
    {
        LOGE("FCM collector instance is NULL");
        return 0;
    }

    if (!ovsdb_update_monitor(&intf_stats_inet_config_ovsdb_update,
                              intf_stats_inet_config_ovsdb_update_cb,
                              SCHEMA_TABLE(Wifi_Inet_Config),
                              OMT_ALL))
    {
        LOGE("Failed to monitor OVSDB table %s", SCHEMA_TABLE(Wifi_Inet_Config));
        return 0;
    }

    collector->collect_periodic  = NULL;
    collector->send_report       = intf_stats_send_report_cb;
    collector->close_plugin      = intf_stats_plugin_close_cb;

    /* Initialize the list to hold the interfaces provided by the cloud */
    ds_dlist_init(&cloud_intf_list, intf_stats_t, node);
    intf_stats_get_intf_names(collector);

    /* Get the interface roles from Wifi_Inet_Config */
    intf_stats_get_intf_roles();

    /* Initialize the report list */
    memset(&report, 0, sizeof(report));
    ds_dlist_init(&report.window_list, intf_stats_window_list_t, node);
    report.num_windows = 0;

    intf_stats_get_node_info(collector);

    /* Get the report type */
    report_type = collector->fmt;

    /* Set the baseline stats for delta computation */
    intf_stats_fetch_stats(true);

    /* Activate the current observation window */
    intf_stats_activate_window(&report);

    return 0;
}
