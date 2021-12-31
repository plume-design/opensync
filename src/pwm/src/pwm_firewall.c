/*
 * Copyright (c) 2021, Sagemcom.
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

#include <string.h>
#include <stdio.h>

#include "log.h"
#include "schema.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "ovsdb.h"

#include "pwm_firewall.h"
#include "pwm_ovsdb.h"
#include "pwm_utils.h"

#define MODULE_ID LOG_MODULE_ID_MISC
#define TARGET_LEN 1024
#define RULE_LEN 256

struct ovsdb_table table_Netfilter;

static bool pwm_firewall_add_rule(const char *name, const char *chain, const char *endpoint)
{
    bool errcode;
    struct schema_Netfilter entry;
    int family;
    char target[TARGET_LEN];
    int ret_inp;
    int ret_out;

    if (!name || !chain || !endpoint) {
        LOGE("Add firewall rule failed: invalid arguments");
        return false;
    }

    MEM_SET(&entry, 0, sizeof(entry));
    SCHEMA_SET_INT(entry.enable, true);
    SCHEMA_SET_STR(entry.name, name);
    SCHEMA_SET_STR(entry.table, "filter");
    SCHEMA_SET_STR(entry.chain, chain);
    SCHEMA_SET_INT(entry.priority, 128);
    SCHEMA_SET_STR(entry.target, "ACCEPT");

    family = pwm_utils_get_addr_family(endpoint);
    switch (family)
    {
        case AF_INET:
            SCHEMA_SET_STR(entry.protocol, "ipv4");
            break;

        case AF_INET6:
            SCHEMA_SET_STR(entry.protocol, "ipv6");
            break;

        default:
            LOGE("Add PWM firewall rule: invalid family %d", family);
            return false;
            break;
    }

    ret_inp = strncmp(chain, "INPUT", sizeof("INPUT"));
    ret_out = strncmp(chain, "OUTPUT", sizeof("INPUT"));
    if (ret_inp == 0)
    {
        snprintf(target, TARGET_LEN - 1, "-p 47 -s %s", endpoint);
    }
    else if (ret_out == 0)
    {
        snprintf(target, TARGET_LEN - 1, "-p 47 -d %s", endpoint);
    }
    else
    {
        LOGE("Add PWM firewall rule: invalid chain %s", chain);
        return false;
    }
    SCHEMA_SET_STR(entry.rule, target);

    errcode = ovsdb_table_insert(&table_Netfilter, &entry);
    if (!errcode) {
        LOGE("Add PWM firewall rule: insert OVSDB entry failed");
        return false;
    }

    return true;
}

static bool pwm_firewall_del_rule(const char *name)
{
    bool errcode;
    json_t *where;

    if (!name) {
        LOGE("Delete PWM firewall rule: invalid argument");
        return false;
    }

    where = ovsdb_where_simple_typed(SCHEMA_COLUMN(Netfilter, name), name, OCLM_STR);
    if (!where) {
        LOGE("Delete PWM firewall rule: create filter on %s failed", name);
        return false;
    }

    errcode = ovsdb_table_delete_where(&table_Netfilter, where);
    if (!errcode) {
        LOGE("Delete PWM firewall rule: delete OVSDB entry failed");
        return false;
    }

    return true;
}

bool pwm_firewall_add_rules(const char *endpoint)
{
    bool errcode_rx = false;
    bool errcode_tx = false;

    errcode_rx = pwm_firewall_add_rule(PWM_NETFILER_GRE_RX, "INPUT", endpoint);
    if (!errcode_rx) {
        LOGE("Add PWM firewall rules: add %s rule failed", PWM_NETFILER_GRE_RX);
    }

    errcode_tx = pwm_firewall_add_rule(PWM_NETFILER_GRE_TX, "OUTPUT", endpoint);
    if (!errcode_tx) {
        LOGE("Add PWM firewall rules: add %s rule failed", PWM_NETFILER_GRE_TX);
    }

    if ((errcode_rx == false) || (errcode_tx == false)) {
        return false;
    }

    LOGD("PWM firewall rules added for %s", endpoint);
    return true;
}

bool pwm_firewall_del_rules(void)
{
    bool errcode_rx = false;
    bool errcode_tx = false;

    errcode_rx = pwm_firewall_del_rule(PWM_NETFILER_GRE_RX);
    if (!errcode_rx) {
        LOGE("Delete PWM firewall rules: del %s rule failed", PWM_NETFILER_GRE_RX);
    }

    errcode_tx = pwm_firewall_del_rule(PWM_NETFILER_GRE_TX);
    if (!errcode_tx) {
        LOGE("Delete PWM firewall rules: del %s rule failed", PWM_NETFILER_GRE_TX);
    }

    if ((errcode_rx == false) || (errcode_tx == false)) {
        return false;
    }
    LOGD("PWM firewall rules deleted");

    return true;
}

// Add Radius Server Firewall rules
bool pwm_firewall_add_rs_rules(const char *rs_ip_address,
                         const char *rs_ip_port,
                         char *entry_name)
{
    bool errcode;
    int family;
    struct schema_Netfilter entry;
    char target[TARGET_LEN];

    LOGD("PWM RADIUS SERVER FW ADD: ip_ad=[%s] ip_port=[%s] name=[%s]",
         rs_ip_address, rs_ip_port, entry_name);

    if ((rs_ip_address == NULL)
        || (rs_ip_port == NULL)
        || (rs_ip_address[0] == 0)
        || (rs_ip_port[0] == 0))
    {
        LOGE("PWM RADIUS SERVER FW ADD: invalid arguments: ip_ad=[%s] ip_port=[%s]", rs_ip_address, rs_ip_port);
        return (false);
    }

    // table Netfilter
    // Based on Simon The INPUT Direction is not neaded because
    // -A INPUT -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
    MEM_SET(&entry, 0, sizeof(entry));
    SCHEMA_SET_INT(entry.enable, true);
    SCHEMA_SET_STR(entry.name, entry_name);
    SCHEMA_SET_STR(entry.table, "filter");
    SCHEMA_SET_STR(entry.chain, "OUTPUT");
    SCHEMA_SET_INT(entry.priority, 128);
    SCHEMA_SET_STR(entry.target, "ACCEPT");

    family = pwm_utils_get_addr_family(rs_ip_address);
    switch (family)
    {
        case AF_INET:
            SCHEMA_SET_STR(entry.protocol, "ipv4");
            snprintf(target, (sizeof(target) - 1),
                     "-d %s/32 -p udp -m udp --dport %s", rs_ip_address,
                     rs_ip_port);
            break;

        case AF_INET6:
            SCHEMA_SET_STR(entry.protocol, "ipv6");
            snprintf(target, (sizeof(target) - 1),
                     "-d %s/128 -p udp -m udp --dport %s", rs_ip_address,
                     rs_ip_port);
            break;

        default:
            LOGE("PWM RADIUS SERVER FW ADD: invalid family=%d for ip_ad=%s",
                 family, rs_ip_address);
            return false;
            break;
    }

    LOGD("PWM RADIUS SERVER FW ADD: TX target=[%s]", target);
    SCHEMA_SET_STR(entry.rule, target);

    errcode = ovsdb_table_insert(&table_Netfilter, &entry);
    if (!errcode) {
        LOGE("PWM RADIUS SERVER FW ADD: insert OVSDB entry failed");
        return false;
    }

    return true;
}

// Delete Radius Server Firewall rules
bool pwm_firewall_del_rs_rules(void)
{
    bool errcode;
    const char *name;
    json_t *where;

    name = PWM_NETFILER_RS_TX;
    LOGD("PWM RADIUS SERVER FW DELL: name %s", name);
    where = ovsdb_where_simple_typed(SCHEMA_COLUMN(Netfilter, name), name, OCLM_STR);
    if (where != NULL)
    {
        errcode = ovsdb_table_delete_where(&table_Netfilter, where);
        if (!errcode) {
            LOGI("PWM RADIUS SERVER FW DELL: delete OVSDB entry (%s) failed", name);
        }
    }
    else
    {
        LOGI("PWM RADIUS SERVER FW DELL: where delete OVSDB entry (%s) failed", name);
    }

    name = PWM_NETFILER_RS_TX_SEC;
    LOGD("PWM RADIUS SERVER FW DELL: name %s", name);
    where = ovsdb_where_simple_typed(SCHEMA_COLUMN(Netfilter, name), name, OCLM_STR);
    if (where != NULL)
    {
        errcode = ovsdb_table_delete_where(&table_Netfilter, where);
        if (!errcode) {
            LOGI("PWM RADIUS SERVER FW DELL: delete OVSDB entry (%s) failed", name);
        }
    }
    else
    {
        LOGI("PWM RADIUS SERVER FW DELL: where delete OVSDB entry (%s) failed", name);
    }

    return true;
}

bool pwm_firewall_init(void)
{
    LOGI("PWM: Initializing Netfilter table");

    OVSDB_TABLE_INIT(Netfilter, name);

    return true;
}
