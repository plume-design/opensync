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

#ifndef HW_ACC_H_INCLUDED
#define HW_ACC_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

enum hw_acc_flush_action_e {
    HW_ACC_FLUSH_ACTION_ALL     = (1 << 0),
    HW_ACC_FLUSH_ACTION_DSTMAC  = (1 << 1),
    HW_ACC_FLUSH_ACTION_SRCMAC  = (1 << 2),
    HW_ACC_FLUSH_ACTION_DSTIP   = (1 << 3),
    HW_ACC_FLUSH_ACTION_SRCIP   = (1 << 4),
    HW_ACC_FLUSH_ACTION_DSTPORT = (1 << 5),
    HW_ACC_FLUSH_ACTION_SRCPORT = (1 << 6),
    HW_ACC_FLUSH_ACTION_PROTO   = (1 << 7),
    HW_ACC_FLUSH_ACTION_DEVID   = (1 << 8),

    //some common flus flags combos
    HW_ACC_FLUSH_ACTION_MAC     	= HW_ACC_FLUSH_ACTION_DSTMAC | HW_ACC_FLUSH_ACTION_SRCMAC,
    HW_ACC_FLUSH_ACTION_IP      	= HW_ACC_FLUSH_ACTION_DSTIP | HW_ACC_FLUSH_ACTION_SRCIP,
    HW_ACC_FLUSH_ACTION_PORT    	= HW_ACC_FLUSH_ACTION_DSTPORT | HW_ACC_FLUSH_ACTION_SRCPORT,
    HW_ACC_FLUSH_ACTION_IP_PORT 	= HW_ACC_FLUSH_ACTION_IP | HW_ACC_FLUSH_ACTION_PORT,
    HW_ACC_FLUSH_ACTION_4_TUPLE		= HW_ACC_FLUSH_ACTION_MAC | HW_ACC_FLUSH_ACTION_IP,
    HW_ACC_FLUSH_ACTION_5_TUPLE 	= HW_ACC_FLUSH_ACTION_IP_PORT | HW_ACC_FLUSH_ACTION_PROTO,
    HW_ACC_FLUSH_ACTION_7_TUPLE 	= HW_ACC_FLUSH_ACTION_5_TUPLE | HW_ACC_FLUSH_ACTION_MAC,
};

struct hw_acc_flush_flow_t{
    enum hw_acc_flush_action_e actions;
    int devid;

    uint8_t ip_version;
    uint8_t protocol;
    uint8_t src_mac[6];
    uint8_t dst_mac[6];
    uint8_t src_ip[16];
    uint8_t dst_ip[16];
    uint16_t src_port;
    uint16_t dst_port;
};

bool hw_acc_flush(struct hw_acc_flush_flow_t *flow);
bool hw_acc_flush_flow_per_device(int devid);
bool hw_acc_flush_flow_per_mac(const char *mac);
bool hw_acc_flush_all_flows();
void hw_acc_enable();
void hw_acc_disable();

/* Behind every flag there is an intention that the caller
 * expects to be satisfied as how the packet data-path should
 * look like.
 *
 * Target layer on the platform in use must translate
 * below flags to whatever AE configuration is needed.
 * This is necessary, because only platform can know its AE
 * capabilities and what mode is the best to choose in terms
 * of efficiency and fulfiling the caller needs.
 *
 * PASS flags are to ensure that certain hooks get hit:
 * - XDP, means that packets should reach XDP installed programs
 *      no matter whether they are attached directly on the netdev
 *      or in a generic hook.
 * - TC_INGRESS, means that packet should reach traffic control
 *      ingress hook and be subjected to policing or dropping.
 * - TC_EGRESS, means that packet should reach qdisc egress and
 *      be subjected to shaping, scheduling or dropping.
 *
 * DISABLE_ACCEL flag should have identical behaviour as original
 * hw_acc_disable() function to turn off flows acceleration. This
 * must be prioritized over any other PASS flag.
 *
 * Flags can be mixed, so eg. both XDP and TC_INGRESS could be
 * requested together, although be aware that this exact connection
 * will most likely result in ACC being disabled.
 *
 * Since this is slightly more advanced than regular on/off action,
 * in case of platform not being able to do what the caller requests,
 * we allow the platform to report the failure back in hw_acc_mode_set(..)
 */
enum hw_acc_ctrl_flags {
    HW_ACC_F_PASS_XDP          = 0x1,
    HW_ACC_F_PASS_TC_INGRESS   = 0x2,
    HW_ACC_F_PASS_TC_EGRESS    = 0x4,
    HW_ACC_F_DISABLE_ACCEL      = 0x8,
};

typedef uint32_t hw_acc_ctrl_flags_t;
bool hw_acc_mode_set(hw_acc_ctrl_flags_t flags);

#endif /* HW_ACC_H_INCLUDED */
