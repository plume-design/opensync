#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_cache.h"
#include "schema.h"
#include "log.h"
#include "json_util.h"

#include "nm2_ripv2.h"

#define NM2_RIPV2_POSTROUTING_CHAIN "NFM_POSTROUTING"
#define NM2_RIPV2_POSTROUTING_RULE_NAME "v4_ripv2_nfm_postrouting"
#define NM2_RIPV2_PRIVATE_SNAT_RULE_NAME "v4_ripv2_snat_private"
#define NM2_RIPV2_PUBLIC_NAT_RULE_NAME "v4_ripv2_bypassnat_public"

#define TARGET_BUFFER 256
#define TARGET_PREFIX "--to-source"
#define LAN_SUBNET "192.168.1.0/24"

static ovsdb_table_t table_Netfilter;
static ovsdb_table_t table_RIPv2_Interface_State;
static ovsdb_table_t table_Wifi_Inet_State;

bool nm2_ripv2_nfm_init_chain()
{
    struct schema_Netfilter conf = {0};
    char snatChainRule[TARGET_BUFFER] = {0};

    schema_Netfilter_mark_all_present(&conf);
    conf._partial_update = true;

    snprintf(snatChainRule, sizeof(snatChainRule), "-o %s", CONFIG_TARGET_WAN_BRIDGE_NAME);

    STRSCPY(conf.name, NM2_RIPV2_POSTROUTING_RULE_NAME);
    conf.enable = true;
    STRSCPY(conf.protocol, "ipv4");
    STRSCPY(conf.table, "nat");
    STRSCPY(conf.chain, "POSTROUTING");
    conf.priority = 0;
    STRSCPY(conf.rule, snatChainRule);
    STRSCPY(conf.target, NM2_RIPV2_POSTROUTING_CHAIN);

    if(!ovsdb_table_upsert(&table_Netfilter, &conf, false))
    {
        LOG(ERR, "nm2_ripv2_nfm: %s : Unable to update Netfilter.",
                NM2_RIPV2_POSTROUTING_RULE_NAME);
        return false;
    }

    return true;
}

bool nm2_ripv2_nfm_rules_add(void)
{
    struct schema_Netfilter conf = {0};
    char snat[TARGET_BUFFER] = "SNAT";
    char snatChainRule[TARGET_BUFFER] = {0};

    struct schema_Wifi_Inet_State inet_state = {0};
    char rip_addr[19] = {0};

    struct schema_RIPv2_Interface_State *intf_state;
    void *conf_p;
    int count = 0;
    int i;

    schema_Netfilter_mark_all_present(&conf);
    conf._partial_update = true;

    conf_p = ovsdb_table_select_where(&table_RIPv2_Interface_State,
                                      NULL,
                                      &count);
    if (!count)
        return;
    for (i = 0; i < count; i++) {
        intf_state = (struct schema_RIPv2_Interface_State *) (conf_p + table_RIPv2_Interface_State.schema_size * i);

        if (intf_state->enable == false)
            return;
        MEMZERO(snatChainRule);
        snprintf(snatChainRule, sizeof(snatChainRule), "-s %s", intf_state->network);
    
        snprintf(conf.name, sizeof(conf.name), "%s%d", NM2_RIPV2_PUBLIC_NAT_RULE_NAME, i+1);
        conf.enable = true;
        STRSCPY(conf.protocol, "ipv4");
        STRSCPY(conf.table, "nat");
        STRSCPY(conf.chain, NM2_RIPV2_POSTROUTING_CHAIN);
        conf.priority = 8;
        STRSCPY(conf.rule, snatChainRule);
        STRSCPY(conf.target, "ACCEPT");
    
        if(!ovsdb_table_upsert(&table_Netfilter, &conf, false))
        {
            LOG(ERR, "nm2_ripv2_nfm: %s%d : Unable to update Netfilter.",
                    NM2_RIPV2_PUBLIC_NAT_RULE_NAME, i+1);
            return false;
        }
    }
    if (conf_p)
        FREE(conf_p);


    if (!ovsdb_table_select_one(&table_Wifi_Inet_State, SCHEMA_COLUMN(Wifi_Inet_State, if_name), CONFIG_STATIC_IP_LAN_INTF1, &inet_state))
    {
        LOG(ERR, "nm2_ripv2_nfm: if_name == %s : Unable to select Wifi_Inet_State.",
                CONFIG_STATIC_IP_LAN_INTF1);
        return false;
    }
    STRSCPY(rip_addr, inet_state.inet_addr);
    
    MEMZERO(snatChainRule);
    snprintf(snatChainRule, sizeof(snatChainRule), "-s %s %s %s", LAN_SUBNET, TARGET_PREFIX, rip_addr);

    STRSCPY(conf.name, NM2_RIPV2_PRIVATE_SNAT_RULE_NAME);
    conf.enable = true;
    STRSCPY(conf.protocol, "ipv4");
    STRSCPY(conf.table, "nat");
    STRSCPY(conf.chain, NM2_RIPV2_POSTROUTING_CHAIN);
    conf.priority = 8;
    STRSCPY(conf.rule, snatChainRule);
    STRSCPY(conf.target, snat);

    if(!ovsdb_table_upsert(&table_Netfilter, &conf, false))
    {
        LOG(ERR, "nm2_ripv2_nfm: %s : Unable to update Netfilter.",
                NM2_RIPV2_PRIVATE_SNAT_RULE_NAME);
        return false;
    }

    LOG(DEBUG, "nm2_ripv2_nfm: Update Netfilter successful.");

    return true;
}

bool nm2_ripv2_nfm_rules_del(void)
{
    json_t *where = NULL;

    if((where = ovsdb_where_simple(SCHEMA_COLUMN(Netfilter, chain), NM2_RIPV2_POSTROUTING_CHAIN)))
    {
        LOG(DEBUG, "nm2_ripv2_nfm: Removing RIPv2 nat rules: %s",
                  NM2_RIPV2_POSTROUTING_CHAIN);
        ovsdb_table_delete_where(&table_Netfilter, where);
    }

    return true;
}

bool nm2_ripv2_nfm_init(void)
{
    LOGI("nm2_ripv2_nfm: Initializing OVSDB tables");
    OVSDB_TABLE_INIT(Netfilter, name);
    OVSDB_TABLE_INIT_NO_KEY(RIPv2_Interface_State);
    OVSDB_TABLE_INIT_NO_KEY(Wifi_Inet_State);

    if(false == nm2_ripv2_nfm_init_chain())
        LOG(ERR, "nm2_ripv2_nfm: Initializing RIPv2 nfm chain failed");

    return true;
}
