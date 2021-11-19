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

#ifndef TEST_GATEKEEPER_CACHE_H
#define TEST_GATEKEEPER_CACHE_H

#include <stddef.h>
#include <stdint.h>

extern const char *test_name;

#ifndef IP_STR_LEN
#define IP_STR_LEN          INET6_ADDRSTRLEN
#endif /* IP_STR_LEN */

extern size_t OVER_MAX_CACHE_ENTRIES;

struct sample_attribute_entries
{
    char mac_str[18];
    int attribute_type;                        /* request type */
    char attr_name[256];                           /* attribute name */
    uint64_t cache_ttl;   /* TTL value that should be set */
    uint8_t action;       /* action req when adding will be set when lookup is
                            performed */
};

struct sample_flow_entries
{
    char mac_str[18];
    char src_ip_addr[IP_STR_LEN];     /* src ip in Network byte order */
    char dst_ip_addr[IP_STR_LEN];     /* dst ip in Network byte order */
    uint8_t ip_version;       /* ipv4 (4), ipv6 (6) */
    uint16_t src_port;        /* source port value */
    uint16_t dst_port;        /* dst port value */
    uint8_t protocol;         /* protocol value  */
    uint8_t direction;        /* used to check inbound/outbound cache */
    uint64_t cache_ttl;       /* TTL value that should be set */
    uint8_t action;           /* action req when adding will be set when lookup is
                                 performed */
    uint64_t hit_counter;     /* will be updated when lookup is performed */
};

extern struct sample_attribute_entries *test_attr_entries;
extern struct sample_flow_entries *test_flow_entries;

extern struct gk_attr_cache_interface *entry1, *entry2, *entry3, *entry4, *entry5;
extern struct gkc_ip_flow_interface *flow_entry1, *flow_entry2, *flow_entry3,
    *flow_entry4, *flow_entry5;

void free_flow_interface(struct gkc_ip_flow_interface *entry);

void run_gk_cache(void);
void run_gk_cache_flush(void);
void run_gk_cache_cmp(void);

#endif /* TEST_GATEKEEPER_CACHE_H */
