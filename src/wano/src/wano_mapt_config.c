/* Copyright (c) 2020 Charter, Inc.
 *
 * This module contains unpublished, confidential, proprietary
 * material. The use and dissemination of this material are
 * governed by a license. The above copyright notice does not
 * evidence any actual or intended publication of this material.
 *
 * Created: 05 February 2020
 *
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

#include "evsched.h"
#include "log.h"
#include "os.h"
#include "ovsdb.h"
#include "evext.h"
#include "os_backtrace.h"
#include "json_util.h"
#include "target.h"
#include <stdint.h>
#include <linux/types.h>
#include <arpa/inet.h>
#include "ds_dlist.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "wano_mapt.h"
#include "osn_maptm.h"
/*Firewall Rules*/
#define V4_CHCEK_FORWARD "v4_check_forward"
#define V4_MAPT_CHCEK_FORWARD "v4_mapt_check_forward"
#define V4_MAPT_TCP_CHCEK_1 "v4_mapt_tcp_check_1"
#define V4_MAPT_TCP_CHCEK_2 "v4_mapt_tcp_check_2"
	

struct ovsdb_table table_Netfilter;

/*
 * Free MAPT struct
 */
bool wano_mapt_remove_maptStruct(struct mapt* mapt_rule)
{
   if(mapt_rule ==NULL)
      return true;
   if(mapt_rule->dmr !=NULL ) free(mapt_rule->dmr);
   if(mapt_rule->ipv6prefix !=NULL ) free(mapt_rule->ipv6prefix);
   if(mapt_rule->ipv4prefix !=NULL ) free(mapt_rule->ipv4prefix);
   free(mapt_rule);
   return true;
}
/*
 *  Free list node
 */
 
 bool wano_mapt_remove_list(ds_dlist_t rules)
{
   ds_dlist_iter_t iter;
   struct list_rules *node =NULL;

   for (node = ds_dlist_ifirst(&iter ,&rules) ; 
         node !=NULL ;
         node = ds_dlist_inext(&iter)){
            ds_dlist_iremove(&iter);
            free(node->value);
            free(node);
    }
    return true;

}
/*
 * Parce MAPT Rules
 */
struct mapt* parse_option_rule(char* rule)
{
   if(rule==NULL)
   {
      LOGE("MAP-T Rule is NULL");
      return NULL;
   }
   struct mapt* mapt_rule = malloc(sizeof(struct mapt));
   if(mapt_rule ==NULL){
      LOGE("Unable to allocate update handler!");
      return NULL;
   }
   char *p = strtok(rule, ",");
   while(p)
   {
      char *name;
      char *value;
      name = strsep(&p, "=");
      
      if (name == NULL)  continue;
      value=p;
      if (value == NULL)  continue;
         
      if(!strncmp(name,"ealen",sizeof("ealen"))){
         mapt_rule->ealen = atoi(value);
      }
      else if(!strncmp(name,"prefix4len",sizeof("prefix4len"))){
         mapt_rule->prefix4len = atoi(value);
      }
      else if(!strncmp(name,"prefix6len",sizeof("prefix6len"))){
         mapt_rule->prefix6len = atoi(value);
      }
      else if(!strncmp(name,"offset",sizeof("offset"))){
         mapt_rule->offset = atoi(value);
      }
      else if(!strncmp(name,"psidlen",sizeof("psidlen"))){
         mapt_rule->psidlen = atoi(value);
      }
      else if(!strncmp(name,"psid",sizeof("psid"))){
         mapt_rule->psid = atoi(value);
      }
      else if(!strncmp(name,"ipv4prefix",sizeof("ipv4prefix"))){
         mapt_rule->ipv4prefix= strndup(value,strlen(value));
         if(mapt_rule->ipv4prefix == NULL) {
            LOGE("Unable to allocate update handler!");
            goto free;
         }
      }
      else if(!strncmp(name,"ipv6prefix",sizeof("ipv6prefix"))){
         mapt_rule->ipv6prefix=strndup(value,strlen(value));
         if(mapt_rule->ipv6prefix == NULL) {
            LOGE("Unable to allocate update handler!");
            goto free;
         }
      }
      else if(!strncmp(name,"dmr",sizeof("dmr"))){
         mapt_rule->dmr=strndup(value,strlen(value));
         if(mapt_rule->dmr == NULL) {
            LOGE("Unable to allocate update handler!");
            goto free;
         }
      }
      p = strtok(NULL, ",");
   }
   return mapt_rule;
free:
   wano_mapt_remove_maptStruct(mapt_rule);
   return NULL;
}

/*
 * Compare Between the ipv6prefix of mapt rule and the IA-PD  
 */
int comparePrefix( const char *iapd_prefix, const char *mapt_prefix, int length)
{
   struct in6_addr iapd;
   struct in6_addr pref;

	if (!inet_pton(AF_INET6, iapd_prefix, &iapd)) {
		return 0;
	}

	if (!inet_pton(AF_INET6, mapt_prefix, &pref)) {
		return 0;
	}

	
	uint32_t mask;
	if (length == -1) {
		length = 128;
	}
	if ((length >= 0) && (length <= 32)) {
		mask = htonl((uint32_t)-1 << (32 - length));
		if ((iapd.s6_addr32[0] & mask) == (pref.s6_addr32[0] & mask)) {
			return 1;
		}
	} else if ((length >= 33) && (length <= 64)) {
		mask = htonl((uint32_t)-1 << (64 - length));
		if ((iapd.s6_addr32[0] == pref.s6_addr32[0]) &&
		    (iapd.s6_addr32[1] & mask) == (pref.s6_addr32[1] & mask)) {
			return 1;
		}
	} else if ((length >= 65) && (length <= 96)) {
		mask = htonl((uint32_t)-1 << (96 - length));
		if ((iapd.s6_addr32[0] == pref.s6_addr32[0]) &&
		    (iapd.s6_addr32[1] == pref.s6_addr32[1]) &&
		    (iapd.s6_addr32[2] & mask) == (pref.s6_addr32[2] & mask)) {
			return 1;
		}
	} else if ((length >= 97) && (length <= 128)) {
		mask = htonl((uint32_t)-1 << (128 - length));
		if ((iapd.s6_addr32[0] == pref.s6_addr32[0]) &&
		    (iapd.s6_addr32[1] == pref.s6_addr32[1]) &&
		    (iapd.s6_addr32[2] == pref.s6_addr32[2]) &&
		    (iapd.s6_addr32[3] & mask) == (pref.s6_addr32[3] & mask)) {
			return 1;
		}
	}
	return 0;
}

/*
 * Select Matched MAPT Rule 
 */
struct mapt*  get_Mapt_Rule(char* option95,char* iapd)
{
   LOGD(" Get MAP-t Rules");
   if (!option95)
		return NULL ;
   bool ret =false;
   char* mapt_option95 =NULL;
   mapt_option95 = option95;
   
   ds_dlist_t l_rules;
   ds_dlist_init(&l_rules, struct list_rules, d_node);
 
   /* Fill the List of MAPT Rules */
   char * rule = strtok(mapt_option95, " ");
   while( rule != NULL ) {
      struct list_rules *l_node;
      l_node = malloc(sizeof(struct list_rules));
      if(l_node == NULL)
      {
         LOGE("Unable to allocate update handler!");
         free(l_node);
         return NULL;
      }
      l_node->value=strndup(rule,strlen(rule));
      if(l_node->value == NULL) {
            LOGE("Unable to allocate update handler!");
            free(l_node);
            wano_mapt_remove_list(l_rules);
            return NULL;
      } 
      ds_dlist_insert_tail(&l_rules, l_node);
      rule = strtok(NULL, " ");
   }
   
   struct list_rules *node =NULL;
   ds_dlist_iter_t iter;
   struct mapt* mapt_rule =NULL;
   for (node = ds_dlist_ifirst(&iter ,&l_rules) ; 
         node !=NULL ;
         node = ds_dlist_inext(&iter)){
      
      mapt_rule = parse_option_rule(node->value);
      if(mapt_rule){
         ret = comparePrefix(iapd,mapt_rule->ipv6prefix,mapt_rule->prefix6len) ;
         if(ret){
            LOGD("MAPT rule foud");
            break ;
         }
         else 
         {
            wano_mapt_remove_maptStruct(mapt_rule);
         }
      }
    }
    
    if (ret) {
      wano_mapt_remove_list(l_rules);
      return mapt_rule;
    }
    return NULL;
}

/* Configure Map Domaine */
 
void configureMapDomain(char*iapd,int iapd_length, struct mapt* mapt_rule ){
    struct in6_addr addr6Wan;
    char ipv6PrefixHex[19];
    
    /*Set Domain PSID Lengh*/
    mapt_rule->domaine_psidlen = mapt_rule->ealen-( IPV4_ADDRESS_SIZE - mapt_rule->prefix4len);
    /*Set Ratio*/
    mapt_rule->ratio = (1 << mapt_rule->domaine_psidlen);
    
    /* Set Domaine PSSID */
    inet_pton(AF_INET6, iapd, &addr6Wan);
    snprintf(ipv6PrefixHex, 19, "0x%02x%02x%02x%02x%02x%02x%02x%02x",(int)addr6Wan.s6_addr[0], (int)addr6Wan.s6_addr[1], 
								(int)addr6Wan.s6_addr[2], (int)addr6Wan.s6_addr[3], (int)addr6Wan.s6_addr[4], (int)addr6Wan.s6_addr[5], (int)addr6Wan.s6_addr[6], (int)addr6Wan.s6_addr[7]); 
    uint64_t ipv6addr ;
    ipv6addr = strtoll (ipv6PrefixHex, NULL, 0);
    ipv6addr <<= mapt_rule->prefix6len ;
    ipv6addr >>= mapt_rule->prefix6len + (IPV6_PREFIX_MAX_SIZE - iapd_length) ;  
    uint32_t suffix = ipv6addr>>(mapt_rule->ealen-(IPV4_ADDRESS_SIZE-mapt_rule->prefix4len));
    if (!(IPV4_ADDRESS_SIZE-mapt_rule->prefix4len == mapt_rule->ealen)) {
        mapt_rule->domaine_pssid = ipv6addr&(~(suffix << (mapt_rule->ealen - (IPV4_ADDRESS_SIZE - mapt_rule->prefix4len))));
    }
    
    /* set Public IPv4 Address*/
    uint32_t swapped_suffix = ((suffix>>24)&0xff)|((suffix<<8)&0xff0000)|((suffix>>8)&0xff00)|((suffix<<24)&0xff000000);
    struct in_addr ipv4BinPrefixRule;
    inet_pton(AF_INET, mapt_rule->ipv4prefix, &ipv4BinPrefixRule);
    struct in_addr ipv4BinPublicAddress;
    ipv4BinPublicAddress.s_addr= ((uint32_t)ipv4BinPrefixRule.s_addr | swapped_suffix ); 
    inet_ntop(AF_INET, &ipv4BinPublicAddress, mapt_rule->ipv4PublicAddress , IPV4_ADDRESS_SIZE);
    
}

/* Get configuration of MAPT*/
struct mapt* wano_mapt_getconfigure(char* option95 ,char* iapd , int iapd_len)
{
	struct mapt* selected_rule = get_Mapt_Rule(option95,iapd);
	if(selected_rule== NULL)
    	return NULL;

    configureMapDomain(iapd,iapd_len,selected_rule);
    return selected_rule;
}

/*MAP-T Firewall Configuration*/
bool wano_mapt_ovsdb_nfm_set_rule(const char* rule , bool enable)
{
   	struct schema_Netfilter set;
	json_t *where = NULL;
	int rc = 0;

	memset(&set, 0, sizeof(set));
	set._partial_update = true;
	SCHEMA_SET_INT(set.enable, enable);

	where = ovsdb_where_simple(SCHEMA_COLUMN(Netfilter, name), rule);
	if (!where) {
		LOGE("[%s] Set NAT Netfilter rule: create filter failed", rule);
		return false;
	}

	rc = ovsdb_table_update_where(&table_Netfilter, where, &set);
	if (rc != 1) {
		LOGE("[%s] Set NAT Netfilter rule: unexpected result count %d", rule, rc);
		return false;
	}
	return true;
}
bool wano_mapt_ovsdb_nfm_rules(bool enable)
{
	LOGD("%s , config Firewall MAPT",__func__);
	if(!wano_mapt_ovsdb_nfm_set_rule(V4_CHCEK_FORWARD,!enable)) 
		return false ;
	if(!wano_mapt_ovsdb_nfm_set_rule(V4_MAPT_CHCEK_FORWARD,enable))
		return false ;
	if(!wano_mapt_ovsdb_nfm_set_rule(V4_MAPT_TCP_CHCEK_1,enable))
		return false;
	if(!wano_mapt_ovsdb_nfm_set_rule(V4_MAPT_TCP_CHCEK_2,enable))
		return false;
	
	return true;
}
bool stop_mapt()
{
	if(! wano_mapt_ovsdb_nfm_rules(false))
		LOGE("Unable to disable Firewall");
	if(! osn_mapt_stop())
		return false;
	LOGD("Stopped Mapt");	
	return true;
}

bool config_mapt()
{
   if( strucWanConfig.mapt_95_value[0] == '\0' ||  strucWanConfig.iapd[0]=='\0' ){
      LOGD("Unable to Configure MAPT Option");
      return false;
   }
   /*Hard Coded value : Must be changed*/
	char subnetcidr4[20]="192.168.1.1/24";
	
	/* Get Prifix IAPD and his Length */
	char iapd[256]="";
	int iapd_len=0;

	char* flag= NULL;
	snprintf(iapd,sizeof(iapd)-1,"%s",strucWanConfig.iapd);

	flag = strtok(iapd,",");
	if(flag ==NULL) 
      return false;
   flag = strtok(iapd,"/");
    if(flag != NULL)
	{
		flag = strtok(NULL, ",");
      if(flag == NULL)
      {
         LOGE("Unable to allocate update handler!");
         return false;
      }
      iapd_len = atoi(flag);
	}
   
	struct mapt* MaptConf=wano_mapt_getconfigure(strucWanConfig.mapt_95_value , iapd ,iapd_len);
   if(MaptConf == NULL){
      LOGE("Unable to get MAPT Rule");
      return false;
   }
   /* Show MAPT Configuration */
   LOGD("MAPT Rule for Mapt Configuration : ");
   
   /* Add the Length to the Prefix v4/v6*/
   char ipv6prefix[100] = "";
   char ipv4PublicAddress[100] = "";
   snprintf(ipv6prefix,sizeof(ipv6prefix)-1,"%s/%d",MaptConf->ipv6prefix,MaptConf->prefix6len);
   snprintf(ipv4PublicAddress,sizeof(ipv4PublicAddress)-1,"%s/%d",MaptConf->ipv4PublicAddress,MaptConf->prefix4len);
   /*Enable firewall*/
   if(!wano_mapt_ovsdb_nfm_rules(true))
   {
      LOGE("Unable to Config Firewall");
      wano_mapt_remove_maptStruct(MaptConf);
      return false;
   }
   
   /*Run target MAPT Congifuration*/
	if(osn_mapt_configure(MaptConf->dmr,
							MaptConf->ratio,
							"BR_LAN",
							"br-wan",
							ipv6prefix,
							subnetcidr4,
							ipv4PublicAddress,
							MaptConf->offset,
							MaptConf->domaine_pssid)){
                     
                        LOGD("MAPT CONFIGURED");
                        wano_mapt_remove_maptStruct(MaptConf);
                        
                        return true;
   }
   
   wano_mapt_remove_maptStruct(MaptConf);
   LOGE("MAPT NOT CONFIGURED");
   return false;
}
