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

#include <inttypes.h>

#include "const.h"
#include "log.h"
#include "osp_otbr.h"
#include "otbr_cli.h"
#include "otbr_cli_api.h"
#include "util.h"

/** Parent event loop */
static struct ev_loop *g_loop;
/** Timers used for updating all Thread related information periodically */
static struct
{
    struct ev_timer topology;  /**< Timer used for updating Thread topology information */
    struct ev_timer discovery; /**< Timer used for discoveries of Thread networks */
} g_timers;
/** Signal watchers used to stop the daemon child process, close D-Bus connections, and cleanup resources */
static ev_signal g_signal_watchers[] = {{.signum = SIGTERM}, {.signum = SIGINT}};
/** Callback used to notify about Thread dataset changes */
static osp_otbr_on_dataset_change_cb_t *g_on_dataset_change_cb;
/** Callback used to report Thread network topology */
static osp_otbr_on_network_topology_cb_t *g_on_network_topology_cb;
/** Callback used to report Thread network scan or discovery results */
static osp_otbr_on_network_scan_result_cb_t *g_on_network_scan_result_cb;

C_STATIC_ASSERT(
        ((int)OSP_OTBR_DEVICE_ROLE_DISABLED == (int)OT_DEVICE_ROLE_DISABLED)
                && ((int)OSP_OTBR_DEVICE_ROLE_LEADER == (int)OT_DEVICE_ROLE_LEADER),
        "osp_otbr_device_role_e incompatible with otDeviceRole");

/**
 * Count the number of occurrences of a character in a string
 *
 * @param[in] char_to_count  Character of which to count the occurrences.
 * @param[in] in_string      String to search in.
 *
 * @return number of occurrences of `char_to_count` in `in_string`.
 */
size_t strcount(const char char_to_count, const char *in_string)
{
    size_t count;

    for (count = 0; *in_string != '\0'; in_string++)
    {
        if (*in_string == char_to_count)
        {
            count++;
        }
    }

    return count;
}

/**
 * Start the timer with the given timeout and repeat interval
 *
 * @param[in] timer   Timer to start.
 * @param[in] after   Timeout time in seconds.
 *                    If positive, timer will be started using @ref ev_timer_start().
 * @param[in] repeat  Repeat time in seconds, or -1 to keep the current timer value.
 *                    If `after` is 0 and timer has the repeat value set, it will be
 *                    started using @ref ev_timer_again().
 */
static void timer_start(struct ev_timer *const timer, const float after, const float repeat)
{
    if (ev_is_active(timer))
    {
        ev_timer_stop(g_loop, timer);
    }

    ev_timer_set(timer, after, repeat < 0 ? timer->repeat : repeat);

    if (after > 0)
    {
        ev_timer_start(g_loop, timer);
    }
    else if (timer->repeat > 0)
    {
        ev_timer_again(g_loop, timer);
    }
}

/**
 * Stop the timer, if currently running
 *
 * @param[in] timer   Timer to stop.
 */
static void timer_stop(struct ev_timer *const timer)
{
    if (ev_is_active(timer))
    {
        ev_timer_stop(g_loop, timer);
    }
}

/** libev callback invoked when a signal is received */
static void on_signal(struct ev_loop *loop, ev_signal *w, int r_events)
{
    (void)r_events;

    LOGI("OSP OTBR caught signal %d, closing", w->signum);

    /* Note: Signal watchers are started from osp_otbr_init(), so the daemon might not be initialized yet.
     * Breaking the loop shall cause osp_otbr_close() to be called from the parent module. */
    ev_break(loop, EVBREAK_ALL);
}

/** Callback invoked in case of any unrecoverable error in the otbr_cli library */
static void on_otbr_cli_failure_cb(const enum otbr_cli_failure_reason_e reason)
{
    LOGE("OTBR CLI failure (%d)", reason);

    ev_break(g_loop, EVBREAK_ALL);
}

/** Report the dataset change to the OSP API client */
static bool report_dataset_change(void)
{
    static struct osp_otbr_dataset_tlvs_s s_dataset = {
        .tlvs = {[0] = 0xFF}, /*< Trigger initial update */
        .len = 0xFF};
    struct osp_otbr_dataset_tlvs_s dataset = {0};
    ssize_t dataset_len;

    if (g_on_dataset_change_cb == NULL)
    {
        return true;
    }

    dataset_len = otbr_cli_get_array("dataset active -x", dataset.tlvs, sizeof(dataset.tlvs));
    if ((dataset_len < 0) || (dataset_len > (ssize_t)sizeof(dataset.tlvs)))
    {
        /* This shall never happen if the CLI is running */
        LOGE("Invalid dataset length (%zd)", dataset_len);
        return false;
    }
    dataset.len = (uint8_t)dataset_len;

    if (memcmp(&dataset, &s_dataset, sizeof(dataset)) != 0)
    {
        LOGD("Dataset changed (%d to %d B), reporting", s_dataset.len, dataset.len);
        memcpy(&s_dataset, &dataset, sizeof(s_dataset));

        g_on_dataset_change_cb(&dataset);
    }

    return true;
}

static enum osp_otbr_device_role_e get_role(const uint16_t rloc16, const uint8_t leader_id)
{
    /* Use RLOC16 -> Router[Child=0] ID -> RLOC16 conversion to check if this device is
     * a router (if child, cast router_id will have invalid value, which also works). */
    const uint8_t router_id = (uint8_t)osp_otbr_device_id(rloc16);

    if (rloc16 == osp_otbr_rloc16(router_id, 0))
    {
        if (router_id == leader_id)
        {
            return OSP_OTBR_DEVICE_ROLE_LEADER;
        }
        else
        {
            return OSP_OTBR_DEVICE_ROLE_ROUTER;
        }
    }
    else
    {
        return OSP_OTBR_DEVICE_ROLE_CHILD;
    }
    /* Other roles (DETACHED, DISABLED) are not possible for devices connected
     * to a Thread network, that is devices having RLOC16 address. */
}

static void parse_mode(struct osp_otbr_device_s *const device, const otLinkModeConfig *const mode)
{
    if (device->role > OSP_OTBR_DEVICE_ROLE_CHILD)
    {
        if (!(mode->mRxOnWhenIdle && mode->mDeviceType && mode->mNetworkData))
        {
            LOGW("%s with mode r=%d, d=%d, n=%d",
                 (device->role == OSP_OTBR_DEVICE_ROLE_LEADER) ? "Leader" : "Router",
                 mode->mRxOnWhenIdle,
                 mode->mDeviceType,
                 mode->mNetworkData);
        }
    }
    else
    {
        device->child.mode.rx_on_when_idle = mode->mRxOnWhenIdle;
        device->child.mode.full_thread_device = mode->mDeviceType;
        device->child.mode.full_network_data = mode->mNetworkData;
    }
}

static void parse_route(struct osp_otbr_device_s *const device, const otNetworkDiagRouteData *const route)
{
    struct osp_otbr_link_s link = {
        .rloc16 = osp_otbr_rloc16(route->mRouterId, 0),
        .lq_in = route->mLinkQualityIn,
        .lq_out = route->mLinkQualityOut};

    if (device->role >= OSP_OTBR_DEVICE_ROLE_ROUTER)
    {
        ARRAY_APPEND_COPY(device->router.neighbors.links, device->router.neighbors.count, link);
    }
    else if (device->role == OSP_OTBR_DEVICE_ROLE_CHILD)
    {
        memcpy(&device->child.parent, &link, sizeof(device->child.parent));
    }
}

static bool NONNULL(1, 3) parse_network_diag_peer_tlvs(
        const struct otbr_network_diagnostic_peer_tlvs_s *const tlvs,
        const otLeaderData *const leader,
        struct osp_otbr_device_s *const device)
{
    LOGT("Parsing %d TLVs", tlvs->num_tlvs);

    for (size_t i_tlv = 0; i_tlv < tlvs->num_tlvs; i_tlv++)
    {
        const struct otNetworkDiagTlv *const tlv = &tlvs->tlvs[i_tlv];

        switch (tlv->mType)
        {
            case OT_NETWORK_DIAGNOSTIC_TLV_EXT_ADDRESS: {
                const uint8_t *const ea = tlv->mData.mExtAddress.m8;

                device->ext_addr = 0;
                for (size_t i = 0; i < sizeof(tlv->mData.mExtAddress.m8); i++)
                {
                    device->ext_addr <<= 8;
                    device->ext_addr |= ea[i];
                }

                LOGT("TLV Ext Address: %016llx", device->ext_addr);
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_SHORT_ADDRESS: {
                const uint16_t addr16 = tlv->mData.mAddr16;

                device->rloc16 = addr16;
                device->role = get_role(addr16, (leader != NULL) ? leader->mLeaderRouterId : 0xFF);

                LOGT("TLV Short Address: %04x (id=%d, role=%d)", addr16, osp_otbr_device_id(addr16), device->role);
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_MODE: {
                const otLinkModeConfig *const mode = &tlv->mData.mMode;

                parse_mode(device, mode);

                LOGT("TLV Mode (role=%d): 0x%02x (r=%d, d=%d, n=%d)",
                     device->role,
                     *((uint8_t *)mode),
                     mode->mRxOnWhenIdle,
                     mode->mDeviceType,
                     mode->mNetworkData);
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_TIMEOUT: {
                const uint32_t timeout = tlv->mData.mTimeout;
                LOGT("Ignoring TLV Timeout: %d s", timeout);
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_CONNECTIVITY: {
                const otNetworkDiagConnectivity *const c = &tlv->mData.mConnectivity;
                LOGT("Ignoring TLV Connectivity: ParentPriority=%d, LeaderCost=%d, ActiveRouters=%d, LQ3/2/1=%d/%d/%d",
                     c->mParentPriority,
                     c->mLeaderCost,
                     c->mActiveRouters,
                     c->mLinkQuality3,
                     c->mLinkQuality2,
                     c->mLinkQuality1);
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_ROUTE: {
                if (device->role < OSP_OTBR_DEVICE_ROLE_CHILD)
                {
                    LOGW("Routes (%d) for device role %d", tlv->mData.mRoute.mRouteCount, device->role);
                    break;
                }
                else if ((device->role == OSP_OTBR_DEVICE_ROLE_CHILD) && (tlv->mData.mRoute.mRouteCount > 1))
                {
                    LOGW("Child with multiple routes");
                }

                for (size_t i_route = 0; i_route < tlv->mData.mRoute.mRouteCount; i_route++)
                {
                    const otNetworkDiagRouteData *const rd = &tlv->mData.mRoute.mRouteData[i_route];

                    parse_route(device, rd);

                    LOGT("TLV Route: %d (RouterId=%d, RouteCost=%d, LQ In/Out=%d/%d)",
                         i_route,
                         rd->mRouterId,
                         rd->mRouteCost,
                         rd->mLinkQualityIn,
                         rd->mLinkQualityOut);
                }
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_LEADER_DATA: {
                const otLeaderData *const ld = &tlv->mData.mLeaderData;
                LOGT("Ignoring TLV Leader Data: PartitionId=%d, Weighting=%d, DataVersion/Stable=%d/%d, "
                     "LeaderRouterId=%d",
                     ld->mPartitionId,
                     ld->mWeighting,
                     ld->mDataVersion,
                     ld->mStableDataVersion,
                     ld->mLeaderRouterId);
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_NETWORK_DATA: {
                const uint8_t *const data = tlv->mData.mNetworkData.m8;
                const uint8_t count = tlv->mData.mNetworkData.mCount;
                char str[sizeof(tlv->mData.mNetworkData.m8) * 2 + 1];

                str[0] = '\0';
                bin2hex(data, count, str, sizeof(str));

                LOGT("Ignoring TLV Network Data: %d B %s", count, str);
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_IP6_ADDR_LIST: {
                for (size_t i_addr = 0; i_addr < tlv->mData.mIp6AddrList.mCount; i_addr++)
                {
                    const otIp6Address *const addr = &tlv->mData.mIp6AddrList.mList[i_addr];
                    osn_ip6_addr_t ip_addr = OSN_IP6_ADDR_INIT;
                    C_STATIC_ASSERT(sizeof(ip_addr.ia6_addr) == sizeof(addr->mFields), "IPv6 address struct mismatch");

                    memcpy(&ip_addr.ia6_addr, addr->mFields.m8, sizeof(ip_addr.ia6_addr));
                    ARRAY_APPEND_COPY(device->ip_addresses.addr, device->ip_addresses.count, ip_addr);

                    LOGT("TLV IPv6 Addr List %d: " PRI_osn_ip6_addr, i_addr, FMT_osn_ip6_addr(ip_addr));
                }
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_MAC_COUNTERS: {
                const otNetworkDiagMacCounters *const mc = &tlv->mData.mMacCounters;
                LOGT("Ignoring TLV MAC Counters (In/Out Errors=%d/%d Discards=%d/%d"
                     " UcastPkts=%d/%d BroadcastPkts=%d/%d, UnknownProtos=%d/-)",
                     ntohl(mc->mIfInErrors),
                     ntohl(mc->mIfOutErrors),
                     ntohl(mc->mIfInDiscards),
                     ntohl(mc->mIfOutDiscards),
                     ntohl(mc->mIfInUcastPkts),
                     ntohl(mc->mIfOutUcastPkts),
                     ntohl(mc->mIfInBroadcastPkts),
                     ntohl(mc->mIfOutBroadcastPkts),
                     ntohl(mc->mIfInUnknownProtos));
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_BATTERY_LEVEL: {
                LOGT("Ignoring TLV Battery Level (BatteryLevel=%d)", tlv->mData.mBatteryLevel);
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_SUPPLY_VOLTAGE: {
                LOGT("Ignoring TLV Supply Voltage (SupplyVoltage=%d)", tlv->mData.mSupplyVoltage);
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_CHILD_TABLE: {
                LOGT("Ignoring TLV Child Table (Count=%d)", tlv->mData.mChildTable.mCount);
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_CHANNEL_PAGES: {
                LOGT("Ignoring TLV Channel Pages (Count=%d)", tlv->mData.mChannelPages.mCount);
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_MAX_CHILD_TIMEOUT: {
                LOGT("Ignoring TLV Max Child Timeout (MaxChildTimeout=%d)", tlv->mData.mMaxChildTimeout);
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_VERSION: {
                device->version = tlv->mData.mVersion;
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_VENDOR_NAME: {
                strscpy(device->vendor_name, tlv->mData.mVendorName, sizeof(device->vendor_name));
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_VENDOR_MODEL: {
                strscpy(device->vendor_model, tlv->mData.mVendorModel, sizeof(device->vendor_model));
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_VENDOR_SW_VERSION: {
                strscpy(device->vendor_sw_version, tlv->mData.mVendorSwVersion, sizeof(device->vendor_sw_version));
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_THREAD_STACK_VERSION: {
                strscpy(device->thread_stack_version,
                        tlv->mData.mThreadStackVersion,
                        sizeof(device->thread_stack_version));
                break;
            }
            case OT_NETWORK_DIAGNOSTIC_TLV_MLE_COUNTERS: {
                LOGT("Ignoring TLV MLE Counters (TrackedTime=%llu)", tlv->mData.mMleCounters.mTrackedTime);
                break;
            }

            case OT_NETWORK_DIAGNOSTIC_TLV_TYPE_LIST:           /* passthrough */
            case OT_NETWORK_DIAGNOSTIC_TLV_CHILD:               /* passthrough */
            case OT_NETWORK_DIAGNOSTIC_TLV_CHILD_IP6_ADDR_LIST: /* passthrough */
            case OT_NETWORK_DIAGNOSTIC_TLV_ROUTER_NEIGHBOR:
                LOGT("Parsing TLV type %d is not supported", tlv->mType);
                break;

            case OT_NETWORK_DIAGNOSTIC_TLV_ANSWER:   /* passthrough */
            case OT_NETWORK_DIAGNOSTIC_TLV_QUERY_ID: /* ignored */
                break;

            default:
                LOGE("Unknown TLV type %d", tlv->mType);
                return false;
        }
    }
    return true;
}

static bool NONNULL(1, 3) parse_network_diag_tlvs(
        const struct otbr_network_diagnostic_tlvs_s *const tlvs,
        const otLeaderData *const leader,
        struct osp_otbr_devices_s *const devices)
{
    for (size_t i_peer = 0; i_peer < tlvs->num_peers; i_peer++)
    {
        struct osp_otbr_device_s device = OSP_OTBR_DEVICE_INIT;

        if (!parse_network_diag_peer_tlvs(&tlvs->peers[i_peer], leader, &device))
        {
            return false;
        }

        ARRAY_APPEND_COPY(devices->devices, devices->count, device);
    }
    return true;
}

static bool NONNULL(1) ot_get_meshdiag_topology(struct osp_otbr_devices_s *const devices, const float timeout)
{
    static const otNetworkDiagTlvType diag_types[] = {
        OT_NETWORK_DIAGNOSTIC_TLV_EXT_ADDRESS,
        OT_NETWORK_DIAGNOSTIC_TLV_SHORT_ADDRESS,
        OT_NETWORK_DIAGNOSTIC_TLV_MODE,
        /* OT_NETWORK_DIAGNOSTIC_TLV_TIMEOUT, */
        /* OT_NETWORK_DIAGNOSTIC_TLV_CONNECTIVITY, */
        OT_NETWORK_DIAGNOSTIC_TLV_ROUTE,
        /* OT_NETWORK_DIAGNOSTIC_TLV_LEADER_DATA, */
        /* OT_NETWORK_DIAGNOSTIC_TLV_NETWORK_DATA, */
        OT_NETWORK_DIAGNOSTIC_TLV_IP6_ADDR_LIST,
        /* OT_NETWORK_DIAGNOSTIC_TLV_MAC_COUNTERS, */
        /* OT_NETWORK_DIAGNOSTIC_TLV_BATTERY_LEVEL, */
        /* OT_NETWORK_DIAGNOSTIC_TLV_SUPPLY_VOLTAGE, */
        OT_NETWORK_DIAGNOSTIC_TLV_CHILD_TABLE,
        /* OT_NETWORK_DIAGNOSTIC_TLV_CHANNEL_PAGES, */
        /* OT_NETWORK_DIAGNOSTIC_TLV_TYPE_LIST, */
        /* OT_NETWORK_DIAGNOSTIC_TLV_MAX_CHILD_TIMEOUT, */
        OT_NETWORK_DIAGNOSTIC_TLV_VERSION,
        OT_NETWORK_DIAGNOSTIC_TLV_VENDOR_NAME,
        OT_NETWORK_DIAGNOSTIC_TLV_VENDOR_MODEL,
        OT_NETWORK_DIAGNOSTIC_TLV_VENDOR_SW_VERSION,
        OT_NETWORK_DIAGNOSTIC_TLV_THREAD_STACK_VERSION,
        /* OT_NETWORK_DIAGNOSTIC_TLV_CHILD, */
        /* OT_NETWORK_DIAGNOSTIC_TLV_CHILD_IP6_ADDR_LIST, */
        /* OT_NETWORK_DIAGNOSTIC_TLV_ROUTER_NEIGHBOR, */
        /* OT_NETWORK_DIAGNOSTIC_TLV_ANSWER, */
        /* OT_NETWORK_DIAGNOSTIC_TLV_QUERY_ID, */
        /* OT_NETWORK_DIAGNOSTIC_TLV_MLE_COUNTERS, */
    };
    struct otbr_network_diagnostic_tlvs_s tlvs = {0};
    otLeaderData leader;
    bool success = false;

    /* To find out which router has the Leader role, OT_NETWORK_DIAGNOSTIC_TLV_CONNECTIVITY
     * could be retrieved and then otNetworkDiagConnectivity::mLeaderCost examined.
     * Every device connected to a thread network always has a route to the leader
     * of the network, and this route's quality is represented by LeaderCost.
     * The cost is measured on a scale from 1 to 255, with 1 being the highest
     * quality and 255 being the least, except when the device itself is the
     * leader of the network, in which case the LeaderCost will be 0.
     *
     * However, instead of retrieving the whole connectivity TLV from each device
     * just to get this information, utilize the otThreadGetLeaderData() API call
     * to retrieve the leader data directly once, and then just check which router
     * has the same ID as the leader.
     */
    if (otbr_cli_get_leader_data(&leader)
        && otbr_cli_get_network_diagnostic(
                otbr_cli_get_multicast_address(true, true, false),
                diag_types,
                ARRAY_SIZE(diag_types),
                timeout,
                &tlvs))
    {
        success = parse_network_diag_tlvs(&tlvs, &leader, devices);
    }
    otbr_cli_get_network_diagnostic_tlvs_free(&tlvs);

    return success;
}

static bool report_network_topology(void)
{
    struct osp_otbr_devices_s devices = {0};
    enum osp_otbr_device_role_e role;

    if (g_on_network_topology_cb == NULL)
    {
        return true;
    }

    if (!otbr_cli_get_role((otDeviceRole *)&role))
    {
        return false;
    }

    if (role > OSP_OTBR_DEVICE_ROLE_DETACHED)
    {
        ot_get_meshdiag_topology(&devices, 10);
    }

    g_on_network_topology_cb(role, &devices);

    /* Free all allocated memory */
    for (size_t i = 0; i < devices.count; i++)
    {
        struct osp_otbr_device_s *const dev = &devices.devices[i];

        ARRAY_FREE(dev->ip_addresses.addr, dev->ip_addresses.count);
        if (dev->role >= OSP_OTBR_DEVICE_ROLE_ROUTER)
        {
            ARRAY_FREE(dev->router.neighbors.links, dev->router.neighbors.count);
        }
    }
    ARRAY_FREE(devices.devices, devices.count);

    return true;
}

static bool report_network_discovery(void)
{
    struct osp_otbr_scan_results_s result = {0};
    otbr_cli_response_t rsp = {0};
    bool success = true;

    if (g_on_network_scan_result_cb == NULL)
    {
        return true;
    }

    if (!otbr_cli_get("discover", &rsp, 2, 30))
    {
        otbr_cli_response_free(&rsp);
        return false;
    }

    for (size_t i_line = 2; i_line < rsp.count; i_line++)
    {
        /* > discover
         * | Network Name     | Extended PAN     | PAN  | MAC Address      | Ch | dBm | LQI |
         * +------------------+------------------+------+------------------+----+-----+-----+
         * ...
         * > scan
         * | PAN  | MAC Address      | Ch | dBm | LQI |
         * +------+------------------+----+-----+-----+
         * ...
         */
        struct osp_otbr_scan_result_s network = {0};
        const char *const line = rsp.lines[i_line];
        const size_t sep_count = strcount('|', line);
        const size_t line_len = strlen(line);
        int parsed = 0;

        if ((sep_count == 8) && (line_len == 82))
        {
            /* discover
             * | %-16s | %16llx | %04x | %16llx | %2u | %3d | %3u |
             */
            network.discover = true;
            parsed =
                    sscanf(line,
                           "| %16s | %16" PRIx64 " | %04hx | %16" PRIx64 " | %2hhu | %3hhd | %3hhu |",
                           network.name,
                           &network.ext_pan_id,
                           &network.pan_id,
                           &network.ext_addr,
                           &network.channel,
                           &network.rssi,
                           &network.lqi);
        }
        else if ((sep_count == 6) && (line_len == 44))
        {
            /* scan
             * | %04x | %16llx | %2u | %3d | %3u |
             */
            parsed =
                    sscanf(line,
                           "| %04hx | %16" PRIx64 " | %2hhu | %3hhd | %3hhu |",
                           &network.pan_id,
                           &network.ext_addr,
                           &network.channel,
                           &network.rssi,
                           &network.lqi);
        }

        if (parsed != (int)(sep_count - 1))
        {
            LOGE("Invalid discover/scan line (%d, %d, %d): '%s'", sep_count, line_len, parsed, line);
            success = false;
            break;
        }

        ARRAY_APPEND_COPY(result.networks, result.count, network);
    }
    otbr_cli_response_free(&rsp);

    g_on_network_scan_result_cb(&result);

    ARRAY_FREE(result.networks, result.count);

    return success;
}

static bool stats_pre_check(const char *const op_name, ev_timer *const timer, const float postpone_interval)
{
    if (!otbr_cli_is_running())
    {
        LOGD("%s: OTBR not running", op_name);
        return false;
    }
    if (otbr_cli_is_busy())
    {
        LOGD("%s: Busy, retry after %.f sec", op_name, postpone_interval);
        timer_start(timer, postpone_interval, -1);
        return false;
    }
    return true;
}

static void topology_timer_callback(struct ev_loop *loop, ev_timer *timer, int r_events)
{
    (void)r_events;

    LOGT("Reporting Thread Network status (every %.0f s)", timer->repeat);

    if (!stats_pre_check("Topology", timer, 5))
    {
        return;
    }

    if (!(report_dataset_change() && report_network_topology()))
    {
        ev_break(loop, EVBREAK_ONE);
    }
}

static void discovery_timer_callback(struct ev_loop *loop, ev_timer *timer, int r_events)
{
    (void)r_events;

    LOGT("Performing Thread Network Discovery (every %.0f s)", timer->repeat);

    if (!stats_pre_check("Discovery", timer, 5))
    {
        return;
    }

    if (!report_network_discovery())
    {
        ev_break(loop, EVBREAK_ONE);
    }
}

bool osp_otbr_init(
        struct ev_loop *loop,
        osp_otbr_on_dataset_change_cb_t *on_dataset_change_cb,
        osp_otbr_on_network_topology_cb_t *on_network_topology_cb,
        osp_otbr_on_network_scan_result_cb_t *on_network_scan_result_cb)
{
    g_loop = loop;

    if (!otbr_cli_init(on_otbr_cli_failure_cb))
    {
        g_loop = NULL;
        return false;
    }

    /* Timer is initially paused and only started from osp_otbr_set_report_interval()  x */
    ev_timer_init(&g_timers.topology, topology_timer_callback, 0, 0);
    ev_timer_init(&g_timers.discovery, discovery_timer_callback, 0, 0);

    for (size_t i = 0; i < ARRAY_SIZE(g_signal_watchers); i++)
    {
        ev_signal *const sig = &g_signal_watchers[i];

        ev_signal_init(sig, on_signal, sig->signum);
        ev_signal_start(loop, sig);
    }

    g_on_dataset_change_cb = on_dataset_change_cb;
    g_on_network_topology_cb = on_network_topology_cb;
    g_on_network_scan_result_cb = on_network_scan_result_cb;

    LOGI("OSP OTBR initialized");
    return true;
}

bool osp_otbr_start(const char *thread_iface, const char *network_iface, uint64_t *eui64, uint64_t *ext_addr)
{
    uint32_t thread_version;
    uint32_t api_version;
    char rcp_version[129];

    ASSERT((thread_iface != NULL) && (thread_iface[0] != '\0'), "thread_interface is required");
    ASSERT((strlen(thread_iface) <= 16), "thread_interface is too long");
    ASSERT(g_loop != NULL, "OSP OTBR not initialized");

    if (!otbr_cli_start(g_loop, thread_iface, network_iface))
    {
        return false;
    }

    if (!(otbr_cli_get_number("eui64", eui64, sizeof(*eui64), 16)
          && otbr_cli_get_number("extaddr", ext_addr, sizeof(*ext_addr), 16)
          && otbr_cli_get_number("thread version", &thread_version, sizeof(thread_version), 10)
          && otbr_cli_get_number("version api", &api_version, sizeof(api_version), 10)
          && otbr_cli_get_string("rcp version", rcp_version, sizeof(rcp_version)) && otbr_cli_exec("ifconfig up", -1)))
    {
        otbr_cli_stop();
        return false;
    }

    /* Trigger initial topology update as soon as OTBR starts (after 1 s).
     * Keep the repeat value as it is, so that timer will be run again
     * automatically if any interval was set with osp_otbr_set_report_interval(). */
    timer_start(&g_timers.topology, 1, -1);
    /* Other timers are just restarted with the same repeating interval, if any */
    timer_start(&g_timers.discovery, 0, -1);

    LOGI("OSP OTBR started (EUI-64 = %016llx, ext. addr = %016llx, Thread v%u API %d, RCP %s)",
         *eui64,
         *ext_addr,
         thread_version,
         api_version,
         rcp_version);
    return true;
}

static void timer_set_interval(struct ev_timer *const timer, const int interval, const char *const name)
{
    if (interval > 0)
    {
        timer->repeat = interval;

        if (otbr_cli_is_running())
        {
            /* Timer might already be active from osp_otbr_start(), do not reset that timeout */
            if (!ev_is_active(timer))
            {
                ev_timer_again(g_loop, timer);
            }
            LOGI("OSP OTBR %s every %d s, now", name, interval);
        }
        else
        {
            LOGI("OSP OTBR %s every %d s, when enabled", name, interval);
        }
    }
    else if ((interval == 0) && ev_is_active(timer))
    {
        ev_timer_stop(g_loop, timer);
        LOGI("OSP OTBR %s stopped", name);
    }
}

bool osp_otbr_set_report_interval(const int topology, const int discovery)
{
    timer_set_interval(&g_timers.topology, topology, "topology reporting");
    timer_set_interval(&g_timers.discovery, discovery, "discovery");
    return true;
}

bool osp_otbr_create_network(const struct otbr_osp_network_params_s *params, struct osp_otbr_dataset_tlvs_s *dataset)
{
    ssize_t len;
    bool ok;

    /* Generate a new dataset with all values randomly generated, then overwrite the provided parameters */
    ok = otbr_cli_exec("dataset init new", -1)
         && otbr_cli_exec(otbr_cli_cmd("dataset networkname %s", params->network_name), -1)
         && ((params->pan_id == UINT16_MAX)
             || otbr_cli_exec(otbr_cli_cmd("dataset panid 0x%04" PRIx16, params->pan_id), -1))
         && ((params->ext_pan_id == UINT64_MAX)
             || otbr_cli_exec(otbr_cli_cmd("dataset extpanid %" PRIx64, params->ext_pan_id), -1))
         && (memcmp_b(&params->mesh_local_prefix, 0, sizeof(params->mesh_local_prefix))
             || otbr_cli_exec(
                     otbr_cli_cmd("dataset meshlocalprefix %s", FMT_osn_ip6_addr(params->mesh_local_prefix)),
                     -1))
         && ((params->channel == 0) || (params->channel == UINT8_MAX)
             || otbr_cli_exec(otbr_cli_cmd("dataset channel %" PRIu8, params->channel), -1))
         && otbr_cli_exec(otbr_cli_cmd("dataset channelmask 0x%08x", params->channel_mask), -1)
         && ((params->commissioning_psk == NULL)
             || otbr_cli_exec(otbr_cli_cmd("dataset pskc -p %s", params->commissioning_psk), -1));
    if (ok && !memcmp_b(params->network_key, 0, sizeof(params->network_key)))
    {
        char nk[sizeof(params->network_key) * 2 + 1];

        if (bin2hex(params->network_key, sizeof(params->network_key), nk, sizeof(nk)) != 0)
        {
            LOGE("Network key encoding error");
            return false;
        }

        ok = otbr_cli_exec(otbr_cli_cmd("dataset networkkey %s", nk), -1);
    }

    if (!ok)
    {
        LOGE("Failed to create Thread network using provided parameters");
        return false;
    }

    /* Get TLVs for this new operational dataset */
    len = otbr_cli_get_array("dataset tlvs", dataset->tlvs, sizeof(dataset->tlvs));
    if ((len < 0) || (len > (ssize_t)sizeof(dataset->tlvs)))
    {
        LOGE("Invalid dataset (%zd)", len);
        return false;
    }
    else
    {
        dataset->len = (uint8_t)len;
        return true;
    }
}

bool osp_otbr_set_dataset(const struct osp_otbr_dataset_tlvs_s *dataset, bool active)
{
    char str[sizeof(dataset->tlvs) * 2 + 1];
    otDeviceRole role;
    bool success;

    if (bin2hex(dataset->tlvs, dataset->len, str, sizeof(str)) != 0)
    {
        LOGE("Dataset encoding error");
        return false;
    }

    /* If radio is enabled, stop it before setting the dataset, then start it again */
    if (!(otbr_cli_get_role(&role) && ((role == OT_DEVICE_ROLE_DISABLED) || otbr_cli_exec("thread stop", -1))))
    {
        return false;
    }

    if (dataset->len > 0)
    {
        success = otbr_cli_exec(otbr_cli_cmd("dataset set %s %s", active ? "active" : "pending", str), 5);
    }
    else
    {
        success = otbr_cli_exec(otbr_cli_cmd("dataset clear"), -1)
                  && otbr_cli_exec(otbr_cli_cmd("dataset commit %s", active ? "active" : "pending"), 5);
    }

    if (success && (role != OT_DEVICE_ROLE_DISABLED))
    {
        success = otbr_cli_exec("thread start", -1);
    }

    return success;
}

bool osp_otbr_get_dataset(struct osp_otbr_dataset_tlvs_s *dataset, bool active)
{
    const ssize_t len = otbr_cli_get_array(
            otbr_cli_cmd("dataset %s -x", active ? "active" : "pending"),
            dataset->tlvs,
            sizeof(dataset->tlvs));
    if ((len < 0) || (len > (ssize_t)sizeof(dataset->tlvs)))
    {
        return false;
    }

    dataset->len = (uint8_t)len;
    return true;
}

bool osp_otbr_set_thread_radio(bool enable)
{
    if (otbr_cli_is_running())
    {
        return otbr_cli_exec(otbr_cli_cmd("thread %s", enable ? "start" : "stop"), -1);
    }
    else if (enable)
    {
        LOGE("Cannot enable OTBR radio - OTBR is not running");
        return false;
    }
    return true;
}

bool osp_otbr_stop(void)
{
    /* Try to power off the radio first, but ignore any errors */
    if (otbr_cli_is_running())
    {
        otbr_cli_execute("thread stop", NULL, -1);
        otbr_cli_execute("ifconfig down", NULL, -1);
    }

    timer_stop(&g_timers.topology);
    timer_stop(&g_timers.discovery);

    otbr_cli_stop();

    LOGI("OSP OTBR stopped");
    return true;
}

void osp_otbr_close(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(g_signal_watchers); i++)
    {
        ev_signal *const sig = &g_signal_watchers[i];

        if (ev_is_active(sig))
        {
            ev_signal_stop(g_loop, sig);
        }
    }

    osp_otbr_stop();
    otbr_cli_close();

    g_on_dataset_change_cb = NULL;
    g_on_network_topology_cb = NULL;
    g_on_network_scan_result_cb = NULL;
    g_loop = NULL;

    LOGI("OSP OTBR closed");
}
