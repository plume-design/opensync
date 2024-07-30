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

#include <stdio.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "os.h"

#include "pwm_ovsdb.h"
#include "pwm_utils.h"
#include "pwm_tunnel.h"
#include "pwm_bridge.h"
#include "pwm_wifi.h"


#define ADDRESS_TMP_FILE "/tmp/address"
#define MODULE_ID LOG_MODULE_ID_OVSDB
#define OPT_WAIT_TIMEOUT 30
#define REM_ENDPOINT_IP_TABLE_SIZE 20
#define DOMAIN_NM_LEN 256
#define CMD_LEN 156

#define ENTERPRISE_ID_SIZE        8
#define OPTION_TAG_SIZE           4
#define OPTION_LENGTH_SIZE        4
#define CHARACTER_SIZE            2
#define VENDOR_SPECIFIC_SUBOPTION 21

struct ovsdb_table table_Public_Wifi_Config;
struct ovsdb_table table_Public_Wifi_State;
struct ovsdb_table table_Wifi_Inet_Config;
struct ovsdb_table table_Wifi_Inet_State;
struct ovsdb_table table_DHCP_Option;
struct ovsdb_table table_IP_Interface;
struct ovsdb_table table_IPv6_Address;
struct ovsdb_table table_DHCPv6_Client;
struct ovsdb_table table_Wifi_VIF_Config;

static bool PWM_enable = false;
static char v4_domain_nm[DOMAIN_NM_LEN];
static char v6_domain_nm[DOMAIN_NM_LEN];
static char rem_endpoint_ip_addr_table[REM_ENDPOINT_IP_TABLE_SIZE][INET6_ADDRSTRLEN];

static struct schema_Public_Wifi_Config pwm_local_config;
static struct schema_Public_Wifi_State public_wifi_state;

char *g_tunnel_gre_status_msg = NULL;
char *g_gre_endpoint = NULL;

static void pmw_ovsdb_set_v6_domain_name(const char *name)
{
    STRSCPY_WARN(v6_domain_nm, name);
    LOGD("[%s] Set to [%s]", __FUNCTION__, v6_domain_nm);
    return;
}

static void pwm_ovsdb_set_v4_domain_name(const char *name)
{
    STRSCPY_WARN(v4_domain_nm, name);
    LOGD("[%s] Set to [%s]", __FUNCTION__, v4_domain_nm);
    return;
}

static void pwm_ovsdb_get_v6_domain_name(void)
{
    LOGD("[%s] Get v6 domain name [%s]", __FUNCTION__, v6_domain_nm);
}

static void pwm_ovsdb_get_v4_domain_name(void)
{
    LOGD("[%s] Get v4 domain name [%s]", __FUNCTION__, v4_domain_nm);
}

static void pwm_ovsdb_set_pw_enable(bool val)
{
    PWM_enable = val;
    return;
}

bool pwm_ovsdb_is_enabled(void)
{
    return PWM_enable;
}

static bool pwm_ovsdb_is_v6_opt17(int opt, const char *ver, const char *type)
{
    int ret_v6;
    int ret_rx;

    ret_v6 = strncmp(ver, "v6", sizeof("v6"));
    ret_rx = strncmp(type, "rx", sizeof("rx"));

    if ((opt == 17) && (ret_v6 == 0) && (ret_rx == 0)) {
        return true;
    }
    return false;
}

int pwm_ovsdb_str_to_num(char *string, int size)
{
    char hex_string[DOMAIN_NM_LEN] = {'\0'};
    strncpy(hex_string, string, size);
    return strtol(hex_string, NULL, 16);
}

int pwm_ovsdb_eid_matches(char *hexstring, uint32_t eid)
{
    uint32_t eid_number = pwm_ovsdb_str_to_num(hexstring, ENTERPRISE_ID_SIZE);
        if (eid_number == eid) {
        return 1;
    } else {
        return 0;
    }
}

int pwm_ovsdb_extract_fqdn(char *hexstring, int length, char dest[])
{
    int i;

    if (length > DOMAIN_NM_LEN - 1) {
        LOGE("[%s] FQDN is longer than %d", __FUNCTION__, DOMAIN_NM_LEN - 1);
        return 0;
    }

    uint8_t character_hex;
    for (i = 0; i < length; i++) {
        character_hex = pwm_ovsdb_str_to_num(hexstring + i * CHARACTER_SIZE, CHARACTER_SIZE);
        dest[i] = (char)character_hex;
    }

    return 1;
}

int pwm_ovsdb_select_suboption(char *hexstring, int suboption, char dest[])
{
    int hexstring_length = strlen(hexstring);
    uint16_t option_tag;
    uint16_t option_length;
    char *pointer = hexstring;
    while (pointer < hexstring + hexstring_length) {
        option_tag = pwm_ovsdb_str_to_num(pointer, OPTION_TAG_SIZE);
        pointer += OPTION_TAG_SIZE;
        option_length = pwm_ovsdb_str_to_num(pointer, OPTION_LENGTH_SIZE);
        pointer += OPTION_LENGTH_SIZE;

        if (option_tag == suboption) {
            LOGD("[%s] Found suboption [%d]", __FUNCTION__, suboption);

            if (!pwm_ovsdb_extract_fqdn(pointer, option_length, dest)) {
                LOGE("[%s] Error while extracting FQDN from suboption [%d]", __FUNCTION__, suboption);
                return 0;
            }
            return 1;
        }

        pointer += option_length * CHARACTER_SIZE;
    }
    LOGI("[%s] Option 17 suboption not found", __FUNCTION__);
    return 0;
}

int pwm_ovsdb_extract_vendor_suboption(char *hexstring, char dest[])
{
    char *pointer = hexstring;

    if (!pwm_ovsdb_eid_matches(pointer, CONFIG_PWM_ENTERPRISE_ID)) {
        LOGI("[%s] Option 17 Enterprise ID doesn't match", __FUNCTION__);
        return 0;
    }

    if (!pwm_ovsdb_select_suboption(pointer + ENTERPRISE_ID_SIZE, VENDOR_SPECIFIC_SUBOPTION, dest)) {
        LOGD("[%s] Error selecting suboption", __FUNCTION__);
        return 0;
    }

    if (dest[0]) {
        LOGD("[%s] FQDN: %s", __FUNCTION__, dest);
        return 1;
    } else {
        LOGD("[%s] FQDN not found", __FUNCTION__);
        return 0;
    }
}

static int pwm_ovsdb_resolve_dns(const char *domain, int family)
{
    int ret_num_found = 0;
    int ret;
    int i = 0;
    struct addrinfo *res_ptr;
    struct addrinfo *res_ptr_tmp;
    void *ptr = NULL;
    struct addrinfo hints;

    LOGD("[%s] [%s] [%d]", __FUNCTION__, domain, family);
    MEM_SET(&hints, 0, sizeof(struct addrinfo));
    MEM_SET(&rem_endpoint_ip_addr_table, '\0',
           sizeof(rem_endpoint_ip_addr_table[0][0])
           * REM_ENDPOINT_IP_TABLE_SIZE * INET6_ADDRSTRLEN);

    hints.ai_family = family;
    hints.ai_flags |= AI_ADDRCONFIG;

    ret = getaddrinfo(domain, "domain", &hints, &res_ptr);
    if (ret == 0)
    {
        res_ptr_tmp = res_ptr;
        while (res_ptr_tmp)
        {
            ptr = NULL;
            switch (res_ptr_tmp->ai_family)
            {
                case AF_INET:
                    LOGD("[%s] - AF_INET", __FUNCTION__);
                    ptr = &((struct sockaddr_in *) res_ptr_tmp->ai_addr)->sin_addr;
                    break;
                case AF_INET6:
                    LOGD("[%s] - AF_INET6", __FUNCTION__);
                    ptr = &((struct sockaddr_in6 *) res_ptr_tmp->ai_addr)->sin6_addr;
                    break;
                default:
                    LOGI("[%s] - wrong AF_INET/AF_INET6 family=[%d]", __FUNCTION__, res_ptr_tmp->ai_family);
                    break;
            }

            if (ptr != NULL)
            {
                if (inet_ntop(res_ptr_tmp->ai_family, ptr, rem_endpoint_ip_addr_table[i], INET6_ADDRSTRLEN))
                {
                    ret_num_found++;
                    LOGD("[%s] DNS lookup OK [%s] -> [%d][%s]", __FUNCTION__,
                         domain, i, rem_endpoint_ip_addr_table[i]);
                }
                else
                {
                    LOGI("[%s] Couldn't convert resolved GRE remote endpoint IP adress for [%s]:[%d] to text", __FUNCTION__, domain, i);
                }
            }

            i++;
            if (i >= REM_ENDPOINT_IP_TABLE_SIZE) {
                break;
            }
            res_ptr_tmp = res_ptr_tmp->ai_next;
        }
        freeaddrinfo(res_ptr);
    }

    return ret_num_found;
}

char* pwm_ovsdb_update_gre_tunnel_status(GRE_tunnel_status_type status)
{
    char *status_ptr = NULL;

    LOGD("Update PWM State: GRE status: status=[%d]", status);

    switch (status)
    {
        case GRE_tunnel_DOWN:
            status_ptr = "DOWN";
            break;

        case GRE_tunnel_DOWN_FQDN:
            status_ptr = "DOWN: FQDN couldn't be resolved";
            break;

        case GRE_tunnel_DOWN_PING:
            status_ptr = "DOWN: Remote endpoint not reachable";
            break;

        case GRE_tunnel_DOWN_CREATION_ERROR:
            status_ptr = "DOWN: Creation Error";
            break;

        case GRE_tunnel_UP:
            status_ptr = "UP";
            break;

        default:
            status_ptr = "UNKNOWN";
            break;
    }

    return status_ptr;
}

bool pwm_ovsdb_check_remote_endpoint_alive(const char *remote_endpoint)
{
    int err;
    int ret;
    char cmd[CMD_LEN];

    ret = pwm_utils_get_addr_family(remote_endpoint);

    if (ret == AF_INET6) {
        snprintf(cmd, CMD_LEN - 1, "ping6 -c 1 -W 1 %s", remote_endpoint);
    } else if (ret == AF_INET) {
        snprintf(cmd, sizeof(cmd) - 1, "ping -c 1 -W 1 %s", remote_endpoint);
    } else {
        LOGE("Invalid inet family");
        return false;
    }
    cmd[CMD_LEN - 1] = '\0';
    err = cmd_log_check_safe(cmd);
    if (err) {
        LOGE("Ping to PWM end-point %s failed", remote_endpoint);
        return false;
    } else {
        return true;
    }
}

//Resolve AAAA first, if AAAA is not available resolve A, in any case preferred one is AAAA if available.
//IPv6 is preferred over IPv4 when both options are available.
static bool pwm_ovsdb_choose_endpoint(void)
{
    int i = 0;

    while ((rem_endpoint_ip_addr_table[i][0]) && (i < REM_ENDPOINT_IP_TABLE_SIZE))
    {
        LOGD("[%s] Checking remote endpoint alive [%s] index [%d]",
             __func__, rem_endpoint_ip_addr_table[i], i);
        if (pwm_ovsdb_check_remote_endpoint_alive(rem_endpoint_ip_addr_table[i]))
        {
            LOGD("[%s] Checking remote endpoint alive [%s] index [%d] - found",
                 __func__, rem_endpoint_ip_addr_table[i], i);

            pwm_tunnel_far_ip_set(rem_endpoint_ip_addr_table[i]);
            return true;
        }
        i++;
    };

    LOGE("[%s] No GRE remote endpoints available", __func__);
    return false;
}

static bool pwm_ovsdb_fetch_domain_option(void)
{
    struct schema_DHCPv6_Client *dh6c;
    struct schema_DHCP_Option dh_op;
    json_t *where;
    int i;
    int err_code;
    bool ret;
    char domain_name[DOMAIN_NM_LEN] = {'\0'};

    bool enable = true;
    int rc = 0;
    bool get_v6opt17 = false;

    // Get DHCPv6_Client record
    dh6c = ovsdb_table_select_typed(&table_DHCPv6_Client, "enable", OCLM_BOOL, &enable, &rc);
    if (dh6c == NULL) {
        LOGE("DHCPv6 Client record fetch failed");
        return false;
    }

    // Iterate through all received options records to fetch DHCP_Option records
    for (i = 0; i < dh6c->received_options_len; i++)
    {
        MEM_SET(&dh_op, 0, sizeof(dh_op));
        // Get DHCP Option record of received options
        where = ovsdb_where_uuid("_uuid", dh6c->received_options[i].uuid);
        if (where == NULL) {
            LOGE("DHCPv6 option record condition failed");
            break;
        }

        err_code = ovsdb_table_select_one_where(&table_DHCP_Option, where, &dh_op);
        if (!err_code) {
            LOGE("DHCPv6 option record fetch failed");
            break;
        }

        ret = pwm_ovsdb_is_v6_opt17(dh_op.tag, dh_op.version, dh_op.type);
        if (!ret) {
            LOGD("skipping DHCPv6 option: not Option 17");
            continue;
        } else {
            LOGD("DHCPv6 Option 17 found");
        }

        ret = pwm_ovsdb_extract_vendor_suboption(dh_op.value, domain_name);
        if (ret) {
            pmw_ovsdb_set_v6_domain_name((const char *)&domain_name);
            get_v6opt17 = true;
            break;
        }
    }
    free(dh6c);

    if (get_v6opt17 != true) {
        LOGE("[%s] Failed to receive IPv6 option 17", __func__);
        return false;
    }

    return true;
}

static bool pwm_ovsdb_configure_endpoint(void)
{
    int num_found;

    pmw_ovsdb_set_v6_domain_name("");
    pwm_ovsdb_set_v4_domain_name("");

    pwm_ovsdb_fetch_domain_option();

    pwm_ovsdb_get_v6_domain_name();
    pwm_ovsdb_get_v4_domain_name();
    // DHCP Option 17 - DNS Lookup for AF_INET6
    if (strlen(v6_domain_nm)) {
        num_found = pwm_ovsdb_resolve_dns(v6_domain_nm, AF_INET6);
        if (num_found > 0)
        {
            if (pwm_ovsdb_choose_endpoint()) {
                return true;
            }
        }
    }

    // DHCP Option 15 - DNS Lookup for AF_INET
    if (strlen(v4_domain_nm)) {
        num_found = pwm_ovsdb_resolve_dns(v4_domain_nm, AF_INET);
        if (num_found > 0)
        {
            if (pwm_ovsdb_choose_endpoint()) {
                return true;
            }
        }
    }

    return false;
}

bool pwm_ovsdb_reset(void)
{
    if (!pwm_ovsdb_is_enabled()) {
        return true;
    }

    return true;
}

bool pwm_ovsdb_update_state_table(PWM_State_Table_Member pwm_state_member,
                                  bool action_set, void* ptr_value)
{
    bool gre_tunnel_status = false;

    switch (pwm_state_member)
    {
        case PWM_STATE_TABLE_ENABLED:
            if (action_set)
            {
                int *ptr_int;
                ptr_int = ptr_value;
                SCHEMA_SET_INT(public_wifi_state.enabled, ptr_int);
                LOGI("Public Wifi State Update enabled = %d", public_wifi_state.enabled);
            }
            else
            {
                SCHEMA_UNSET_FIELD(public_wifi_state.enabled);
            }
            break;

        case PWM_STATE_TABLE_KEEP_ALIVE:
            if (action_set)
            {
                int *ptr_int;
                ptr_int = ptr_value;
                SCHEMA_SET_INT(public_wifi_state.keepalive_interval, ptr_int[0]);
                LOGI("Public Wifi State Update keepalive_interval = %d", public_wifi_state.keepalive_interval);
            }
            else
            {
                SCHEMA_UNSET_FIELD(public_wifi_state.keepalive_interval);
            }
            break;

        case PWM_STATE_TABLE_TUNNEL_IFNAME:
            if (action_set)
            {
                char *ptr_char;
                ptr_char = ptr_value;
                SCHEMA_SET_STR(public_wifi_state.tunnel_ifname, ptr_char);
                LOGI("Public Wifi State Update tunnel_ifname = %s", public_wifi_state.tunnel_ifname);
            }
            else
            {
                SCHEMA_UNSET_FIELD(public_wifi_state.tunnel_ifname);
            }
            break;

        case PWM_STATE_TABLE_VIF_IFNAMES:
            if (action_set)
            {
                int i;
                char **ptr_chat_ptr;

                ptr_chat_ptr = ptr_value;
                for(i = 0; i < 8; i++)
                {
                    if (ptr_chat_ptr[i] == NULL)
                        break;
                    SCHEMA_VAL_APPEND(public_wifi_state.vif_ifnames, ptr_chat_ptr[i]);
                }
            }
            else
            {
                SCHEMA_UNSET_MAP(public_wifi_state.vif_ifnames);
            }
            break;

        case PWM_STATE_TABLE_VLAN_ID:
            if (action_set)
            {
                int *ptr_int;
                ptr_int = ptr_value;
                SCHEMA_SET_INT(public_wifi_state.vlan_id, ptr_int[0]);
                LOGI("Public Wifi State Update vlan_id = %d", public_wifi_state.vlan_id);
            }
            else
            {
                SCHEMA_UNSET_FIELD(public_wifi_state.vlan_id);
            }
            break;
        case PWM_STATE_TABLE_GRE_ENDPOINT:
            if (action_set)
            {
                char *ptr_char;
                ptr_char = ptr_value;
                SCHEMA_SET_STR(public_wifi_state.gre_endpoint, ptr_char);
                LOGI("Public Wifi State Update gre_endpoint = %s", public_wifi_state.gre_endpoint);
            }
            else
            {
                SCHEMA_UNSET_FIELD(public_wifi_state.gre_endpoint);
            }
            break;
        case PWM_STATE_TABLE_GRE_TUNNEL_STATUS_MSG:
            if (action_set)
            {
                char *ptr_char;
                ptr_char = ptr_value;
                SCHEMA_SET_STR(public_wifi_state.gre_tunnel_status_msg, ptr_char);
                LOGD("Public Wifi State Update: gre_tunnel_status_msg: %s", public_wifi_state.gre_tunnel_status_msg);
                if (memcmp(public_wifi_state.gre_tunnel_status_msg, "UP", sizeof(char)) == 0) {
                    gre_tunnel_status = true;
                }
                SCHEMA_SET_INT(public_wifi_state.gre_tunnel_status, gre_tunnel_status);
                LOGI("Public Wifi State Update gre_tunnel_status = %d", public_wifi_state.gre_tunnel_status);
            }
            else
            {
                SCHEMA_UNSET_FIELD(public_wifi_state.gre_tunnel_status_msg);
                SCHEMA_UNSET_FIELD(public_wifi_state.gre_tunnel_status);
            }
            break;

        default:
            return false;
            break;
    }

    return true;
}

void callback_Wifi_VIF_Config(ovsdb_update_monitor_t *mon,
                              struct schema_Wifi_VIF_Config *old_rec,
                              struct schema_Wifi_VIF_Config *conf)
{
    const char *sec_encription;
    const char *radius_server_ip;
    const char *radius_server_port;
    const char *radius_server_ip_sec;
    const char *radius_server_port_sec;
    char port_string[32];
    int ret;

    if (pwm_ovsdb_is_enabled())
    {
        if (strcmp(conf->if_name, pwm_local_config.vif_ifnames[0]) == 0)
        {
            if (conf->enabled == true)
            {
                sec_encription = SCHEMA_KEY_VAL(conf->security, "encryption");
                if (strcmp(sec_encription, "WPA-EAP") == 0)
                {
                    pwm_firewall_del_rs_rules();

                    LOGD("ADD WIFI NETFILTER for %s", conf->if_name);

                    radius_server_ip = SCHEMA_KEY_VAL(conf->security, "radius_server_ip");
                    radius_server_port = SCHEMA_KEY_VAL(conf->security, "radius_server_port");
                    radius_server_ip_sec = SCHEMA_KEY_VAL(conf->security, "radius_server_ip_sec");
                    radius_server_port_sec = SCHEMA_KEY_VAL(conf->security, "radius_server_port_sec");

                    if (radius_server_ip[0]) {
                        snprintf(port_string, (sizeof(port_string) - 1), "%s", radius_server_port);
                        LOGD("ADD WIFI NETFILTER for %s, ip = %s port = %s", conf->if_name, radius_server_ip, port_string);
                        pwm_firewall_add_rs_rules(radius_server_ip, port_string, PWM_NETFILER_RS_TX);
                    }

                    if (radius_server_ip_sec[0]) {
                        snprintf(port_string, (sizeof(port_string) - 1), "%s", radius_server_port_sec);
                        LOGD("ADD WIFI NETFILTER for %s, ip = %s port = %s", conf->if_name, radius_server_ip_sec, port_string);
                        pwm_firewall_add_rs_rules(radius_server_ip_sec, port_string, PWM_NETFILER_RS_TX_SEC);
                    }
                }
            }
        }

        // Create Relay option conf
        ret = pwm_dhcp_relay_create_options(&pwm_local_config);
        if (!ret) {
            LOGE("Create Relay option conf: Creation failed");
        }
    }
}

void callback_Public_Wifi_Config(ovsdb_update_monitor_t *mon,
                                 struct schema_Public_Wifi_Config *old_rec,
                                 struct schema_Public_Wifi_Config *conf)
{
    int rc;
    int gre_ip_address_found;
    bool ret = true;

    MEM_SET(&public_wifi_state, 0, sizeof(struct schema_Public_Wifi_State));
    g_tunnel_gre_status_msg = NULL;
    g_gre_endpoint = NULL;

    pwm_ovsdb_set_pw_enable(conf->enabled);
    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            // Create Relay option conf
            ret = pwm_dhcp_relay_create_options(conf);
            if (!ret) {
                LOGE("Create Relay option conf: Creation failed");
            }

            memcpy(&pwm_local_config, conf, sizeof(struct schema_Public_Wifi_Config));
            if ((old_rec->enabled == false) && (conf->enabled == true))
            {
                snprintf( conf->tunnel_ifname, (sizeof(conf->tunnel_ifname) -1), "%s", "gre-pw" );
                // Setting pre_gre_endpoint has higher priority
                // over the search for FQDN
                gre_ip_address_found = 1;
                if (conf->gre_endpoint[0] == 0)
                {
                    ret = pwm_ovsdb_configure_endpoint();
                    if (!ret) {
                        gre_ip_address_found = 0;
                        g_tunnel_gre_status_msg = pwm_ovsdb_update_gre_tunnel_status(GRE_tunnel_DOWN_FQDN);
                        LOGE("[%s] gre_status_msg = %s ", __func__, g_tunnel_gre_status_msg);
                    }
                }
                if (gre_ip_address_found)
                {
                    ret = pwm_tunnel_update(conf);
                    if (!ret) {
                        LOGE("PWM OVSDB event: update PWM tunnel failed");
                    }
                }
            }
            else if ((old_rec->enabled == true) && (conf->enabled == false))
            {
                ret = pwm_tunnel_update(NULL);
                if (!ret) {
                    LOGE("PWM OVSDB event: delete PWM tunnel failed");
                }
            }

            pwm_ovsdb_update_state_table(PWM_STATE_TABLE_ENABLED, true, &conf->enabled);
            pwm_ovsdb_update_state_table(PWM_STATE_TABLE_VLAN_ID, true, &conf->vlan_id);
            pwm_ovsdb_update_state_table(PWM_STATE_TABLE_TUNNEL_IFNAME, true, &conf->tunnel_ifname);
            pwm_ovsdb_update_state_table(PWM_STATE_TABLE_KEEP_ALIVE, true, &conf->keepalive_interval);
            if (g_gre_endpoint != NULL)
            {
                pwm_ovsdb_update_state_table(PWM_STATE_TABLE_GRE_ENDPOINT, true, g_gre_endpoint);
            }
            if (g_tunnel_gre_status_msg != NULL)
            {
                pwm_ovsdb_update_state_table(PWM_STATE_TABLE_GRE_TUNNEL_STATUS_MSG, true, g_tunnel_gre_status_msg);
            }

            public_wifi_state._partial_update = true;

            rc = ovsdb_table_upsert(&table_Public_Wifi_State, &public_wifi_state, false);
            if (!rc) {
                LOGE("Upsert/Insert of table_Public_Wifi_State failed");
            }
            break;

        case OVSDB_UPDATE_DEL:
            ret = pwm_tunnel_update(NULL);
            if (!ret) {
                LOGE("PWM OVSDB event: delete PWM tunnel failed");
            }

            rc = ovsdb_table_delete(&table_Public_Wifi_State, &public_wifi_state);
            if (!rc) {
                LOGE("Delete table_Public_Wifi_State failed");
            }
            break;

        case OVSDB_UPDATE_ERROR:
        default:
            LOGI("PWM - callback table modify = [%d]", mon->mon_type);
            break;
    }
}

bool pwm_ovsdb_init(void)
{
    LOGI("Initializing PWM OVSDB tables...");

    OVSDB_TABLE_INIT_NO_KEY(Public_Wifi_Config);
    OVSDB_TABLE_INIT_NO_KEY(Public_Wifi_State);
    OVSDB_TABLE_INIT_NO_KEY(Wifi_Inet_Config);
    OVSDB_TABLE_INIT_NO_KEY(Wifi_Inet_State);
    OVSDB_TABLE_INIT_NO_KEY(DHCP_Option);
    OVSDB_TABLE_INIT(IP_Interface, name);
    OVSDB_TABLE_INIT_NO_KEY(IPv6_Address);
    OVSDB_TABLE_INIT_NO_KEY(DHCPv6_Client);
    OVSDB_TABLE_INIT_NO_KEY(Wifi_VIF_Config);

    OVSDB_TABLE_MONITOR(Public_Wifi_Config, false);
    OVSDB_TABLE_MONITOR(Wifi_VIF_Config, false);

    return true;
}
