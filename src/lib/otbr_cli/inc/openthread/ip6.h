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

#ifndef OPENTHREAD_IP6_H_INCLUDED
#define OPENTHREAD_IP6_H_INCLUDED

#include <stdint.h>

/** Size of an IPv6 prefix (bytes) */
#define OT_IP6_PREFIX_SIZE 8
/** Size of an IPv6 Interface Identifier (bytes) */
#define OT_IP6_IID_SIZE 8
/** Size of an IPv6 address (bytes) */
#define OT_IP6_ADDRESS_SIZE 16

/** Represents the Interface Identifier of an IPv6 address */
typedef struct
{
    union __attribute__((__packed__))
    {
        uint8_t m8[OT_IP6_IID_SIZE];                      /**< 8-bit fields */
        uint16_t m16[OT_IP6_IID_SIZE / sizeof(uint16_t)]; /**< 16-bit fields */
        uint32_t m32[OT_IP6_IID_SIZE / sizeof(uint32_t)]; /**< 32-bit fields */
    };
} otIp6InterfaceIdentifier;

/** Represents the Network Prefix of an IPv6 address (most significant 64 bits of the address) */
typedef struct
{
    uint8_t m8[OT_IP6_PREFIX_SIZE]; /**< The Network Prefix */
} otIp6NetworkPrefix;

/** Represents the components of an IPv6 address */
typedef struct
{
    otIp6NetworkPrefix mNetworkPrefix; /**< The Network Prefix (most significant 64 bits of the address) */
    otIp6InterfaceIdentifier mIid;     /**< The Interface Identifier (least significant 64 bits of the address) */
} otIp6AddressComponents;

/** Represents an IPv6 address */
typedef struct
{
    union __attribute__((__packed__))
    {
        uint8_t m8[OT_IP6_ADDRESS_SIZE];                      /**< 8-bit fields */
        uint16_t m16[OT_IP6_ADDRESS_SIZE / sizeof(uint16_t)]; /**< 16-bit fields */
        uint32_t m32[OT_IP6_ADDRESS_SIZE / sizeof(uint32_t)]; /**< 32-bit fields */
        otIp6AddressComponents mComponents;                   /**< IPv6 address components */
    } mFields;
} otIp6Address;

#endif /* OPENTHREAD_IP6_H_INCLUDED */
