/*
* Copyright (c) 2020, Sagemcom.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* 3. Neither the name of the copyright holder nor the names of its contributors
*    may be used to endorse or promote products derived from this software
*    without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>
#include <ev.h>
#include <syslog.h>
#include <getopt.h>
#include <stdint.h>
#include <linux/types.h>
#include <arpa/inet.h>

#include "evsched.h"
#include "log.h"
#include "os.h"
#include "ovsdb.h"
#include "evext.h"
#include "os_backtrace.h"
#include "json_util.h"
#include "target.h"
#include "ds_dlist.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "osn_mapt.h"
#include "osn_types.h"

#include "maptm.h"

/* Firewall Rules */
#define V4_CHCEK_FORWARD "v4_check_forward"
#define V4_MAPT_CHCEK_FORWARD "v4_mapt_check_forward"
#define V4_MAPT_TCP_CHCEK_1 "v4_mapt_tcp_check_1"
#define V4_MAPT_TCP_CHCEK_2 "v4_mapt_tcp_check_2"
#define V4_CT_CHECK "v4_ct_check"

#define V6_MAPT_TCP_CHCEK_3 "v6_tcp_check_3"

struct maptm_MAPT strucWanConfig;

// Free MAP-T struct
void maptm_remove_maptStruct(struct mapt *mapt_rule)
{
    if (mapt_rule == NULL) return;
    free(mapt_rule->dmr);
    free(mapt_rule->ipv6prefix);
    free(mapt_rule->ipv4prefix);
    free(mapt_rule);
}

// Free list node
bool maptm_remove_list(ds_dlist_t rules)
{
    ds_dlist_iter_t iter;
    struct list_rules *node = NULL;

    for (   node = ds_dlist_ifirst(&iter, &rules);
            node != NULL;
            node = ds_dlist_inext(&iter))
    {
        ds_dlist_iremove(&iter);
        free(node->value);
        free(node);
    }
    return true;
}

// Parse MAP-T rules
struct mapt* parse_option_rule(char *rule)
{
    if (rule == NULL)
    {
        LOGE("MAP-T Rule is NULL");
        return NULL;
    }
    struct mapt *mapt_rule = malloc(sizeof(struct mapt));
    if (mapt_rule == NULL)
    {
        LOGE("Unable to allocate update handler!");
        return NULL;
    }
    char *p = strtok(rule, ",");
    while (p != NULL)
    {
        char *name;
        char *value;
        name = strsep(&p, "=");

        if (name == NULL) continue;
        value = p;
        if (value == NULL)
        {
            LOGD("MAP-T: Error parsing rule.");
            continue;
        }

        if (!strcmp(name, "ealen"))
        {
            mapt_rule->ealen = atoi(value);
        }
        else if (!strcmp(name, "prefix4len"))
        {
            mapt_rule->prefix4len = atoi(value);
        }
        else if (!strcmp(name, "prefix6len"))
        {
            mapt_rule->prefix6len = atoi(value);
        }
        else if (!strcmp(name, "offset"))
        {
            mapt_rule->offset = atoi(value);
        }
        else if (!strcmp(name, "psidlen"))
        {
            mapt_rule->psidlen = atoi(value);
        }
        else if (!strcmp(name, "psid"))
        {
            mapt_rule->psid = atoi(value);
        }
        else if (!strcmp(name, "ipv4prefix"))
        {
            mapt_rule->ipv4prefix = strdup(value);
            if ((mapt_rule->ipv4prefix) == NULL)
            {
                LOGE("Unable to allocate update handler!");
                goto free;
            }
        }
        else if (!strcmp(name, "ipv6prefix"))
        {
            mapt_rule->ipv6prefix = strdup(value);
            if ((mapt_rule->ipv6prefix) == NULL)
            {
                LOGE("Unable to allocate update handler!");
                goto free;
            }
        }
        else if (!strcmp(name, "dmr"))
        {
            mapt_rule->dmr = strdup(value);
            if ((mapt_rule->dmr) == NULL)
            {
                LOGE("Unable to allocate update handler!");
                goto free;
            }
        }
        p = strtok(NULL, ",");
    } /* while */

    return mapt_rule;

free:
    maptm_remove_maptStruct(mapt_rule);
    return NULL;
}

// Compare between the ipv6prefix of MAP-T rule and the IA-PD
int comparePrefix(
        const char *iapd_prefix,
        const char *mapt_prefix,
        int length
)
{
    struct in6_addr iapd;
    struct in6_addr pref;

    if (!inet_pton(AF_INET6, iapd_prefix, &iapd)) return 0;

    if (!inet_pton(AF_INET6, mapt_prefix, &pref)) return 0;

    uint32_t mask;
    if (length == -1) length = 128;

    if ((length >= 0) && (length <= 32))
    {
        mask = htonl((uint32_t)-1 << (32 - length));
        if ((iapd.s6_addr32[0] & mask) == (pref.s6_addr32[0] & mask))
        {
            return 1;
        }
    }
    else if ((length >= 33) && (length <= 64))
    {
        mask = htonl((uint32_t)-1 << (64 - length));
        if ((iapd.s6_addr32[0] == pref.s6_addr32[0])
            && (iapd.s6_addr32[1] & mask) == (pref.s6_addr32[1] & mask))
        {
            return 1;
        }
    }
    else if ((length >= 65) && (length <= 96))
    {
        mask = htonl((uint32_t)-1 << (96 - length));
        if ((iapd.s6_addr32[0] == pref.s6_addr32[0])
            && (iapd.s6_addr32[1] == pref.s6_addr32[1])
            && (iapd.s6_addr32[2] & mask) == (pref.s6_addr32[2] & mask))
        {
            return 1;
        }
    }
    else if ((length >= 97) && (length <= 128))
    {
        mask = htonl((uint32_t)-1 << (128 - length));
        if ((iapd.s6_addr32[0] == pref.s6_addr32[0])
            && (iapd.s6_addr32[1] == pref.s6_addr32[1])
            && (iapd.s6_addr32[2] == pref.s6_addr32[2])
            && (iapd.s6_addr32[3] & mask) == (pref.s6_addr32[3] & mask))
        {
            return 1;
        }
    }
    return 0;
}

// Select matched MAP-T rule
struct mapt* get_Mapt_Rule(char *option95, char *iapd)
{
    LOGT("Get MAP-t Rules");
    if (!option95) return NULL;

    bool ret = false;
    char *mapt_option95 = strdup(option95);
    if (mapt_option95 == NULL) return NULL;

    ds_dlist_t l_rules;
    ds_dlist_init(&l_rules, struct list_rules, d_node);

    // Fill MAP-T rules list
    char *rule = strtok(mapt_option95, " ");
    while (rule != NULL)
    {
        struct list_rules *l_node;
        l_node = malloc(sizeof(struct list_rules));
        if (l_node == NULL)
        {
            LOGE("Unable to allocate update handler!");
            free(l_node);
            free(mapt_option95);
            return NULL;
        }

        l_node->value = strdup(rule);
        if ((l_node->value) == NULL)
        {
            LOGE("Unable to allocate update handler!");
            free(l_node);
            maptm_remove_list(l_rules);
            free(mapt_option95);
            return NULL;
        }

        ds_dlist_insert_tail(&l_rules, l_node);
        rule = strtok(NULL, " ");
    }

    struct list_rules *node = NULL;
    ds_dlist_iter_t iter;
    struct mapt *mapt_rule = NULL;

    for (   node = ds_dlist_ifirst(&iter, &l_rules);
            node != NULL;
            node = ds_dlist_inext(&iter))
    {
        mapt_rule = parse_option_rule(node->value);
        if (mapt_rule)
        {
            ret = comparePrefix(iapd, mapt_rule->ipv6prefix, mapt_rule->prefix6len);
            if (ret)
            {
                LOGD("MAP-T rule found");
                break;
            }
            else
            {
                maptm_remove_maptStruct(mapt_rule);
            }
        }
    }

    free(mapt_option95);
    maptm_remove_list(l_rules);
    if (!ret)
    {
        LOGD("MAP-T rule NOT found");
        return NULL;
    }
    return mapt_rule;
}

// Configure Map Domain
void configureMapDomain(
        char *iapd,
        int iapd_length,
        struct mapt *mapt_rule
)
{
    struct in6_addr addr6Wan;
    char ipv6PrefixHex[19];

    // Set Domain PSID Length
    mapt_rule->domain_psid_len = mapt_rule->ealen - (IPV4_ADDRESS_SIZE - mapt_rule->prefix4len);

    // Set Ratio
    mapt_rule->ratio = (1 << mapt_rule->domain_psid_len);

    // Set Domain PSID
    inet_pton(AF_INET6, iapd, &addr6Wan);
    snprintf(
        ipv6PrefixHex,
        sizeof(ipv6PrefixHex),
        "0x%02x%02x%02x%02x%02x%02x%02x%02x",
        (int)addr6Wan.s6_addr[0],
        (int)addr6Wan.s6_addr[1],
        (int)addr6Wan.s6_addr[2],
        (int)addr6Wan.s6_addr[3],
        (int)addr6Wan.s6_addr[4],
        (int)addr6Wan.s6_addr[5],
        (int)addr6Wan.s6_addr[6],
        (int)addr6Wan.s6_addr[7]);

    uint64_t ipv6addr;
    ipv6addr = strtoll(ipv6PrefixHex, NULL, 0);
    ipv6addr <<= mapt_rule->prefix6len;
    ipv6addr >>= mapt_rule->prefix6len + (IPV6_PREFIX_MAX_SIZE - iapd_length);
    uint32_t suffix = ipv6addr>>(mapt_rule->ealen - (IPV4_ADDRESS_SIZE-mapt_rule->prefix4len));
    if (!((IPV4_ADDRESS_SIZE-mapt_rule->prefix4len) == (mapt_rule->ealen)))
    {
        mapt_rule->domain_psid = ipv6addr&(~(suffix << (mapt_rule->ealen - (IPV4_ADDRESS_SIZE - mapt_rule->prefix4len))));
    }

    // Set Public IPv4 Address
    uint32_t swapped_suffix = ((suffix>>24)&0xff)
        | ((suffix<<8)&0xff0000)
        | ((suffix>>8)&0xff00)
        | ((suffix<<24)&0xff000000);

    struct in_addr ipv4BinPrefixRule;
    inet_pton(AF_INET, mapt_rule->ipv4prefix, &ipv4BinPrefixRule);
    struct in_addr ipv4BinPublicAddress;
    ipv4BinPublicAddress.s_addr = ((uint32_t)ipv4BinPrefixRule.s_addr | swapped_suffix);
    inet_ntop(AF_INET, &ipv4BinPublicAddress, mapt_rule->ipv4PublicAddress, IPV4_ADDRESS_SIZE);
}

// Get MAP-T configuration
struct mapt* maptm_getconfigure(char *option95, char *iapd, int iapd_len)
{
    struct mapt *selected_rule = get_Mapt_Rule(option95, iapd);
    if (selected_rule == NULL) return NULL;

    configureMapDomain(iapd, iapd_len, selected_rule);

    return selected_rule;
}

// MAP-T Firewall configuration
bool maptm_ovsdb_nfm_set_rule(const char *rule, bool enable)
{
    struct schema_Netfilter set;
    json_t *where = NULL;
    int rc = 0;

    memset(&set, 0, sizeof(set));
    set._partial_update = true;
    SCHEMA_SET_INT(set.enable, enable);

    where = ovsdb_where_simple(SCHEMA_COLUMN(Netfilter, name), rule);

    if (!where)
    {
        LOGE("[%s] Set NAT Netfilter rule: create filter failed", rule);
        return false;
    }

    rc = ovsdb_table_update_where(&table_Netfilter, where, &set);
    if (rc != 1)
    {
        LOGE("[%s] Set NAT Netfilter rule: unexpected result count %d", rule, rc);
        return false;
    }
    return true;
}

// Set MAP-T firewall rules
bool maptm_ovsdb_nfm_rules(bool enable)
{
    LOGT("%s, MAP-T firewall configuration", __func__);

    if (!maptm_ovsdb_nfm_set_rule(V4_CHCEK_FORWARD, !enable)) return false;
    if (!maptm_ovsdb_nfm_set_rule(V4_MAPT_CHCEK_FORWARD, enable)) return false;
    if (!maptm_ovsdb_nfm_set_rule(V4_MAPT_TCP_CHCEK_1, enable)) return false;
    if (!maptm_ovsdb_nfm_set_rule(V4_MAPT_TCP_CHCEK_2, enable)) return false;
    if (!maptm_ovsdb_nfm_set_rule(V6_MAPT_TCP_CHCEK_3, !enable)) return false;
    if (!maptm_ovsdb_nfm_set_rule(V4_CT_CHECK, !enable)) return false;

    return true;
}

// Stop MAP-T functionality
bool stop_mapt(void)
{
    if (!maptm_ovsdb_nfm_rules(false)) LOGE("Could not disable Firewall");
    if (!osn_mapt_stop()) return false;

    LOGD("Stopped MAP-T");
    return true;
}

// Get LAN Subnet
char* get_subnetcidr4(char *subnetcidr4)
{
    struct schema_Wifi_Inet_Config iconf;
    int ret;
    osn_ip_addr_t netmask;
    int prefix = 0;
    MEMZERO(iconf);

    ret = ovsdb_table_select_one(
            &table_Wifi_Inet_Config,
            SCHEMA_COLUMN(Wifi_Inet_Config, if_name),
            MAPT_IFC_LAN,
            &iconf);

    if (!ret)
    {
        LOGE("%s: Failed to get Interface config", __func__);
        return NULL;
    }
    if (!osn_ip_addr_from_str(&netmask, iconf.netmask))
    {
        LOGE("%s: Wrong Netmask", __func__);
        return NULL;
    }
    prefix = osn_ip_addr_to_prefix(&netmask);
    sprintf(subnetcidr4, "%s/%d", iconf.inet_addr, prefix);
    return subnetcidr4;
}

// Configure MAP-T functionality
bool config_mapt(void)
{
    if (strucWanConfig.mapt_95_value[0] == '\0' || strucWanConfig.iapd[0] == '\0')
    {
        LOGE("Unable to configure MAP-T option");
        return false;
    }

    // Get LAN Subnet
    char subnetcidr4[20] = "";
    if (get_subnetcidr4(subnetcidr4) == NULL)
    {
        LOGE("Unable to configure MAP-T option (unable to get subnetcidr4)");
        return false;
    }

    // Get IA-PD prefix and its length
    char iapd[256] = "";
    int iapd_len = 0;

    char *flag = NULL;
    STRSCPY(iapd, strucWanConfig.iapd);

    flag = strtok(iapd, ",");
    if (flag == NULL) return false;
    flag = strtok(iapd, "/");
    if (flag != NULL)
    {
        flag = strtok(NULL, ",");
        if (flag == NULL)
        {
            LOGE("Failed to parse IA-PD");
            return false;
        }
        iapd_len = atoi(flag);
    }

    struct mapt *MaptConf = maptm_getconfigure(strucWanConfig.mapt_95_value, iapd, iapd_len);

    if (MaptConf == NULL)
    {
        LOGE("Unable to get MAP-T Rule");
        return false;
    }

    // Add the length to the Prefix v4/v6
    char ipv6prefix[100] = "";
    char ipv4PublicAddress[100] = "";
    snprintf(ipv6prefix, sizeof(ipv6prefix), "%s/%d", MaptConf->ipv6prefix, MaptConf->prefix6len);
    snprintf(ipv4PublicAddress, sizeof(ipv4PublicAddress), "%s/%d", MaptConf->ipv4PublicAddress, MaptConf->prefix4len);

    // Enable firewall
    if (!maptm_ovsdb_nfm_rules(true))
    {
        LOGE("Unable to configure Firewall");
        maptm_remove_maptStruct(MaptConf);
        return false;
    }

    bool result;
    result = osn_mapt_configure(
            MaptConf->dmr,
            MaptConf->ratio,
            MAPT_IFC_LAN,
            MAPT_IFC_WAN,
            ipv6prefix,
            subnetcidr4,
            ipv4PublicAddress,
            MaptConf->offset,
            MaptConf->domain_psid);

    // Run target MAP-T Configuration
    if (result)
    {
        LOGT("MAP-T configured");
        maptm_remove_maptStruct(MaptConf);
        return true;
    }

    maptm_remove_maptStruct(MaptConf);
    LOGE("MAP-T not configured");

    return false;
}
