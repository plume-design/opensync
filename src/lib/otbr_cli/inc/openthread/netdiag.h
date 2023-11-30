/*
 *  Copyright (c) 2020, The OpenThread Authors.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Modified by: Plume Design Inc.
 * Removed everything except the constants and typedefs.
 */

#ifndef OPENTHREAD_NETDIAG_H_INCLUDED
#define OPENTHREAD_NETDIAG_H_INCLUDED

#include "openthread/ip6.h"
#include "openthread/thread.h"

/** Maximum Number of Network Diagnostic TLV Types to Request or Reset */
#define OT_NETWORK_DIAGNOSTIC_TYPELIST_MAX_ENTRIES 19

/** Size of Network Diagnostic Child Table entry */
#define OT_NETWORK_DIAGNOSTIC_CHILD_TABLE_ENTRY_SIZE 3

typedef enum
{
    OT_NETWORK_DIAGNOSTIC_TLV_EXT_ADDRESS = 0,           /**< MAC Extended Address TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_SHORT_ADDRESS = 1,         /**< Address16 TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_MODE = 2,                  /**< Mode TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_TIMEOUT = 3,               /**< Timeout TLV (the maximum polling time period for SEDs) */
    OT_NETWORK_DIAGNOSTIC_TLV_CONNECTIVITY = 4,          /**< Connectivity TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_ROUTE = 5,                 /**< Route64 TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_LEADER_DATA = 6,           /**< Leader Data TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_NETWORK_DATA = 7,          /**< Network Data TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_IP6_ADDR_LIST = 8,         /**< IPv6 Address List TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_MAC_COUNTERS = 9,          /**< MAC Counters TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_BATTERY_LEVEL = 14,        /**< Battery Level TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_SUPPLY_VOLTAGE = 15,       /**< Supply Voltage TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_CHILD_TABLE = 16,          /**< Child Table TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_CHANNEL_PAGES = 17,        /**< Channel Pages TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_TYPE_LIST = 18,            /**< Type List TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_MAX_CHILD_TIMEOUT = 19,    /**< Max Child Timeout TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_VERSION = 24,              /**< Version TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_VENDOR_NAME = 25,          /**< Vendor Name TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_VENDOR_MODEL = 26,         /**< Vendor Model TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_VENDOR_SW_VERSION = 27,    /**< Vendor SW Version TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_THREAD_STACK_VERSION = 28, /**< Thread Stack Version TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_CHILD = 29,                /**< Child TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_CHILD_IP6_ADDR_LIST = 30,  /**< Child IPv6 Address List TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_ROUTER_NEIGHBOR = 31,      /**< Router Neighbor TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_ANSWER = 32,               /**< Answer TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_QUERY_ID = 33,             /**< Query ID TLV */
    OT_NETWORK_DIAGNOSTIC_TLV_MLE_COUNTERS = 34,         /**< MLE Counters TLV */
} otNetworkDiagTlvType;

#define OT_NETWORK_DIAGNOSTIC_MAX_VENDOR_NAME_TLV_LENGTH 32          /**< Max length of Vendor Name TLV */
#define OT_NETWORK_DIAGNOSTIC_MAX_VENDOR_MODEL_TLV_LENGTH 32         /**< Max length of Vendor Model TLV */
#define OT_NETWORK_DIAGNOSTIC_MAX_VENDOR_SW_VERSION_TLV_LENGTH 16    /**< Max length of Vendor SW Version TLV */
#define OT_NETWORK_DIAGNOSTIC_MAX_THREAD_STACK_VERSION_TLV_LENGTH 64 /**< Max length of Thread Stack Version TLV */

/** Represents a Network Diagnostic Connectivity value */
typedef struct otNetworkDiagConnectivity
{
    /** The priority of the sender as a parent */
    int8_t mParentPriority;
    /** The number of neighboring devices with which the sender shares a link of quality 3 */
    uint8_t mLinkQuality3;
    /** The number of neighboring devices with which the sender shares a link of quality 2 */
    uint8_t mLinkQuality2;
    /** The number of neighboring devices with which the sender shares a link of quality 1 */
    uint8_t mLinkQuality1;
    /** The sender's routing cost to the Leader */
    uint8_t mLeaderCost;
    /** The most recent ID sequence number received by the sender */
    uint8_t mIdSequence;
    /** The number of active Routers in the sender's Thread Network Partition */
    uint8_t mActiveRouters;
    /** The optional guaranteed buffer capacity in octets for all IPv6 datagrams destined to a given SED */
    uint16_t mSedBufferSize;
    /** The optional guaranteed queue capacity in number of IPv6 datagrams destined to a given SED */
    uint8_t mSedDatagramCount;
} otNetworkDiagConnectivity;

/** Represents a Network Diagnostic Route data */
typedef struct otNetworkDiagRouteData
{
    uint8_t mRouterId;           /**< The Assigned Router ID */
    uint8_t mLinkQualityOut : 2; /**< Link Quality Out */
    uint8_t mLinkQualityIn : 2;  /**< Link Quality In */
    uint8_t mRouteCost : 4;      /**< Routing Cost. Infinite routing cost is represented by value 0 */
} otNetworkDiagRouteData;

/** Represents a Network Diagnostic Route TLV value */
typedef struct otNetworkDiagRoute
{
    /** The sequence number associated with the set of Router ID assignments in #mRouteData */
    uint8_t mIdSequence;
    /** Number of elements in #mRouteData */
    uint8_t mRouteCount;
    /** Link Quality and Routing Cost data */
    otNetworkDiagRouteData mRouteData[OT_NETWORK_MAX_ROUTER_ID + 1];
} otNetworkDiagRoute;

/**
 * Represents a Network Diagnostic Mac Counters value
 *
 * See <a href="https://www.ietf.org/rfc/rfc2863">RFC 2863</a> for definitions of member fields.
 */
typedef struct otNetworkDiagMacCounters
{
    uint32_t mIfInUnknownProtos;
    uint32_t mIfInErrors;
    uint32_t mIfOutErrors;
    uint32_t mIfInUcastPkts;
    uint32_t mIfInBroadcastPkts;
    uint32_t mIfInDiscards;
    uint32_t mIfOutUcastPkts;
    uint32_t mIfOutBroadcastPkts;
    uint32_t mIfOutDiscards;
} otNetworkDiagMacCounters;

/** Represents a Network Diagnostics MLE Counters value */
typedef struct otNetworkDiagMleCounters
{
    uint16_t mDisabledRole;                  /**< Number of times device entered disabled role */
    uint16_t mDetachedRole;                  /**< Number of times device entered detached role */
    uint16_t mChildRole;                     /**< Number of times device entered child role */
    uint16_t mRouterRole;                    /**< Number of times device entered router role */
    uint16_t mLeaderRole;                    /**< Number of times device entered leader role */
    uint16_t mAttachAttempts;                /**< Number of attach attempts while device was detached */
    uint16_t mPartitionIdChanges;            /**< Number of changes to partition ID */
    uint16_t mBetterPartitionAttachAttempts; /**< Number of attempts to attach to a better partition */
    uint16_t mParentChanges;                 /**< Number of time device changed its parent */
    uint64_t mTrackedTime;                   /**< Milliseconds tracked by next counters (zero if not supported) */
    uint64_t mDisabledTime;                  /**< Milliseconds device has been in disabled role */
    uint64_t mDetachedTime;                  /**< Milliseconds device has been in detached role */
    uint64_t mChildTime;                     /**< Milliseconds device has been in child role */
    uint64_t mRouterTime;                    /**< Milliseconds device has been in router role */
    uint64_t mLeaderTime;                    /**< Milliseconds device has been in leader role */
} otNetworkDiagMleCounters;

/** Represents a Network Diagnostic Child Table Entry */
typedef struct otNetworkDiagChildEntry
{
    /** Expected poll time expressed as 2^(Timeout-4) seconds */
    uint16_t mTimeout : 5;
    /** Link Quality In value in [0,3] - 0 indicates that sender does not support providing link quality info */
    uint8_t mLinkQuality : 2;
    /** Child ID from which an RLOC can be generated */
    uint16_t mChildId : 9;
    /** Link mode bits */
    otLinkModeConfig mMode;
} otNetworkDiagChildEntry;

/** Represents a Network Diagnostic TLV */
typedef struct otNetworkDiagTlv
{
    /** The Network Diagnostic TLV type */
    otNetworkDiagTlvType mType;

    union
    {
        otExtAddress mExtAddress;
        uint16_t mAddr16;
        otLinkModeConfig mMode;
        uint32_t mTimeout;
        otNetworkDiagConnectivity mConnectivity;
        otNetworkDiagRoute mRoute;
        otLeaderData mLeaderData;
        otNetworkDiagMacCounters mMacCounters;
        otNetworkDiagMleCounters mMleCounters;
        uint8_t mBatteryLevel;
        uint16_t mSupplyVoltage;
        uint32_t mMaxChildTimeout;
        uint16_t mVersion;
        char mVendorName[OT_NETWORK_DIAGNOSTIC_MAX_VENDOR_NAME_TLV_LENGTH + 1];
        char mVendorModel[OT_NETWORK_DIAGNOSTIC_MAX_VENDOR_MODEL_TLV_LENGTH + 1];
        char mVendorSwVersion[OT_NETWORK_DIAGNOSTIC_MAX_VENDOR_SW_VERSION_TLV_LENGTH + 1];
        char mThreadStackVersion[OT_NETWORK_DIAGNOSTIC_MAX_THREAD_STACK_VERSION_TLV_LENGTH + 1];
        struct
        {
            uint8_t mCount;
            uint8_t m8[OT_NETWORK_BASE_TLV_MAX_LENGTH];
        } mNetworkData;
        struct
        {
            uint8_t mCount;
            otIp6Address mList[OT_NETWORK_BASE_TLV_MAX_LENGTH / OT_IP6_ADDRESS_SIZE];
        } mIp6AddrList;
        struct
        {
            uint8_t mCount;
            otNetworkDiagChildEntry
                    mTable[OT_NETWORK_BASE_TLV_MAX_LENGTH / OT_NETWORK_DIAGNOSTIC_CHILD_TABLE_ENTRY_SIZE];
        } mChildTable;
        struct
        {
            uint8_t mCount;
            uint8_t m8[OT_NETWORK_BASE_TLV_MAX_LENGTH];
        } mChannelPages;
        struct
        {
            uint8_t mCount;
            uint8_t mList[OT_NETWORK_BASE_TLV_MAX_LENGTH];
        } mTypeList;
    } mData;
} otNetworkDiagTlv;

#endif /* OPENTHREAD_NETDIAG_H_INCLUDED */
