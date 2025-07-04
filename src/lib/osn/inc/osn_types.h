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

#ifndef OSN_TYPES_H_INCLUDED
#define OSN_TYPES_H_INCLUDED

#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

/**
 * @file osn_types.h
 * @brief OpenSync Networking Common Types
 *
 * @defgroup OSN OpenSync Networking
 *
 * OpenSync Networking APIs
 *
 * @{
 */

/*
 * ===========================================================================
 *  Support functions for various types in OpenSync networking
 *  - string conversion functions
 *  - comparators
 *  - misc
 * ===========================================================================
 */

/**
 * @defgroup OSN_COMMON Common Networking Types and Utilities
 *
 * Common OpenSync networking types and utilities
 *
 * @{
 */

/*
 * ===========================================================================
 *  IPv4 types
 * ===========================================================================
 */

/**
 * @defgroup OSN_COMMON_osn_ip_addr_t osn_ip_addr_t
 *
 * IPv4 Address types and associated functions.
 *
 * @{
 */

/**
 * IPv4 address definition; this includes the netmask (prefix).
 *
 * A negative prefix indicates that the prefix is not present. Note that a
 * prefix of 0 is valid (for example, the default route).
 *
 * This structure should not be accessed directly.
 *
 * Use @ref OSN_IP_ADDR_INIT to initialize this structure.
 */
typedef struct osn_ip_addr
{
    struct in_addr      ia_addr;        /**< IPv4 Address */
    int                 ia_prefix;      /**< Netmask in /XX notation */
} osn_ip_addr_t;

/**
 * Initializer for an IPv4 address structure (@ref osn_ip_addr_t)
 */
#define OSN_IP_ADDR_INIT (osn_ip_addr_t)    \
{                                           \
    .ia_prefix = -1,                        \
}

/**
 * Maximum length of a IPv4 Address structure when expressed as a string,
 * including the terminating \0
 */
#define OSN_IP_ADDR_LEN     sizeof("255.255.255.255/32")

/**
 * Macro helpers for printf() formatting. The PRI_ macro can be used in
 * conjunction with the FMT_ macro to print IPv4 addresses.
 *
 * Examples:
 *
 * @code
 * osn_ip_addr_t my_ipaddr;
 *
 * printf("Hello. The IP address is: "PRI_osn_ip_addr"\n", FMT_osn_ip_addr(my_ipaddr));
 * @endcode
 */
#define PRI_osn_ip_addr         "%s"

/**
 * Macro helper for printf() formatting. See @ref PRI_osn_ip_addr for more
 * info.
 */
#define FMT_osn_ip_addr(x)      (__FMT_osn_ip_addr((char[OSN_IP_ADDR_LEN]){0}, OSN_IP_ADDR_LEN, &(x)))
char* __FMT_osn_ip_addr(char *buf, size_t sz, const osn_ip_addr_t *addr);
/**< @copydoc FMT_osn_ip_addr */

/**
 * Initialize an osn_ip_addr_t from a string. Valid string formats are:
 *
 * "NN.NN.NN.NN"
 *
 * or
 *
 * "NN.NN.NN.NN/NN"
 *
 * @param[in]   out  Output osn_ip_addr_t structure
 * @param[in]   str  Input string
 *
 * @return
 * This function returns true if @p str is valid and was successfully parsed,
 * false otherwise. If false is returned, @p out should be considered invalid.
 */
bool osn_ip_addr_from_str(osn_ip_addr_t *out, const char *str);

/**
 * Initialize a osn_ip_addr_t structure from a in_addr structure. in_addr is
 * commonly used hidden inside struct sockaddr
 */
bool osn_ip_addr_from_in_addr(osn_ip_addr_t *out, const struct in_addr *in);

/**
 * Initialize a son_ip_addr_t structure from a sockaddr structure.
 */
bool osn_ip_addr_from_sockaddr(osn_ip_addr_t *out, const struct sockaddr *in);

/**
 * Comparator for @ref osn_ip_addr_t structures.
 *
 * @param[in]   a  First osn_ip_addr_t to compare
 * @param[in]   b  Second osn_ip_addr_t to compare
 *
 * @return
 * This function returns an integer less than, equal to, or greater than zero
 * if @p a is found, respectively, to be less than, to match, or be
 * greater than @p b.
 */
int osn_ip_addr_cmp(const void *a, const void *b);

/**
 * Strip the non-subnet part of an IP address. For example:
 *
 * @code
 * 192.168.40.1/24 -> 192.168.40.0/24
 * @endcode
 *
 * @param[in]   addr  Address to convert
 *
 * @return
 * Returns an osn_ip_addr_t structure that has its non-subnet part set to all
 * zeroes
 */
osn_ip_addr_t osn_ip_addr_subnet(osn_ip_addr_t *addr);

/**
 * Calculate a broadcast address from the given address in @p addr
 *
 * @code
 * 192.168.40.1/24 -> 192.168.40.255
 * @endcode
 *
 * @param[in]   addr  Address to convert
 *
 * @return
 * This function returns a valid broadcast address with the prefix part removed
 */
osn_ip_addr_t osn_ip_addr_to_bcast(osn_ip_addr_t *addr);

/**
 * Converts a subnet IP representation to a prefix integer.
 * For example:
 *
 * @code
 * 255.255.255.0 -> 24
 * @endcode
 *
 * @param[in]   addr  Input address
 *
 * @return
 * Returns the number of consecutive bits set in @p addr
 */
int osn_ip_addr_to_prefix(osn_ip_addr_t *addr);

/**
 * Convert a prefix integer to an IP representation.
 * For example:
 *
 * @code
 * 24 -> 255.255.255.0
 * @endcode
 *
 * @param[in]   prefix  Prefix to convert
 *
 * @return
 * This function returns an osn_ip_addr_t structure representing the prefix
 */
osn_ip_addr_t osn_ip_addr_from_prefix(int prefix);

/** @} OSN_COMMON_osn_ip_addr_t */

/*
 * ===========================================================================
 *  IPv6 types
 * ===========================================================================
 */

/**
 * @defgroup OSN_COMMON_osn_ip6_addr_t osn_ip6_addr_t
 *
 * IPv6 Address types and associated functions.
 *
 * @{
 */

/**
 * IPv6 Address definition; this includes the prefix and lifetimes.
 *
 * If the prefix is -1, it should be considered not present.
 *
 * If a lifetime is set to INT_MIN, it should be considered absent, while
 * a value of -1 means infinite.
 *
 * Use OSN_IP6_ADDR_INIT to initialize this structure to default values.
 */
typedef struct osn_ip6_addr
{
    struct in6_addr     ia6_addr;           /**< Global IP address */
    int                 ia6_prefix;         /**< IP prefix -- usually 64 */
    int                 ia6_pref_lft;       /**< Preferred lifetime in seconds (INT_MIN means not set) */
    int                 ia6_valid_lft;      /**< Valid lifetime in seconds (INT_MIN means not set) */
} osn_ip6_addr_t;

/**
 * Initializer for an IPv6 address structure (@ref osn_ip6_addr_t)
 */
#define OSN_IP6_ADDR_INIT (osn_ip6_addr_t)  \
{                                           \
    .ia6_prefix = -1,                       \
    .ia6_pref_lft = INT_MIN,                \
    .ia6_valid_lft = INT_MIN,               \
}

/**
 * Maximum length of IPv6 Address structure when expressed as a string,
 * including the terminating \0
 */
#define OSN_IP6_ADDR_LEN sizeof("1111:2222:3333:4444:5555:6666:7777:8888/128,2147483648,2147483648")

/**
 * Macro helpers for printf() formatting. The PRI_ macro can be used in
 * conjunction with the FMT_ macro to print IPv6 addresses.
 *
 * Examples:
 *
 * @code
 * osn_ip6_addr_t my_ip6addr;
 *
 * printf("Hello. The IPv6 address is: "PRI_osn_ip6_addr"\n", FMT_osn_ip6_addr(my_ipaddr));
 * @endcode
 */
#define PRI_osn_ip6_addr        "%s"

/**
 * Macro helper for printf() formatting. See @ref PRI_osn_ip6_addr for more
 * info.
 */
#define FMT_osn_ip6_addr(x)     (__FMT_osn_ip6_addr((char[OSN_IP6_ADDR_LEN]){0}, OSN_IP6_ADDR_LEN, &x))
char* __FMT_osn_ip6_addr(char *buf, size_t sz, const osn_ip6_addr_t *addr);
#define FMT_osn_ip6_addr_nolft(x) (__FMT_osn_ip6_addr_nolft((char[OSN_IP6_ADDR_LEN]){0}, OSN_IP6_ADDR_LEN, &x))
char* __FMT_osn_ip6_addr_nolft(char *buf, size_t sz, const osn_ip6_addr_t *addr);
/**< @copydoc FMT_osn_ip6_addr */

/**
 * IPv6 address type. Use in conjunction with @ref osn_ip6_addr_type() to
 * determine the address type.
 */
enum osn_ip6_addr_type
{
    OSN_IP6_ADDR_INVALID,               /* Invalid or unknown address type */
    OSN_IP6_ADDR_LOOPBACK,              /* The loopback address, "::1/128" */
    OSN_IP6_ADDR_LOCAL_LINK,            /* Link-local address */
    OSN_IP6_ADDR_LOCAL_UNIQUE,          /* Unique local address */
    OSN_IP6_ADDR_GLOBAL,                /* Global address */
    OSN_IP6_ADDR_MULTICAST,             /* Multicast address */
};

/**
 * Initialize an osn_ip6_addr_t from a string. Valid string formats are:
 *
 * IPV6_ADDR/PREFIX,MIN_LFT,MAX_LFT
 *
 * - IPV6_ADDR - Anything that inet_pton(AF_INET6, ...) can understand
 * - PREFIX    - An integer between 1 and 64 bits, a value of -1 means that
 *               the prefix is not present
 * - MIN_LFT   - An integer representing the minimum lifetime in seconds
 * - MAX_LFT   - An integer representing the maximum lifetime in seconds
 *
 * A value of -1 for MIN_LFT and MAX_LFT means infinite lifetime.
 * A value of INT_MIN for MIN_LFT and MAX_LFT means that the lifetime is not
 * present.
 *
 * @param[in]   out  Output osn_ip6_addr_t structure
 * @param[in]   str  Input string
 *
 * @return
 * This function returns true if @p str is valid and was successfully parsed,
 * false otherwise. If false is returned, @p out should be considered invalid.
 */
bool osn_ip6_addr_from_str(osn_ip6_addr_t *out, const char *str);

/**
 * Comparator for @ref osn_ip6_addr_t structures.
 *
 * @param[in]   a  First osn_ip6_addr_t to compare
 * @param[in]   b  Second osn_ip6_addr_t to compare
 *
 * @return
 * This function returns an integer less than, equal to, or greater than zero
 * if @p a is found, respectively, to be less than, to match, or be
 * greater than @p b.
 */
int osn_ip6_addr_cmp(const void *a, const void *b);

/**
 * Comparator for @ref osn_ip6_addr_t structures. This version ignores the
 * IPv6 address lifetimes.
 *
 * @param[in]   _a  First osn_ip6_addr_t to compare
 * @param[in]   _b  Second osn_ip6_addr_t to compare
 *
 * @return
 * This function returns an integer less than, equal to, or greater than zero
 * if @p a is found, respectively, to be less than, to match, or be
 * greater than @p b.
 */
int osn_ip6_addr_nolft_cmp(const void *_a, const void *_b);

/**
 * Strip the non-subnet part of an IPv6 address. For example:
 *
 * 2001:db8:1704:9902:7e13:1dff:fea8:924c/64
 * --> 2001:db8:1704:9902::/64
 *
 * @param[in]    addr   IPv6 address with prefix set.
 *
 * @return
 * Returns an osn_ip6_addr_t structure that has its non-subnet part
 * set to all zeroes i.e. the IPv6 prefix of the provided address.
 */
osn_ip6_addr_t osn_ip6_addr_subnet(const osn_ip6_addr_t *addr);

/**
 * Converts a subnet IPv6 representation to a prefix integer.
 * For example:
 *
 * @code
 * ffff:: -> 16
 * @endcode
 *
 * @param[in]   addr  Input address
 *
 * @return
 * Returns the number of consecutive bits set in @p addr
 */
int osn_ip6_addr_to_prefix(osn_ip6_addr_t *addr);

/**
 * Convert a prefix integer to an IPv6 representation.
 * For example:
 *
 * @code
 * 16 -> ffff:0000:0000:0000:0000:0000:0000:0000
 * @endcode
 *
 * @param[in]   prefix  Prefix to convert
 *
 * @return
 * This function returns an osn_ip_addr_t structure representing the prefix
 */
osn_ip6_addr_t osn_ip6_addr_from_prefix(int prefix);

/**
 * Detect the IPv6 address type.
 *
 * @param[in]   ip6 IPv6 address
 *
 * @return
 * This function returns an osn_ipv6_addr_type enum value which corresponds to the
 * detected IPv6 interface type.
 */
enum osn_ip6_addr_type osn_ip6_addr_type(osn_ip6_addr_t *ip6);

/**< @copydoc FMT_osn_ip6_addr */

/** @} OSN_COMMON_osn_ip6_addr_t */

/**
 * @defgroup OSN_COMMON_osn_ipany_addr_t osn_ipany_addr_t
 *
 * ipany (IPv4 or IPv6) address utility functions
 *
 * @{
 */

/**
 * IPv4 or IPv6 address.
 */
typedef struct osn_ipany_addr
{
    int addr_type;            /** AF_INET or AF_INET6 */
    union
    {
        osn_ip_addr_t  ip4;   /** IPv4 address */
        osn_ip6_addr_t ip6;   /** IPv6 address */
    } addr;
} osn_ipany_addr_t;

/**
 * Maximum length of an osn_ipany_addr_t address structure when expressed as
 * a string, including the terminating \0
 */
#define OSN_IPANY_ADDR_LEN    OSN_IP6_ADDR_LEN

/**
 * Comparator for @ref osn_ipany_addr_t structures.
 *
 * @param[in]   a  First osn_ipany_addr_t to compare
 * @param[in]   b  Second osn_ipany_addr_t to compare
 *
 * @return
 * This function returns an integer less than, equal to, or greater than zero
 * if @p a is found, respectively, to be less than, to match, or be
 * greater than @p b.
 */
int osn_ipany_addr_cmp(const void *_a, const void *_b);

/**
 * Check if ipany address is valid (ipv4 or ipv6) and not zero (0.0.0.0 or ::)
 *
 * @param[in]      osn_ipany_addr_t to check
 *
 * @return
 * This function returns true if the address is valid (addr_type is AF_INET or AF_INET6)
 * and not zero value (0.0.0.0 ipv4 or :: ipv6)
 */
bool osn_ipany_addr_is_set(const osn_ipany_addr_t *addr);

/**
 * Initialize an osn_ipany_addr_t from a string.
 *
 * This function first attempts to parse an IPv4 address string. If it
 * succeeds, it sets the osn_ipany_addr_t->addr_type to AF_INET and returns true.
 *
 * Otherwise it then attempts to parse an IPv6 address string. If it succeeds,
 * it sets the osn_ipany_addr_t->addr_type to AF_INET6 and returns true.
 *
 * Otherwise it returns false.
 *
 * See @ref osn_ip_addr_from_str() and @ref osn_ip6_addr_from_str() for
 * documentation on what counts as a valid IPv4 or IPv6 address string.
 *
 * @param[out]   out  Output osn_ipany_addr_t structure
 * @param[in]    str  Input string
 *
 * @return true on success
 */
bool osn_ipany_addr_from_str(osn_ipany_addr_t *out, const char *str);

/**
 * Macro helpers for printf() formatting. The PRI_ macro can be used in
 * conjunction with the FMT_ macro to print osn_ipany_addr_t (IPv4|IPv6 addresses).
 *
 * Examples:
 *
 * @code
 * osn_ipany_addr_t my_ipaddr;
 *
 * printf("Hello. The IP address is: "PRI_osn_ipany_addr"\n", FMT_osn_ipany_addr(my_ipaddr));
 * @endcode
 */
#define PRI_osn_ipany_addr        "%s"

/**
 * Macro helper for printf() formatting. See @ref PRI_osn_ipany_addr for more
 * info.
 */
#define FMT_osn_ipany_addr(x)     (__FMT_osn_ipany_addr((char[OSN_IPANY_ADDR_LEN]){0}, OSN_IPANY_ADDR_LEN, &x))
char* __FMT_osn_ipany_addr(char *buf, size_t sz, const osn_ipany_addr_t *addr);

/** @} OSN_COMMON_osn_ipany_addr_t */

/*
 * ===========================================================================
 *  Other types
 * ===========================================================================
 */

/**
 * @defgroup OSN_COMMON_osn_mac_addr_t osn_mac_addr_t
 *
 * Hardware Address (MAC) types and associated functions.
 *
 * @{
 */

/**
 * MAC address definition. It is advisable that this structure
 * is never used directly but through osn_mac_addr_* functions.
 */
typedef struct osn_mac_addr
{
    uint8_t             ma_addr[6];  /**< Raw MAC address bytes */
} osn_mac_addr_t;

/**
 * Maximum length of MAC Address structure when expressed as a string,
 * including the terminating \0
 *
 * This structure should be initialized with @ref OSN_MAC_ADDR_INIT before use.
 */
#define OSN_MAC_ADDR_LEN sizeof("11:22:33:44:55:66")

/**
 * Initializer for a MAC address structure (@ref osn_mac_addr_t)
 */
#define OSN_MAC_ADDR_INIT (osn_mac_addr_t){ .ma_addr = { 0 }, }

/**
 * Macro helpers for printf() formatting. The PRI_ macro can be used in
 * conjunction with the FMT_ macro to print MAC addresses.
 *
 * Examples:
 *
 * @code
 * osn_mac_addr_t my_hwaddr;
 *
 * printf("Hello. The MAC address is: "PRI_osn_mac_addr"\n", FMT_osn_mac_addr(my_macaddr));
 * @endcode
 */
#define PRI_osn_mac_addr        "%02x:%02x:%02x:%02x:%02x:%02x"

/**
 * Macro helper for printf() formatting. See @ref PRI_osn_mac_addr for more
 * info.
 */
#define FMT_osn_mac_addr(x)     (x).ma_addr[0], \
                                (x).ma_addr[1], \
                                (x).ma_addr[2], \
                                (x).ma_addr[3], \
                                (x).ma_addr[4], \
                                (x).ma_addr[5]

/**
 * Initialize an osn_mac_addr_t from a string. Valid string formats are:
 *
 * "XX:XX:XX:XX:XX:XX"
 *
 * @param[in]   out  Output osn_mac_addr_t structure
 * @param[in]   str  Input string
 *
 * @return
 * This function returns true if @p str is valid and was successfully parsed,
 * false otherwise. If false is returned, @p out should be considered invalid.
 */
bool osn_mac_addr_from_str(osn_mac_addr_t *out, const char *str);

/**
 * Comparator for @ref osn_mac_addr_t structures.
 *
 * @param[in]   _a First osn_mac_addr_t to compare
 * @param[in]   _b Second osn_mac_addr_t to compare
 *
 * @return
 * This function returns an integer less than, equal to, or greater than zero
 * if @p a is found, respectively, to be less than, to match, or be
 * greater than @p b.
 */
int osn_mac_addr_cmp(const void *_a, const void *_b);

/** @} OSN_COMMON_osn_mac_addr_t */

/**
 * @defgroup OSN_COMMON_osn_duplex_mode_t osn_duplex_mode_t
 *
 * Link duplex mode types and associated defines.
 *
 * @{
 */

/**
 * Link duplex mode type. Used for reporting the negotiated duplex mode of a link.
 */
typedef enum osn_duplex
{
    OSN_DUPLEX_FULL,
    OSN_DUPLEX_HALF,
    OSN_DUPLEX_UNKNOWN,
} osn_duplex_t;

#define OSN_NETIF_DUPLEX_INIT (OSN_DUPLEX_UNKNOWN)
#define OSN_NETIF_SPEED_INIT (0)

/** @} OSN_COMMON_osn_duplex_mode_t */

/** @} OSN_COMMON */

/** @} OSN */

#endif /* OSN_TYPES_H_INCLUDED */
