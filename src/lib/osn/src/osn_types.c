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

#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include "osn_types.h"
#include "util.h"

static int bitcmp(const void *a, const void *b, size_t nbits);

/*
 * ===========================================================================
 *  IPv4 functions
 * ===========================================================================
 */

/*
 * Convert to string and write to @p buf an osn_ip_addr structure
 */
char *__FMT_osn_ip_addr(char *buf, size_t sz, const osn_ip_addr_t *addr)
{
    size_t len;

    inet_ntop(AF_INET, &addr->ia_addr, buf, sz);

    /* Add prefix, if any */
    if (addr->ia_prefix >= 0)
    {
        len = strlen(buf);
        snprintf(buf + len, sz - len, "/%d", addr->ia_prefix);
    }

    return buf;
}


/*
 * Create an osn_ip_addr_t structure from string. The accepted string is in the following format:
 *
 * IPV4_ADDR/PREFIX_LENGTH
 *
 *      - IPV4_ADDR is a string that is accepted by inet_pton()
 *      - PREFIX_LENGTH is a positive integer representing the netmask
 */
bool osn_ip_addr_from_str(osn_ip_addr_t *out, const char *str)
{
    char buf[OSN_IP6_ADDR_LEN];
    char *pbuf;
    char *paddr;

    if (strlen(str) >= sizeof(buf)) return false;

    STRSCPY(buf, str);

    /* Split the string further by using the '.' separator (extract the prefix) */
    pbuf = buf;
    paddr = strsep(&pbuf, "/");

    /* Parse the IPv4 address */
    if (inet_pton(AF_INET, paddr, &out->ia_addr) != 1) return false;

    out->ia_prefix = -1;
    if (pbuf != NULL)
    {
        out->ia_prefix = strtol(pbuf, NULL, 0);
        if (out->ia_prefix < 0 || out->ia_prefix > 32) return false;
    }

    return true;
}

/*
 * Create a osn_ip_addr_t structure from a in_addr structure. in_addr is
 * commonly used hidden inside sockaddr
 */
bool osn_ip_addr_from_in_addr(osn_ip_addr_t *out, const struct in_addr *in)
{
    *out = OSN_IP_ADDR_INIT;
    memcpy(&out->ia_addr, in, sizeof(out->ia_addr));

    return true;
}

/*
 * Create a osn_ip_addr_t structure from a sockaddr structure.
 */
bool osn_ip_addr_from_sockaddr(osn_ip_addr_t *out, const struct sockaddr *in)
{
    const struct sockaddr_in *sin = (const struct sockaddr_in *)in;

    if (sin->sin_family != AF_INET)
    {
        return false;
    }

    return osn_ip_addr_from_in_addr(out, &sin->sin_addr);
}

/*
 * Comparator function -- compare two osn_ip_addr_t and return negative, 0 or positive if
 * a is lower than, equal to or greater than b, respectively.
 */
int osn_ip_addr_cmp(void *_a, void *_b)
{
    osn_ip_addr_t *a = _a;
    osn_ip_addr_t *b = _b;

    int rc;

    rc = memcmp(&a->ia_addr, &b->ia_addr, sizeof(a->ia_addr));
    if (rc != 0) return rc;

    rc = a->ia_prefix - b->ia_prefix;
    if (rc != 0) return rc;

    return rc;
}

/*
 * Compute prefix from a netmask address. For example, 255.255.0.0 -> /16
 */
int osn_ip_addr_to_prefix(osn_ip_addr_t *addr)
{
    uint32_t ii;

    uint32_t laddr = ntohl(addr->ia_addr.s_addr);

    for (ii = 0; ii < 32 ; ii++)
    {
        if ((laddr & (1 << (31 - ii))) == 0) break;
    }

    return ii;
}

/*
 * Compute netmask address from prefix. For example 16 -> 255.255.0.0
 */
osn_ip_addr_t osn_ip_addr_from_prefix(int prefix)
{
    osn_ip_addr_t out = OSN_IP_ADDR_INIT;

    out.ia_addr.s_addr = htonl(~(0xFFFFFFFFu >> prefix));

    return out;
}

/*
 * Calculate the subnet from an IP/mask pair. For example, 192.168.40.1/24 -> 192.168.40.0/24
 */
osn_ip_addr_t osn_ip_addr_subnet(osn_ip_addr_t *addr)
{
    uint32_t mask;

    osn_ip_addr_t subnet = *addr;

    /* If no prefix is present, assume it's /32 */
    if (addr->ia_prefix < 0 || addr->ia_prefix > 31)
    {
        mask = 0xFFFFFFFF;
    }
    else
    {
        mask = ~(0xFFFFFFFFu >> addr->ia_prefix);
    }

    subnet.ia_addr.s_addr = subnet.ia_addr.s_addr & htonl(mask);

    return subnet;
}

osn_ip_addr_t osn_ip_addr_to_bcast(osn_ip_addr_t *addr)
{
    uint32_t mask;

    osn_ip_addr_t out = *addr;

    /* If no prefix is present, assume it's /32 */
    if (addr->ia_prefix < 0 || addr->ia_prefix > 31)
    {
        mask = 0x0;
    }
    else
    {
        mask = 0xFFFFFFFFu >> addr->ia_prefix;
    }

    out.ia_addr.s_addr = out.ia_addr.s_addr | htonl(mask);
    out.ia_prefix = -1;

    return out;
}

/*
 * ===========================================================================
 *  IPv6 functions
 * ===========================================================================
 */

/*
 * Convert to string and write to @p buf an osn_ip6_addr structure
 */
char *__FMT_osn_ip6_addr(char *buf, size_t sz, const osn_ip6_addr_t *addr)
{
    size_t len;

    inet_ntop(AF_INET6, &addr->ia6_addr, buf, sz);

    /* Add prefix, if any */
    if (addr->ia6_prefix >= 0)
    {
        len = strlen(buf);
        snprintf(buf + len, sz - len, "/%d", addr->ia6_prefix);
    }

    /* -1 is infinite lifetime */
    if (addr->ia6_pref_lft >= -1)
    {
        len = strlen(buf);
        snprintf(buf + len, sz - len, ",%d", addr->ia6_pref_lft);
    }

    /* -1 is infinite lifetime */
    if (addr->ia6_valid_lft >= -1)
    {
        len = strlen(buf);
        snprintf(buf + len, sz - len, ",%d", addr->ia6_valid_lft);
    }

    return buf;
}

/*
 * Create an osn_ip6_addr_t structure from string. The accepted string is in the following format:
 *
 * IPV6_ADDR/PREFIX_LENGTH,PREFERRED_LFT,VALID_LFT
 *
 *      - IPV6_ADDR is a string that is accepted by inet_pton()
 *      - PREFIX_LENGTH is a positive integer representing the prefix length
 *      - PREFERRED_LFT and VALID_LFT are the valid lifetime in seconds, or -1 if the lifetime is infinite
 */
bool osn_ip6_addr_from_str(osn_ip6_addr_t *out, const char *str)
{
    char buf[OSN_IP6_ADDR_LEN];
    char *pbuf;
    char *paddr;
    char *pprefix;
    char *ppref;
    char *pvalid;

    if (strlen(str) >= sizeof(buf)) return false;

    STRSCPY(buf, str);

    pbuf = buf;

    /* Split first by , (extract lifetimes) */
    paddr = strsep(&pbuf, ",");
    ppref = strsep(&pbuf, ",");
    pvalid = strsep(&pbuf, ",");

    /* Split the string further by using the '.' separator (extract the prefix) */
    pbuf = paddr;
    paddr = strsep(&pbuf, "/");
    pprefix = strsep(&pbuf, "/");

    /* Parse the IPv6 address */
    if (inet_pton(AF_INET6, paddr, &out->ia6_addr) != 1) return false;

    out->ia6_prefix = -1;
    if (pprefix != NULL)
    {
        out->ia6_prefix = strtol(pprefix, NULL, 0);
        if (out->ia6_prefix < 0 || out->ia6_prefix > 128) return false;
    }

    out->ia6_pref_lft = INT_MIN;
    if (ppref != NULL)
    {
        out->ia6_pref_lft = strtol(ppref, NULL, 0);
    }

    out->ia6_valid_lft = INT_MIN;
    if (pvalid != NULL)
    {
        out->ia6_valid_lft = strtol(pvalid, NULL, 0);
    }

    return true;
}

/*
 * Comparator function -- compare two osn_ip6_addr_t and return negative, 0 or positive if
 * a is lower than, equal to or greater than b, respectively.
 */
int osn_ip6_addr_cmp(void *_a, void *_b)
{
    osn_ip6_addr_t *a = _a;
    osn_ip6_addr_t *b = _b;

    int rc;

    rc = memcmp(&a->ia6_addr, &b->ia6_addr, sizeof(a->ia6_addr));
    if (rc != 0) return rc;

    rc = a->ia6_prefix - b->ia6_prefix;
    if (rc != 0) return rc;

    rc = a->ia6_pref_lft - b->ia6_pref_lft;
    if (rc != 0) return rc;

    rc = b->ia6_valid_lft - b->ia6_valid_lft;

    return rc;
}

/*
 * Comparator function -- compare two osn_ip6_addr_t and return negative, 0 or positive if
 * a is lower than, equal to or greater than b, respectively.
 *
 * This version ignores lifetime values.
 */
int osn_ip6_addr_nolft_cmp(void *_a, void *_b)
{
    osn_ip6_addr_t *a = _a;
    osn_ip6_addr_t *b = _b;

    int rc;

    rc = memcmp(&a->ia6_addr, &b->ia6_addr, sizeof(a->ia6_addr));
    if (rc != 0) return rc;

    rc = a->ia6_prefix - b->ia6_prefix;
    if (rc != 0) return rc;

    return rc;
}

/**
 * Return true if @p addr is part of subnet @p subnet
 */
bool osn_ip6_addr_is_subnet(osn_ip6_addr_t *addr, osn_ip6_addr_t *subnet)
{
    int addr_prefix = addr->ia6_prefix;
    int subnet_prefix = subnet->ia6_prefix;

    if (addr_prefix == -1) addr_prefix = 128;
    if (subnet_prefix == -1) subnet_prefix = 128;

    /* The address prefix is greater than subnet */
    if (addr_prefix < subnet_prefix) return false;

    return bitcmp(&addr->ia6_addr.s6_addr, &subnet->ia6_addr.s6_addr, subnet->ia6_prefix) == 0;
}

/*
 * Parse the @p ip6 address and return osn_ip6_addr_type enum representing
 * its type
 */
enum osn_ip6_addr_type osn_ip6_addr_type(osn_ip6_addr_t *ip6)
{
    osn_ip6_addr_t addr;

    osn_ip6_addr_from_str(&addr, "::1/128");
    if (osn_ip6_addr_cmp(ip6, &addr) == 0)
    {
        return OSN_IP6_ADDR_LOOPBACK;
    }

    osn_ip6_addr_from_str(&addr, "fe80::/10");
    if (osn_ip6_addr_is_subnet(ip6, &addr))
    {
        return OSN_IP6_ADDR_LOCAL_LINK;
    }

    osn_ip6_addr_from_str(&addr, "fc00::/7");
    if (osn_ip6_addr_is_subnet(ip6, &addr))
    {
        return OSN_IP6_ADDR_LOCAL_UNIQUE;
    }

    osn_ip6_addr_from_str(&addr, "ff::/8");
    if (osn_ip6_addr_is_subnet(ip6, &addr))
    {
        return OSN_IP6_ADDR_MULTICAST;
    }

    osn_ip6_addr_from_str(&addr, "2000::/3");
    if (osn_ip6_addr_is_subnet(ip6, &addr))
    {
        return OSN_IP6_ADDR_GLOBAL;
    }

    return OSN_IP6_ADDR_INVALID;
}


/*
 * ===========================================================================
 *  Other types
 * ===========================================================================
 */

/*
 * Create an osn_mac_addr_t structure from string. The accepted string is in the following format:
 *
 * XX:XX:XX:XX:XX:XX or XX-XX-XX-XX-XX-XX
 */
bool osn_mac_addr_from_str(osn_mac_addr_t *mac, const char *str)
{
    char pstr[64];
    char *mstr;
    char *mtok;
    char *pend;
    long cnum;
    int  cmak;

    if (strlen(str) >= sizeof(pstr)) return false;
    STRSCPY(pstr, str);

    mstr = pstr;
    cmak = 0;
    while ((mtok = strsep(&mstr, ":-")) != NULL)
    {
        cnum = strtoul(mtok, &pend, 16);

        /* Extraneous characters in string, return error */
        if (*pend != '\0') return false;

        /* Check if the parsed number is between 0 and 255 */
        if (cnum >= 256)
        {
            return false;
        }

        /* Check if we have more than 6 bytes */
        if (cmak >= (int)(sizeof(mac->ma_addr) / sizeof(mac->ma_addr[0])))
        {
            return false;
        }

        mac->ma_addr[cmak++] = cnum;
    }

    return true;
}

/*
 * Comparator function -- compare two osn_mac_addr_t and return negative, 0 or positive if
 * a is lower than, equal to or greater than b, respectively.
 */
int osn_mac_addr_cmp(void *_a, void *_b)
{
    osn_mac_addr_t *a = _a;
    osn_mac_addr_t *b = _b;

    return memcmp(a->ma_addr, b->ma_addr, sizeof(a->ma_addr));
}

/*
 * ===========================================================================
 *  MISC functions
 * ===========================================================================
 */

/*
 * Similar to memcmp(), but compares up to @p nbits bits of the two memory
 * locations
 */
static int bitcmp(const void *_a, const void *_b, const size_t nbits)
{
    size_t ii;
    uint8_t mask;

    const uint8_t *a = _a;
    const uint8_t *b = _b;

    for (ii = 0; (ii + 8) < nbits; ii += 8)
    {
        if (*a != *b) return *a - *b;
        a++;
        b++;
    }

    /* Calculate the mask that should be used to compare the last (partial) byte */
    mask = 0xff << ((ii - nbits) & 7);
    if ((*a & mask) != (*b & mask))
    {
        return (*a & mask) - (*b & mask);
    }

    return 0;
}
