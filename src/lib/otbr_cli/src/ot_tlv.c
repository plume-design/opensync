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

#include "ot_tlv.h"
#include "const.h"
#include "log.h"

#include "openthread/misc.h"

/** Macro to define a `variable_name` pointer variable pointing to `to_what` */
#define OT_TLV_POINTER(variable_name, to_what) __typeof__(to_what) *const variable_name = &(to_what)

#ifdef DEBUG
#define OT_TLV_DEBUG(fmt, ...) LOGT("OT_TLV: " fmt, __VA_ARGS__)
#else
#define OT_TLV_DEBUG(fmt, ...) \
    do \
    { \
    } while (0)
#endif

/**
 * Function type for parsing Thread TLV value
 *
 * @param[in,out] buff      Pointer to the buffer with the TLV value.
 *                          Will be moved to point to the byte after the parsed TLV value.
 *                          Note that if this function parsed the last TLV in the buffer, this can
 *                          point to the byte after the last TLV - out of the buffer bounds.
 * @param[in]     buff_end  Pointer to the end of the TLV buffer (byte after the last byte of the buffer),
 *                          used to check the TLV value size.
 * @param[out]    tlv       Pointer to the TLV value structure to fill.
 *
 * @return true if the TLV value was parsed successfully, false otherwise (error is logged internally).
 */
typedef bool (*parse_tlv_func_t)(const uint8_t **buff, const uint8_t *buff_end, otNetworkDiagTlv *tlv);

/**
 * Structure representing Thread TLV encoded value
 *
 * TLVs are stored serially with no padding between them. They are byte-aligned
 * but are not aligned in any other way such as on 2- or 4-byte boundaries.
 *
 * @note All values in TLVs are in network byte order.
 */
struct __attribute__((__packed__)) ot_tlv_s
{
    uint8_t type; /**< TLV value type */
    union
    {
        /** Base TLV format (value length up to 254 octets), valid when `base.len` is not 0xFF */
        struct __attribute__((__packed__))
        {
            uint8_t len;     /**< The length of the the `value` field, from 0 to 254 octets */
            uint8_t value[]; /**< `len` octets of value formatted as defined for the `type` */
        } base;
        /** Extended TLV format (value length up to 65535 octets), valid when `ext::indicator` equals 0xFF */
        struct __attribute__((__packed__))
        {
            uint8_t ext_len_indicator; /**< Value indicating the Extended TLV Format, must be 0xFF */
            uint16_t len;              /**< The length of the the `value` field, from 0 to 65535 octets */
            uint8_t value[];           /**< `len` octets of value formatted as defined for the `type` */
        } ext;
    };
};

/** "Take" bytes from the buffer - advance the buffer pointer for `octets` bytes and return the original `*buffer` */
static inline const uint8_t *take_n(const uint8_t **const buffer, const size_t octets)
{
    const uint8_t *const pb = *buffer;
    *buffer += octets;
    return pb;
}

/** "Move" `octets` bytes from the `src` buffer using @ref take_n, to the destination buffer `dst` */
static inline void copy_n(void *dst, const uint8_t **const src, const size_t octets)
{
    memcpy(dst, take_n(src, octets), octets);
}

/** "Take" 8-bit value from the buffer - get the value and advance the buffer pointer 1 byte */
static inline uint8_t take_u8(const uint8_t **const buffer)
{
    return *(*buffer)++;
}

/** "Take" 16-bit value from the network-endian buffer - get the value and advance the buffer pointer for 2 bytes */
static inline uint16_t take_u16(const uint8_t **const buffer)
{
    const uint16_t value = (uint16_t)(((*buffer)[0] << 8) | (*buffer)[1]);
    *buffer += 2;
    return value;
}

/** "Take" 32-bit value from the buffer - get the value and advance the buffer pointer for the value size */
static inline uint32_t take_u32(const uint8_t **const buffer)
{
    const uint32_t value = (*buffer)[0] << 24 | (*buffer)[1] << 16 | (*buffer)[2] << 8 | (*buffer)[3];
    *buffer += 4;
    return value;
}

/** "Take" 64-bit value from the buffer - get the value and advance the buffer pointer for the value size */
static inline uint64_t take_u64(const uint8_t **const buffer)
{
    const uint64_t value = (uint64_t)(*buffer)[0] << 56 | (uint64_t)(*buffer)[1] << 48 | (uint64_t)(*buffer)[2] << 40
                           | (uint64_t)(*buffer)[3] << 32 | (uint64_t)(*buffer)[4] << 24 | (uint64_t)(*buffer)[5] << 16
                           | (uint64_t)(*buffer)[6] << 8 | (uint64_t)(*buffer)[7];
    *buffer += 8;
    return value;
}

/**
 * Check if the TLV value size is within the expected range
 *
 * @param[in] ptr                    Pointer to the TLV value.
 * @param[in] end_ptr                Pointer to the end of the TLV buffer (byte after the last byte of the buffer).
 * @param[in] actual                 Size of the TLV value to check.
 * @param[in] expected_exact_or_min  Expected exact size of the TLV value,
 *                                   or minimum size if `expected_max` is not 0.
 * @param[in] expected_max           Expected maximum size of the TLV value.
 * @param[in] name                   Name of the TLV value used for logging - if NULL, no logging is performed.
 *
 * @return true if `expected_max` is 0 and the TLV value size is equal to `expected_exact_or_min`, or
 *         true if the TLV value size is between `expected_exact_or_min` and `expected_max`.
 *         false otherwise (error is logged internally if `name` is not NULL).
 */
static bool check_tlv_size(
        const uint8_t *const ptr,
        const uint8_t *const end_ptr,
        const size_t expected_exact_or_min,
        const size_t expected_max,
        const char *const name)
{
    const size_t len = (end_ptr > ptr) ? end_ptr - ptr : 0;

    if (expected_max == 0)
    {
        if (len != expected_exact_or_min)
        {
            if (name != NULL)
            {
                LOGE("Invalid %s TLV length (%u != %u)", name, len, expected_exact_or_min);
            }
            return false;
        }
    }
    else if ((len < expected_exact_or_min) || (len > expected_max))
    {
        if (name != NULL)
        {
            LOGE("Invalid %s TLV length (not %u <= %u <= %u)", name, expected_exact_or_min, len, expected_max);
        }
        return false;
    }

    return true;
}

static bool parse_tlv_mac_extended_address(
        const uint8_t **const buff,
        const uint8_t *const buff_end,
        otNetworkDiagTlv *const tlv)
{
    OT_TLV_POINTER(ea, tlv->mData.mExtAddress);

    if (!check_tlv_size(*buff, buff_end, sizeof(ea->m8), 0, "ExtAddress"))
    {
        return false;
    }

    copy_n(ea->m8, buff, sizeof(ea->m8));

    OT_TLV_DEBUG(
            "ExtAddress: %02x%02x%02x%02x%02x%02x%02x%02x",
            ea->m8[0],
            ea->m8[1],
            ea->m8[2],
            ea->m8[3],
            ea->m8[4],
            ea->m8[5],
            ea->m8[6],
            ea->m8[7]);
    return true;
}

static bool parse_tlv_address16(const uint8_t **const buff, const uint8_t *const buff_end, otNetworkDiagTlv *const tlv)
{
    OT_TLV_POINTER(a16, tlv->mData.mAddr16);

    if (!check_tlv_size(*buff, buff_end, sizeof(uint16_t), 0, "Address16"))
    {
        return false;
    }

    *a16 = take_u16(buff);

    OT_TLV_DEBUG("Address16: %04X", *a16);
    return true;
}

static bool parse_mode(const uint8_t **const buff, const uint8_t *const buff_end, otLinkModeConfig *const lmc)
{
    uint8_t mode;

    /* Do not check for exact size, as the mode can be taken from a larger buffer */
    if (!check_tlv_size(*buff, buff_end, sizeof(uint8_t), UINT16_MAX, "Mode"))
    {
        return false;
    }

    mode = take_u8(buff);
    lmc->mRxOnWhenIdle = (mode & (1u << 3)) != 0;
    lmc->mDeviceType = (mode & (1u << 1)) != 0;
    lmc->mNetworkData = (mode & (1u << 0)) != 0;

    return true;
}

static bool parse_tlv_mode(const uint8_t **const buff, const uint8_t *const buff_end, otNetworkDiagTlv *const tlv)
{
    OT_TLV_POINTER(lmc, tlv->mData.mMode);

    if (parse_mode(buff, buff_end, lmc))
    {
        OT_TLV_DEBUG("Mode: RxOWI=%u, DT=%u, ND=%u", lmc->mRxOnWhenIdle, lmc->mDeviceType, lmc->mNetworkData);
        return true;
    }
    return false;
}

static bool parse_tlv_timeout(const uint8_t **const buff, const uint8_t *const buff_end, otNetworkDiagTlv *const tlv)
{
    OT_TLV_POINTER(to, tlv->mData.mTimeout);

    if (!check_tlv_size(*buff, buff_end, sizeof(uint32_t), 0, "Timeout"))
    {
        return false;
    }

    *to = take_u32(buff);

    OT_TLV_DEBUG("Timeout: %u", *to);
    return true;
}

static bool parse_tlv_connectivity(
        const uint8_t **const buff,
        const uint8_t *const buff_end,
        otNetworkDiagTlv *const tlv)
{
    static const int8_t preferences[] = {
        [0] = 0, /* Medium preference */
        [1] = 1, /* High preference */
        [2] = 0, /* Per RFC-4191, the reserved value (10) MUST be treated as (00) */
        [3] = -1 /* Low preference */
    };
    OT_TLV_POINTER(ndc, tlv->mData.mConnectivity);

    /* Last 3 out of 10 bytes are optional, but the length shall be either 7 or 10 bytes */
    if (!(check_tlv_size(*buff, buff_end, 7, 0, NULL) || check_tlv_size(*buff, buff_end, 10, 0, "Connectivity")))
    {
        return false;
    }

    ndc->mParentPriority = preferences[(take_u8(buff) >> 6) & 0b11];
    ndc->mLinkQuality3 = take_u8(buff);
    ndc->mLinkQuality2 = take_u8(buff);
    ndc->mLinkQuality1 = take_u8(buff);
    ndc->mLeaderCost = take_u8(buff);
    ndc->mIdSequence = take_u8(buff);
    ndc->mActiveRouters = take_u8(buff);
    if (check_tlv_size(*buff, buff_end, 3, 0, NULL))
    {
        ndc->mSedBufferSize = take_u16(buff);
        ndc->mSedDatagramCount = take_u8(buff);
    }
    else
    {
        ndc->mSedBufferSize = OPENTHREAD_CONFIG_DEFAULT_SED_BUFFER_SIZE;
        ndc->mSedDatagramCount = OPENTHREAD_CONFIG_DEFAULT_SED_DATAGRAM_COUNT;
    }

    OT_TLV_DEBUG(
            "Connectivity: PP=%d, LQ3=%u, LQ2=%u, LQ1=%u, LC=%u, IdS=%u, AR=%u, SedBS=%u, SedDC=%u",
            ndc->mParentPriority,
            ndc->mLinkQuality3,
            ndc->mLinkQuality2,
            ndc->mLinkQuality1,
            ndc->mLeaderCost,
            ndc->mIdSequence,
            ndc->mActiveRouters,
            ndc->mSedBufferSize,
            ndc->mSedDatagramCount);
    return true;
}

static bool parse_tlv_route64(const uint8_t **const buff, const uint8_t *const buff_end, otNetworkDiagTlv *const tlv)
{
    const uint8_t router_mask_len = (((OT_NETWORK_MAX_ROUTER_ID + 1) + (8 - 1)) / 8);
    OT_TLV_POINTER(ndr, tlv->mData.mRoute);
    const uint8_t *router_mask;

    if (!check_tlv_size(
                *buff,
                buff_end,
                sizeof(uint8_t) + router_mask_len,
                sizeof(uint8_t) + router_mask_len + OT_NETWORK_MAX_ROUTER_ID + 1,
                "Route"))
    {
        return false;
    }

    ndr->mIdSequence = take_u8(buff);
    router_mask = take_n(buff, router_mask_len);
    for (uint8_t i = 0; i <= OT_NETWORK_MAX_ROUTER_ID; i++)
    {
        if ((router_mask[i / 8] & (0x80 >> (i % 8))) == 0)
        {
            continue;
        }

        if (*buff < buff_end)
        {
            const uint8_t lq_and_route_data = take_u8(buff);
            OT_TLV_POINTER(rd, ndr->mRouteData[ndr->mRouteCount++]);

            rd->mRouterId = i;
            rd->mRouteCost = (lq_and_route_data & (0xF << 0)) >> 0;
            rd->mLinkQualityIn = (lq_and_route_data & (3 << 4)) >> 4;
            rd->mLinkQualityOut = (lq_and_route_data & (3 << 6)) >> 6;

            OT_TLV_DEBUG(
                    "Route %d: RId=%u, C=%u, LqIn=%u, LqOut=%u",
                    ndr->mRouteCount,
                    rd->mRouterId,
                    rd->mRouteCost,
                    rd->mLinkQualityIn,
                    rd->mLinkQualityOut);
        }
        else
        {
            LOGE("Insufficient Route TLV data");
            return false;
        }
    }

    OT_TLV_DEBUG("Route: IdS=%u, RC=%u", ndr->mIdSequence, ndr->mRouteCount);
    return true;
}

static bool parse_tlv_leader_data(
        const uint8_t **const buff,
        const uint8_t *const buff_end,
        otNetworkDiagTlv *const tlv)
{
    OT_TLV_POINTER(ld, tlv->mData.mLeaderData);

    if (!check_tlv_size(*buff, buff_end, sizeof(uint32_t) + 4 * sizeof(uint8_t), 0, "LeaderData"))
    {
        return false;
    }

    ld->mPartitionId = take_u32(buff);
    ld->mWeighting = take_u8(buff);
    ld->mDataVersion = take_u8(buff);
    ld->mStableDataVersion = take_u8(buff);
    ld->mLeaderRouterId = take_u8(buff);

    OT_TLV_DEBUG(
            "LeaderData: PId=%u, W=%u, DV=%u, SDV=%u, LRId=%u",
            ld->mPartitionId,
            ld->mWeighting,
            ld->mDataVersion,
            ld->mStableDataVersion,
            ld->mLeaderRouterId);
    return true;
}

static bool parse_tlv_network_data(
        const uint8_t **const buff,
        const uint8_t *const buff_end,
        otNetworkDiagTlv *const tlv)
{
    OT_TLV_POINTER(nd, tlv->mData.mNetworkData);

    if (!check_tlv_size(*buff, buff_end, 0, sizeof(nd->m8), "NetworkData"))
    {
        return false;
    }

    nd->mCount = (uint8_t)(buff_end - *buff);
    copy_n(nd->m8, buff, nd->mCount);

    OT_TLV_DEBUG("NetworkData: C=%u", nd->mCount);
    return true;
}

static bool parse_tlv_ipv6_addr_list(
        const uint8_t **const buff,
        const uint8_t *const buff_end,
        otNetworkDiagTlv *const tlv)
{
    OT_TLV_POINTER(ial, tlv->mData.mIp6AddrList);
    C_STATIC_ASSERT(sizeof(ial->mList[0].mFields.m8) == sizeof(otIp6Address), "otIp6Address is not packed");
    C_STATIC_ASSERT(sizeof(otIp6Address) == OT_IP6_ADDRESS_SIZE, "Invalid otIp6Address size");

    if (!check_tlv_size(*buff, buff_end, 0, sizeof(ial->mList), "Ip6AddrList"))
    {
        return false;
    }

    while ((size_t)(buff_end - *buff) >= sizeof(otIp6Address))
    {
        copy_n(&ial->mList[ial->mCount++], buff, sizeof(otIp6Address));
    }

    OT_TLV_DEBUG("Ip6AddrList: C=%u", ial->mCount);
    return true;
}

static bool parse_tlv_mac_counters(
        const uint8_t **const buff,
        const uint8_t *const buff_end,
        otNetworkDiagTlv *const tlv)
{
    OT_TLV_POINTER(mc, tlv->mData.mMacCounters);

    if (!check_tlv_size(*buff, buff_end, 10 * sizeof(uint32_t), 0, "MacCounters"))
    {
        return false;
    }

    mc->mIfInUnknownProtos = take_u32(buff);
    mc->mIfInErrors = take_u32(buff);
    mc->mIfOutErrors = take_u32(buff);
    mc->mIfInUcastPkts = take_u32(buff);
    mc->mIfInBroadcastPkts = take_u32(buff);
    mc->mIfInDiscards = take_u32(buff);
    mc->mIfOutUcastPkts = take_u32(buff);
    mc->mIfOutBroadcastPkts = take_u32(buff);
    mc->mIfOutDiscards = take_u32(buff);

    OT_TLV_DEBUG(
            "MacCounters: IfIn(Un=%u, E=%u, Uc=%u, B=%u, D=%u), IfOut(E=%u, Uc=%u, B=%u, D=%u)",
            mc->mIfInUnknownProtos,
            mc->mIfInErrors,
            mc->mIfInUcastPkts,
            mc->mIfInBroadcastPkts,
            mc->mIfInDiscards,
            mc->mIfOutErrors,
            mc->mIfOutUcastPkts,
            mc->mIfOutBroadcastPkts,
            mc->mIfOutDiscards);
    return true;
}

static bool parse_tlv_mle_counters(
        const uint8_t **const buff,
        const uint8_t *const buff_end,
        otNetworkDiagTlv *const tlv)
{
    OT_TLV_POINTER(mc, tlv->mData.mMleCounters);

    if (!check_tlv_size(*buff, buff_end, 9 * sizeof(uint16_t) + 6 * sizeof(uint64_t), 0, "MleCounters"))
    {
        return false;
    }

    mc->mDisabledRole = take_u16(buff);
    mc->mDetachedRole = take_u16(buff);
    mc->mChildRole = take_u16(buff);
    mc->mRouterRole = take_u16(buff);
    mc->mLeaderRole = take_u16(buff);
    mc->mAttachAttempts = take_u16(buff);
    mc->mPartitionIdChanges = take_u16(buff);
    mc->mBetterPartitionAttachAttempts = take_u16(buff);
    mc->mParentChanges = take_u16(buff);
    mc->mTrackedTime = take_u64(buff);
    mc->mDisabledTime = take_u64(buff);
    mc->mDetachedTime = take_u64(buff);
    mc->mChildTime = take_u64(buff);
    mc->mRouterTime = take_u64(buff);
    mc->mLeaderTime = take_u64(buff);

    OT_TLV_DEBUG(
            "MleCounters: Role(Di=%u, De=%u, Ch=%u, Ro=%u, Le=%u),"
            " AA=%u, PIC=%u, BPAA=%u, PC=%u,"
            " Time(Tr=%llu, Di=%llu, De=%llu, Ch=%llu, Ro=%llu, Le=%llu)",
            mc->mDisabledRole,
            mc->mDetachedRole,
            mc->mChildRole,
            mc->mRouterRole,
            mc->mLeaderRole,
            mc->mAttachAttempts,
            mc->mPartitionIdChanges,
            mc->mBetterPartitionAttachAttempts,
            mc->mParentChanges,
            mc->mTrackedTime,
            mc->mDisabledTime,
            mc->mDetachedTime,
            mc->mChildTime,
            mc->mRouterTime,
            mc->mLeaderTime);
    return true;
}

static bool parse_tlv_battery_level(
        const uint8_t **const buff,
        const uint8_t *const buff_end,
        otNetworkDiagTlv *const tlv)
{
    OT_TLV_POINTER(bl, tlv->mData.mBatteryLevel);

    if (!check_tlv_size(*buff, buff_end, sizeof(uint8_t), 0, "BatteryLevel"))
    {
        return false;
    }

    *bl = take_u8(buff);

    OT_TLV_DEBUG("BatteryLevel: %u", *bl);
    return true;
}

static bool parse_tlv_supply_voltage(
        const uint8_t **const buff,
        const uint8_t *const buff_end,
        otNetworkDiagTlv *const tlv)
{
    OT_TLV_POINTER(sv, tlv->mData.mSupplyVoltage);

    if (!check_tlv_size(*buff, buff_end, sizeof(uint16_t), 0, "SupplyVoltage"))
    {
        return false;
    }

    *sv = take_u16(buff);

    OT_TLV_DEBUG("SupplyVoltage: %u", *sv);
    return true;
}

static bool parse_tlv_child_table(
        const uint8_t **const buff,
        const uint8_t *const buff_end,
        otNetworkDiagTlv *const tlv)
{
    OT_TLV_POINTER(ct, tlv->mData.mChildTable);

    if (!check_tlv_size(*buff, buff_end, 0, sizeof(ct->mTable), "ChildTable"))
    {
        return false;
    }

    while ((size_t)(buff_end - *buff) >= 3) /*< Size of packed otNetworkDiagChildEntry */
    {
        otNetworkDiagChildEntry *const ce = &ct->mTable[ct->mCount++];
        const uint16_t timeout_child_id = take_u16(buff);

        ce->mTimeout = (timeout_child_id & (0x1F << 11)) >> 11;
        ce->mLinkQuality = (timeout_child_id & (0x3 << 9)) >> 9;
        ce->mChildId = (timeout_child_id & (0x1FF << 0)) >> 0;
        if (!parse_mode(buff, buff_end, &ce->mMode))
        {
            return false;
        }

        OT_TLV_DEBUG(
                "ChildTable %u: T=%u, LQ=%u, CId=%u, Mode(RxOWI=%u, DT=%u, ND=%u)",
                ct->mCount,
                ce->mTimeout,
                ce->mLinkQuality,
                ce->mChildId,
                ce->mMode.mRxOnWhenIdle,
                ce->mMode.mDeviceType,
                ce->mMode.mNetworkData);
    }

    OT_TLV_DEBUG("ChildTable: C=%u", ct->mCount);
    return true;
}

static bool parse_tlv_channel_pages(
        const uint8_t **const buff,
        const uint8_t *const buff_end,
        otNetworkDiagTlv *const tlv)
{
    OT_TLV_POINTER(cp, tlv->mData.mChannelPages);

    if (!check_tlv_size(*buff, buff_end, 0, sizeof(cp->m8), "ChannelPages"))
    {
        return false;
    }

    cp->mCount = (uint8_t)(buff_end - *buff);
    copy_n(cp->m8, buff, cp->mCount);

    OT_TLV_DEBUG("ChannelPages: C=%u", cp->mCount);
    return true;
}

static bool parse_tlv_type_list(const uint8_t **const buff, const uint8_t *const buff_end, otNetworkDiagTlv *const tlv)
{
    OT_TLV_POINTER(cp, tlv->mData.mTypeList);

    if (!check_tlv_size(*buff, buff_end, 0, sizeof(cp->mList), "TypeList"))
    {
        return false;
    }

    cp->mCount = (uint8_t)(buff_end - *buff);
    copy_n(cp->mList, buff, cp->mCount);

    OT_TLV_DEBUG("TypeList: C=%u", cp->mCount);
    return true;
}

static bool parse_tlv_max_child_timeout(
        const uint8_t **const buff,
        const uint8_t *const buff_end,
        otNetworkDiagTlv *const tlv)
{
    OT_TLV_POINTER(mct, tlv->mData.mMaxChildTimeout);

    if (!check_tlv_size(*buff, buff_end, sizeof(uint32_t), 0, "MaxChildTimeout"))
    {
        return false;
    }

    *mct = take_u32(buff);

    OT_TLV_DEBUG("MaxChildTimeout: %u", *mct);
    return true;
}

static bool parse_tlv_version(const uint8_t **const buff, const uint8_t *const buff_end, otNetworkDiagTlv *const tlv)
{
    OT_TLV_POINTER(ver, tlv->mData.mVersion);

    if (!check_tlv_size(*buff, buff_end, sizeof(uint16_t), 0, "Version"))
    {
        return false;
    }

    *ver = take_u16(buff);

    OT_TLV_DEBUG("Version: %u", *ver);
    return true;
}

static bool parse_tlv_string(const uint8_t **const buff, const uint8_t *const buff_end, otNetworkDiagTlv *const tlv)
{
    const char *name;
    char *p_val;
    size_t max_len;

    switch (tlv->mType)
    {
        case OT_NETWORK_DIAGNOSTIC_TLV_VENDOR_NAME: {
            name = "VendorName";
            p_val = tlv->mData.mVendorName;
            max_len = sizeof(tlv->mData.mVendorName) - 1;
            break;
        }
        case OT_NETWORK_DIAGNOSTIC_TLV_VENDOR_MODEL: {
            name = "VendorModel";
            p_val = tlv->mData.mVendorModel;
            max_len = sizeof(tlv->mData.mVendorModel) - 1;
            break;
        }
        case OT_NETWORK_DIAGNOSTIC_TLV_VENDOR_SW_VERSION: {
            name = "VendorSwVersion";
            p_val = tlv->mData.mVendorSwVersion;
            max_len = sizeof(tlv->mData.mVendorSwVersion) - 1;
            break;
        }
        case OT_NETWORK_DIAGNOSTIC_TLV_THREAD_STACK_VERSION: {
            name = "ThreadStackVersion";
            p_val = tlv->mData.mThreadStackVersion;
            max_len = sizeof(tlv->mData.mThreadStackVersion) - 1;
            break;
        }
        default: {
            LOGE("Invalid string TLV type (%u)", tlv->mType);
            return false;
        }
    }

    if (!check_tlv_size(*buff, buff_end, 0, max_len, name))
    {
        return false;
    }

    copy_n(p_val, buff, buff_end - *buff);

    OT_TLV_DEBUG("%s: '%s'", name, p_val);
    return true;
}

static bool parse_tlv_ignore(const uint8_t **const buff, const uint8_t *const buff_end, otNetworkDiagTlv *const tlv)
{
    LOGT("Ignoring TLV type %u with %zu B data", tlv->mType, buff_end - *buff);
    *buff = buff_end;
    return true;
}

/** Check if the Thread TLV value is encoded in the Extended instead of Basic TLV Format */
static bool ot_tlv_is_extended(const ot_tlv_t *tlv)
{
    C_STATIC_ASSERT(
            offsetof(ot_tlv_t, base.len) == offsetof(ot_tlv_t, ext.ext_len_indicator),
            "ot_tlv_t is not packed");
    return tlv->ext.ext_len_indicator == 0xFF;
}

/** Get the length of the Thread TLV value (0-254 for Basic and 0-65535 for Extended TLV formats) */
static uint16_t ot_tlv_get_length(const ot_tlv_t *tlv)
{
    return ot_tlv_is_extended(tlv) ? ntohs(tlv->ext.len) : tlv->base.len;
}

/** Get the pointer to the Thread TLV value */
static const uint8_t *ot_tlv_get_value(const ot_tlv_t *tlv)
{
    C_STATIC_ASSERT(
            (offsetof(ot_tlv_t, base.value) == 2) && (offsetof(ot_tlv_t, ext.value) == 4),
            "ot_tlv_t is not packed");
    return ot_tlv_is_extended(tlv) ? tlv->ext.value : tlv->base.value;
}

uint8_t ot_tlv_get_type(const ot_tlv_t *const tlv)
{
    return tlv->type;
}

const ot_tlv_t *ot_tlv_get_next(const uint8_t **const buffer, size_t *const buffer_len)
{
    const ot_tlv_t *tlv = (const ot_tlv_t *)*buffer;
    size_t offset = 0;

    if ((*buffer_len == 0) || (*buffer == NULL))
    {
        return NULL;
    }

    /* TLV buffer can either be empty if there is nothing left to parse, or contain at least Type + Length octets
     * to represent the Base TLV type. Assumed Base TLV can then be checked if it is perhaps an Extended TLV, in
     * which case the buffer must contain enough bytes to represent the Extended TLV type, respectively. */
    if (*buffer_len < offsetof(ot_tlv_t, base.value))
    {
        LOGE("Insufficient Base TLV length (%u < %u)", *buffer_len, offsetof(ot_tlv_t, base.value));
        tlv = NULL;
    }
    else if (!ot_tlv_is_extended(tlv))
    {
        offset = offsetof(ot_tlv_t, base.value);
    }
    else if (*buffer_len < offsetof(ot_tlv_t, ext.value))
    {
        LOGE("Insufficient Extended TLV length (%u < %u)", *buffer_len, offsetof(ot_tlv_t, ext.value));
        tlv = NULL;
    }
    else
    {
        offset = offsetof(ot_tlv_t, ext.value);
    }

    if (tlv != NULL)
    {
        /* The lengths of the Type and Length fields are not counted in the Length field */
        offset += ot_tlv_get_length(tlv);
        /* Ensure that there is actually enough data for the reported length of the TLV structure */
        if (offset > *buffer_len)
        {
            /* This is malformed or incomplete data, so we cannot be sure how much data to skip or shift.
             * We could shift for a single byte if some erroneous bytes are expected on the communication line.
             * We could do nothing if data is expected to arrive in multiple parts, and this function will be
             * called again with more data. However, in this case, the communication channel is expected to be
             * stable and all data received at once, so skip the whole malformed buffer to avoid processing
             * it again. */
            LOGE("Invalid TLV length (%u; %u > %u)", ot_tlv_get_length(tlv), offset, *buffer_len);
            offset = *buffer_len;
            tlv = NULL;
        }
    }

    *buffer += offset;
    *buffer_len -= offset;
    return tlv;
}

bool ot_tlv_parse_network_diagnostic_tlv(const ot_tlv_t *const raw_tlv, otNetworkDiagTlv *const tlv)
{
    static const parse_tlv_func_t parse_tlv_functions[] = {
        /* Diagnostic TLVs */
        [OT_NETWORK_DIAGNOSTIC_TLV_EXT_ADDRESS] = parse_tlv_mac_extended_address,
        [OT_NETWORK_DIAGNOSTIC_TLV_SHORT_ADDRESS] = parse_tlv_address16,
        [OT_NETWORK_DIAGNOSTIC_TLV_MODE] = parse_tlv_mode,
        [OT_NETWORK_DIAGNOSTIC_TLV_TIMEOUT] = parse_tlv_timeout,
        [OT_NETWORK_DIAGNOSTIC_TLV_CONNECTIVITY] = parse_tlv_connectivity,
        [OT_NETWORK_DIAGNOSTIC_TLV_ROUTE] = parse_tlv_route64,
        [OT_NETWORK_DIAGNOSTIC_TLV_LEADER_DATA] = parse_tlv_leader_data,
        [OT_NETWORK_DIAGNOSTIC_TLV_NETWORK_DATA] = parse_tlv_network_data,
        [OT_NETWORK_DIAGNOSTIC_TLV_IP6_ADDR_LIST] = parse_tlv_ipv6_addr_list,
        [OT_NETWORK_DIAGNOSTIC_TLV_MAC_COUNTERS] = parse_tlv_mac_counters,
        [OT_NETWORK_DIAGNOSTIC_TLV_BATTERY_LEVEL] = parse_tlv_battery_level,
        [OT_NETWORK_DIAGNOSTIC_TLV_SUPPLY_VOLTAGE] = parse_tlv_supply_voltage,
        [OT_NETWORK_DIAGNOSTIC_TLV_CHILD_TABLE] = parse_tlv_child_table,
        [OT_NETWORK_DIAGNOSTIC_TLV_CHANNEL_PAGES] = parse_tlv_channel_pages,
        [OT_NETWORK_DIAGNOSTIC_TLV_TYPE_LIST] = parse_tlv_type_list,
        [OT_NETWORK_DIAGNOSTIC_TLV_MAX_CHILD_TIMEOUT] = parse_tlv_max_child_timeout,
        /* Other TLVs */
        [OT_NETWORK_DIAGNOSTIC_TLV_VERSION] = parse_tlv_version,
        [OT_NETWORK_DIAGNOSTIC_TLV_VENDOR_NAME] = parse_tlv_string,
        [OT_NETWORK_DIAGNOSTIC_TLV_VENDOR_MODEL] = parse_tlv_string,
        [OT_NETWORK_DIAGNOSTIC_TLV_VENDOR_SW_VERSION] = parse_tlv_string,
        [OT_NETWORK_DIAGNOSTIC_TLV_THREAD_STACK_VERSION] = parse_tlv_string,
        [OT_NETWORK_DIAGNOSTIC_TLV_CHILD] = NULL,
        [OT_NETWORK_DIAGNOSTIC_TLV_CHILD_IP6_ADDR_LIST] = NULL,
        [OT_NETWORK_DIAGNOSTIC_TLV_ROUTER_NEIGHBOR] = NULL,
        [OT_NETWORK_DIAGNOSTIC_TLV_ANSWER] = parse_tlv_ignore,
        [OT_NETWORK_DIAGNOSTIC_TLV_QUERY_ID] = parse_tlv_ignore,
        [OT_NETWORK_DIAGNOSTIC_TLV_MLE_COUNTERS] = parse_tlv_mle_counters,
    };
    static bool logged[ARRAY_SIZE(parse_tlv_functions)] = {};
    const otNetworkDiagTlvType type = ot_tlv_get_type(raw_tlv);
    const uint8_t *const buff = ot_tlv_get_value(raw_tlv);
    const uint16_t length = ot_tlv_get_length(raw_tlv);
    const uint8_t *p_buff;
    parse_tlv_func_t parse_func;

    if (type >= ARRAY_SIZE(parse_tlv_functions))
    {
        LOGE("Invalid TLV type %u with %u B data", type, length);
        return false;
    }
    parse_func = parse_tlv_functions[type];

    if (parse_func == NULL)
    {
        log_severity_t severity;

        if (!logged[type])
        {
            severity = LOG_SEVERITY_WARNING;
            logged[type] = true;
        }
        else
        {
            severity = LOG_SEVERITY_TRACE;
        }
        mlog(severity, MODULE_ID, "Skipping unsupported TLV type %u with %u B data", type, length);

        return true;
    }

    p_buff = buff;
    memset(tlv, 0, sizeof(*tlv));
    tlv->mType = type;
    if (!parse_tlv_functions[type](&p_buff, buff + length, tlv))
    {
        LOGE("Failed to parse TLV type %u with %u B data", type, length);
        return false;
    }
    if ((p_buff - buff) != length)
    {
        LOGW("TLV type %u only parsed %u/%u B of data", type, (p_buff - buff), length);
    }

    return true;
}
