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

#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "fsm_policy.h"
#include "gatekeeper_cache_cmp.h"
#include "util.h"
#include "sockaddr_storage.h"

/*
 * MAC address comparison
 *
 * Second parameter is one of the MAC addresses present in the str_set
 * of the rule. This will require extra work to be performed by the caller
 * for IN/OUT.
 */
bool
mac_cmp(os_macaddr_t *a, os_macaddr_t *b)
{
    return (memcmp(a, b, sizeof(*a)) == 0);
}

bool
mac_cmp_true(os_macaddr_t *a, os_macaddr_t *b)
{
    (void)a;
    (void)b;
    return true;
}

mac_comparator
get_mac_cmp(int mac_op)
{
    switch (mac_op)
    {
        case MAC_OP_IN:
        case MAC_OP_OUT:
            return mac_cmp;
    }
    return mac_cmp_true;
}

/*
 * risk comparison
 */
bool
risk_cmp_eq(int a, int b)
{
    return (a == b);
}

bool
risk_cmp_neq(int a, int b)
{
    return (a != b);
}

bool
risk_cmp_lt(int a, int b)
{
    return (a < b);
}

bool
risk_cmp_gt(int a, int b)
{
    return (a > b);
}

bool
risk_cmp_lte(int a, int b)
{
    return (a <= b);
}

bool
risk_cmp_gte(int a, int b)
{
    return (a >= b);
}

bool
risk_cmp_true(int a, int b)
{
    (void)a;
    (void)b;
    return true;
}

risk_comparator
get_risk_cmp(int risk_op)
{
    switch (risk_op)
    {
        case RISK_OP_EQ  : return risk_cmp_eq;
        case RISK_OP_NEQ : return risk_cmp_neq;
        case RISK_OP_LT  : return risk_cmp_lt;
        case RISK_OP_GT  : return risk_cmp_gt;
        case RISK_OP_LTE : return risk_cmp_lte;
        case RISK_OP_GTE : return risk_cmp_gte;
    }
    return risk_cmp_true;
}

/*
 * Category comparison
 *
 * Since we are passing all possible categories to the comparison,
 * we can immediately handle IN/OUT as part of the comparison function.
 */
bool
cat_cmp_eval(int c, struct int_set *s)
{
    size_t i;

    /* NULL set represents an empty set */
    if (s == NULL) return false;

    for (i = 0; i < s->nelems; i++)
    {
        if (c == s->array[i]) return true;
    }
    return false;
}

bool
cat_cmp_in(int c, struct int_set *s)
{
    return cat_cmp_eval(c, s);
}

bool
cat_cmp_out(int c, struct int_set *s)
{
    return !cat_cmp_eval(c, s);
}

bool
cat_cmp_true(int c, struct int_set *s)
{
    (void)c;
    (void)s;
    return true;
}

cat_comparator
get_cat_cmp(int cat_op)
{
    switch (cat_op)
    {
        case CAT_OP_IN : return cat_cmp_in;
        case CAT_OP_OUT: return cat_cmp_out;
    }
    return cat_cmp_true;
}

/*
 * HOSTNAME cache flushing functions
 *
 * All comparison functions take the rule as second parameter.
 * This will require extra work to be performed by the caller for IN/OUT.
 * In the case of the _wild function, the second parameter is a regex that
 * needs to be compiled and executed.
 */
bool
hostname_cmp(char *a, char *b)
{
    return (strncmp(a, b, strlen(a)) == 0);
}

bool
hostname_cmp_sfr(char *hostname, char *suffix)
{
    return str_endswith(hostname, suffix);
}

bool
hostname_cmp_sfl(char *hostname, char *prefix)
{
    return str_startswith(hostname, prefix);
}

bool
hostname_cmp_wild(char *hostname, char *wildcard)
{
    return fsm_policy_wildmatch(wildcard, hostname);
}

bool
hostname_cmp_true(char *a, char *b)
{
    (void)a;
    (void)b;
    return true;
}

hostname_comparator
get_hostname_cmp(int fqdn_op)
{
    switch (fqdn_op)
    {
        case FQDN_OP_IN:
        case FQDN_OP_OUT:
            return hostname_cmp;

        case FQDN_OP_SFR_IN:
        case FQDN_OP_SFR_OUT:
            return hostname_cmp_sfr;

        case FQDN_OP_SFL_IN:
        case FQDN_OP_SFL_OUT:
            return hostname_cmp_sfl;

        case FQDN_OP_WILD_IN:
        case FQDN_OP_WILD_OUT:
            return hostname_cmp_wild;
    }
    return hostname_cmp_true;
}

/*
 * APPLICATION cache flushing functions
 *
 * Second parameter is one of the application names present in the str_set
 * of the rule. This will require extra work to be performed by the caller
 * for IN/OUT.
 */
bool
app_cmp(const char *a, const char *b)
{
    return (strncmp(a, b, strlen(a)) == 0);
}

bool
app_cmp_true(const char *a, const char *b)
{
    (void)a;
    (void)b;
    return true;
}

app_comparator
get_app_cmp(int app_op)
{
    switch (app_op)
    {
        case APP_OP_IN:
        case APP_OP_OUT:
            return app_cmp;
    }
    return app_cmp_true;
}

/*
 * IP address comparison
 *
 * We need to handle both IPv4 and IPv6.
 */
bool
ip_cmp(struct sockaddr_storage *a, struct sockaddr_storage *b)
{
    return sockaddr_storage_equals(a, b);
}

bool
ip_cmp_true(struct sockaddr_storage *a, struct sockaddr_storage *b)
{
    (void)a;
    (void)b;
    return true;
}

ip_comparator
get_ip_cmp(int ip_op)
{
    switch (ip_op)
    {
        case IP_OP_IN:
        case IP_OP_OUT:
            return ip_cmp;
    }
    return ip_cmp_true;
}
