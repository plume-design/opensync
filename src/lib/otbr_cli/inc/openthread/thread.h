/*
 *  Copyright (c) 2016, The OpenThread Authors.
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

#ifndef OPENTHREAD_THREAD_H_INCLUDED
#define OPENTHREAD_THREAD_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

/** Size of an IEEE 802.15.4 Extended Address (bytes) */
#define OT_EXT_ADDRESS_SIZE 8

/** Maximum value length of Thread Base TLV */
#define OT_NETWORK_BASE_TLV_MAX_LENGTH 254

/** Maximum Router ID */
#define OT_NETWORK_MAX_ROUTER_ID 62

/** Represents a Thread device role */
typedef enum
{
    OT_DEVICE_ROLE_DISABLED = 0, /**< The Thread stack is disabled */
    OT_DEVICE_ROLE_DETACHED = 1, /**< Not currently participating in a Thread network/partition */
    OT_DEVICE_ROLE_CHILD = 2,    /**< The Thread Child role */
    OT_DEVICE_ROLE_ROUTER = 3,   /**< The Thread Router role */
    OT_DEVICE_ROLE_LEADER = 4,   /**< The Thread Leader role */
} otDeviceRole;

/** Represents an MLE Link Mode configuration */
typedef struct otLinkModeConfig
{
    bool mRxOnWhenIdle : 1; /**< 1, if the sender has its receiver on when not transmitting. 0, otherwise */
    bool mDeviceType : 1;   /**< 1, if the sender is an FTD. 0, otherwise */
    bool mNetworkData : 1;  /**< 1, if the sender requires the full Network Data. 0, otherwise */
} otLinkModeConfig;

/** Represents the IEEE 802.15.4 Extended Address */
typedef struct otExtAddress
{
    uint8_t m8[OT_EXT_ADDRESS_SIZE]; /**< IEEE 802.15.4 Extended Address bytes */
} otExtAddress;

/** Holds diagnostic information for a neighboring Thread node */
typedef struct
{
    otExtAddress mExtAddress;   /**< IEEE 802.15.4 Extended Address */
    uint32_t mAge;              /**< Seconds since last heard */
    uint32_t mConnectionTime;   /**< Seconds since link establishment (requires `CONFIG_UPTIME_ENABLE`) */
    uint16_t mRloc16;           /**< RLOC16 */
    uint32_t mLinkFrameCounter; /**< Link Frame Counter */
    uint32_t mMleFrameCounter;  /**< MLE Frame Counter */
    uint8_t mLinkQualityIn;     /**< Link Quality In */
    int8_t mAverageRssi;        /**< Average RSSI */
    int8_t mLastRssi;           /**< Last observed RSSI */
    uint8_t mLinkMargin;        /**< Link Margin */
    uint16_t mFrameErrorRate;   /**< Frame error rate (0xffff->100%). Requires error tracking feature. */
    uint16_t mMessageErrorRate; /**< (IPv6) msg error rate (0xffff->100%). Requires error tracking feature. */
    uint16_t mVersion;          /**< Thread version of the neighbor */
    bool mRxOnWhenIdle : 1;     /**< rx-on-when-idle */
    bool mFullThreadDevice : 1; /**< Full Thread Device */
    bool mFullNetworkData : 1;  /**< Full Network Data */
    bool mIsChild : 1;          /**< Is the neighbor a child */
} otNeighborInfo;

/** Represents the Thread Leader Data */
typedef struct otLeaderData
{
    uint32_t mPartitionId;      /**< Partition ID */
    uint8_t mWeighting;         /**< Leader Weight */
    uint8_t mDataVersion;       /**< Full Network Data Version */
    uint8_t mStableDataVersion; /**< Stable Network Data Version */
    uint8_t mLeaderRouterId;    /**< Leader Router ID */
} otLeaderData;

/** Holds diagnostic information for a Thread Router */
typedef struct
{
    otExtAddress mExtAddress;  /**< IEEE 802.15.4 Extended Address */
    uint16_t mRloc16;          /**< RLOC16 */
    uint8_t mRouterId;         /**< Router ID */
    uint8_t mNextHop;          /**< Next hop to router */
    uint8_t mPathCost;         /**< Path cost to router */
    uint8_t mLinkQualityIn;    /**< Link Quality In */
    uint8_t mLinkQualityOut;   /**< Link Quality Out */
    uint8_t mAge;              /**< Time last heard */
    bool mAllocated : 1;       /**< Router ID allocated or not */
    bool mLinkEstablished : 1; /**< Link established with Router ID or not */
    uint8_t mVersion;          /**< Thread version */

    /** Parent CSL parameters are only relevant when OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE is enabled */
    uint8_t mCslClockAccuracy; /**< CSL clock accuracy, in ± ppm */
    uint8_t mCslUncertainty;   /**< CSL uncertainty, in ±10 us */
} otRouterInfo;

/** Represents the IP level counters */
typedef struct otIpCounters
{
    uint32_t mTxSuccess; /**< The number of IPv6 packets successfully transmitted */
    uint32_t mRxSuccess; /**< The number of IPv6 packets successfully received */
    uint32_t mTxFailure; /**< The number of IPv6 packets failed to transmit */
    uint32_t mRxFailure; /**< The number of IPv6 packets failed to receive */
} otIpCounters;

/** Represents the Thread MLE counters */
typedef struct otMleCounters
{
    uint16_t mDisabledRole;                  /**< Number of times device entered OT_DEVICE_ROLE_DISABLED role */
    uint16_t mDetachedRole;                  /**< Number of times device entered OT_DEVICE_ROLE_DETACHED role */
    uint16_t mChildRole;                     /**< Number of times device entered OT_DEVICE_ROLE_CHILD role */
    uint16_t mRouterRole;                    /**< Number of times device entered OT_DEVICE_ROLE_ROUTER role */
    uint16_t mLeaderRole;                    /**< Number of times device entered OT_DEVICE_ROLE_LEADER role */
    uint16_t mAttachAttempts;                /**< Number of attach attempts while device was detached */
    uint16_t mPartitionIdChanges;            /**< Number of changes to partition ID */
    uint16_t mBetterPartitionAttachAttempts; /**< Number of attempts to attach to a better partition */

    /**
     * Role time tracking.
     *
     * When uptime feature is enabled (OPENTHREAD_CONFIG_UPTIME_ENABLE = 1) time spent in each MLE role is tracked.
     */
    uint64_t mDisabledTime; /**< Number of milliseconds device has been in OT_DEVICE_ROLE_DISABLED role */
    uint64_t mDetachedTime; /**< Number of milliseconds device has been in OT_DEVICE_ROLE_DETACHED role */
    uint64_t mChildTime;    /**< Number of milliseconds device has been in OT_DEVICE_ROLE_CHILD role */
    uint64_t mRouterTime;   /**< Number of milliseconds device has been in OT_DEVICE_ROLE_ROUTER role */
    uint64_t mLeaderTime;   /**< Number of milliseconds device has been in OT_DEVICE_ROLE_LEADER role */
    uint64_t mTrackedTime;  /**< Number of milliseconds tracked by previous counters */

    /**
     * Number of times device changed its parent.
     *
     * A parent change can happen if device detaches from its current parent and attaches to a different one, or even
     * while device is attached when the periodic parent search feature is enabled  (please see option
     * OPENTHREAD_CONFIG_PARENT_SEARCH_ENABLE).
     */
    uint16_t mParentChanges;
} otMleCounters;

/** Represents the MLE Parent Response data */
typedef struct otThreadParentResponseInfo
{
    otExtAddress mExtAddr; /**< IEEE 802.15.4 Extended Address of the Parent */
    uint16_t mRloc16;      /**< Short address of the Parent */
    int8_t mRssi;          /**< Rssi of the Parent */
    int8_t mPriority;      /**< Parent priority */
    uint8_t mLinkQuality3; /**< Parent Link Quality 3 */
    uint8_t mLinkQuality2; /**< Parent Link Quality 2 */
    uint8_t mLinkQuality1; /**< Parent Link Quality 1 */
    bool mIsAttached;      /**< Is the node receiving parent response attached */
} otThreadParentResponseInfo;

#endif /* OPENTHREAD_THREAD_H_INCLUDED */
