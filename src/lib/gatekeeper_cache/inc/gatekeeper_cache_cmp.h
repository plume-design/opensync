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

#include <stdbool.h>

#include "os_types.h"
#include "fsm_policy.h"
#include "gatekeeper_cache.h"

/*
 * MAC comparison
 */
typedef bool (*mac_comparator)(os_macaddr_t *a, os_macaddr_t *b);
bool mac_cmp(os_macaddr_t *a, os_macaddr_t *b);
bool mac_cmp_true(os_macaddr_t *a, os_macaddr_t *b);
mac_comparator get_mac_cmp(int mac_op);

/*
 * Risk comparison
 */
typedef bool (*risk_comparator)(int a, int b);
bool risk_cmp_eq(int a, int b);
bool risk_cmp_neq(int a, int b);
bool risk_cmp_lt(int a, int b);
bool risk_cmp_gt(int a, int b);
bool risk_cmp_lte(int a, int b);
bool risk_cmp_gte(int a, int b);
bool risk_cmp_true(int a, int b);
risk_comparator get_risk_cmp(int risk_op);

/*
 * Category comparison
 *
 * Since we are passing all possible categories to the comparison,
 * we can immediately handle IN/OUT as part of the comparison function.
 */
typedef bool (*cat_comparator)(int c, struct int_set *s);
bool cat_cmp_eval(int c, struct int_set *s);
bool cat_cmp_in(int c, struct int_set *s);
bool cat_cmp_out(int c, struct int_set *s);
bool cat_cmp_true(int c, struct int_set *s);
cat_comparator get_cat_cmp(int cat_op);

/*
 * HOSTNAME comparison
 *
 * All comparison functions take the rule as second parameter.
 * This will require extra work to be performed by the caller for IN/OUT.
 * In the case of the _wild function, the second parameter is a regex that
 * needs to be compiled and executed.
 */
typedef bool (*hostname_comparator)(char *a, char *b);
bool hostname_cmp(char *a, char *b);
bool hostname_cmp_sfr(char *a, char *b);
bool hostname_cmp_sfl(char *a, char *b);
bool hostname_cmp_wild(char *a, char *b);
bool hostname_cmp_true(char *a, char *b);
hostname_comparator get_hostname_cmp(int fqdn_op);

/*
 * APPLICATION name comparison
 *
 * Second parameter is one of the application names present in the str_set
 * of the rule. This will require extra work to be performed by the caller
 * for IN/OUT.
 */
typedef bool (*app_comparator)(const char *a, const char *b);
bool app_cmp(const char *a, const char *b);
bool app_cmp_true(const char *a, const char *b);
app_comparator get_app_cmp(int app_op);

/*
 * IP address comparison
 *
 * Comparison is performed on text representation of IP address.
 *
 * Second parameter is one of the IP addresses present in the str_set
 * of the rule. IP addresses (IPv4 or IPv6) will have been transcribed
 * into their binary form.
 * This will require extra work to be performed by the caller
 * for IN/OUT.
 */
typedef bool (*ip_comparator)(struct sockaddr_storage *a, struct sockaddr_storage *b);
bool ip_cmp(struct sockaddr_storage *a, struct sockaddr_storage *b);
bool ip_cmp_true(struct sockaddr_storage *a, struct sockaddr_storage *b);
ip_comparator get_ip_cmp(int cat_op);
