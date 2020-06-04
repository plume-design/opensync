/*
* Copyright (c) 2020, Charter, Inc.
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

#include "../inc/pwm_DHCPrelay.h"

#include <pwm.h>
#include <os.h>
#include <log.h>
#include <stdio.h>
#include <stdlib.h>
#include "daemon.h"


static daemon_t  opt82_proc;

/******************************************************************************/
/* Get hardware address of WAN of gateway                                     */
/******************************************************************************/
static void getMacAddress(const char *iface, char *macaddr)
{
	int fd;

	struct ifreq ifr;
	char *mac;

	fd = socket(AF_INET, SOCK_DGRAM, 0);

	ifr.ifr_addr.sa_family = AF_INET;
	strncpy((char *)ifr.ifr_name , (const char *)iface , IFNAMSIZ-1);

	ioctl(fd, SIOCGIFHWADDR, &ifr);

	close(fd);

	mac = (char *)ifr.ifr_hwaddr.sa_data;

	//Copy mac address
	sprintf((char *)macaddr,(const char *)"%.2x:%.2x:%.2x:%.2x:%.2x:%.2x" , mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

}

/******************************************************************************/
/* Callback of DHCP option82 process                                          */
/******************************************************************************/
static bool pwm_DHCP_option82_at_exit( daemon_t *deamon )
{
	if( deamon->dn_exit_status == 0 )
	{
		LOGI(" Process DHCPoption82 clean exit");
	}
	else
		LOGE("DHCP option82 process terminated with status :%d",deamon->dn_exit_status );

	return true;
}

/******************************************************************************/
/* Generate DHCP option 82 process configuration file                         */
/******************************************************************************/
static bool pwm_gen_DHCP_Opt82_conf(struct schema_PublicWiFi* conf)
{
	char hwaddr[HW_ADDR_SIZE]={0};
	FILE *cfg = fopen (DHCP_OPTION82_CONF, "w");
	if (!cfg) {
		LOGE("Failed to create dhcp-option82 conf file");
		return -1;
	}
	getMacAddress(PWM_WAN_IF_NAME,hwaddr);

	if(strlen(hwaddr))
	{
		char securityMode = 's';
		fprintf(cfg, "intf:%s\n", (const char *)PWM_WAN_IF_NAME);
		fprintf(cfg, "circuitid:%s@%s;%c\n", (const char *)hwaddr, (const char *)conf->ssid,securityMode);
		fprintf(cfg, "remoteid:%s\n",(const char *)REMOTEID_CLIENT_MAC_ADDR);
	}

	fclose(cfg);
	return true;
}

/******************************************************************************/
/* Enable firewall rule and needed library for DHCP option82 process funtion  */
/******************************************************************************/
static bool pwm_set_DHCP_Opt82_config(void)
{
	int err=0;
	char cmd[256];

	err = pwm_add_pktroute_to_q_rule(DHCP_NFQ_RULE,PWM_BR_IF_NAME);
	if (0 == err) {
		LOGE("DHCPrelay: Packet divert rule failed :%s ", DHCP_NFQ_RULE);
		return false;
	}

	memset(cmd,0,sizeof(cmd));
	snprintf(cmd, sizeof(cmd) - 1, "sysctl -w net.bridge.bridge-nf-call-iptables=1");
	cmd[sizeof(cmd) - 1] = '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("DHCPrelay: bridge-nf enable failed: %s",__func__);
		return false;
	}

	return true;
}

/******************************************************************************/
/* Create and start DHCP option82 process                                     */
/******************************************************************************/
bool pwm_create_option82_proc(struct schema_PublicWiFi* conf)
{
	bool ret;

	ret = pwm_gen_DHCP_Opt82_conf(conf);
	if(!ret)
	{
		LOGE("Failed to generate DHCP opt82 configuration");
		return false;
	}

	ret = pwm_set_DHCP_Opt82_config();
	if(!ret)
	{
		LOGE("Failed to generate DHCP opt82 configuration");
		return false;
	}

	ret = daemon_init(&opt82_proc, DHCP_OPTION82_BIN, DAEMON_LOG_ALL );
	if(!ret)
	{
		LOGE("DHCP opt82 process initialization failed");
		return false;
	}
	ret = daemon_arg_reset( &opt82_proc );
	if(!ret)
	{
		LOGE("DHCP opt82 process reset argument failed");
		return false;
	}
	ret = daemon_atexit( &opt82_proc, pwm_DHCP_option82_at_exit );
	if(!ret)
	{
		LOGE("DHCP opt82 process set callback failed ");
		return false;
	}
	ret = daemon_start( &opt82_proc );
	if( !ret)
	{
		LOGE("DHCP opt82 process start failed");
		return false;
	}

	return true;
}

/******************************************************************************/
/* Stop DHCP option 82 process and remove it its configuration                */
/******************************************************************************/
bool pwm_remove_option82_proc(void)
{
	int err=0;
	char cmd[256];

	err = daemon_stop( &opt82_proc );
	if( 0 == err)
	{
		LOGE("DHCP opt82 process stop failed");
		return false;
	}

	err = pwm_del_pktroute_to_q_rule(DHCP_NFQ_RULE);
	if (0 == err) {
		LOGE("DHCPrelay: Delete prerouting to queue rule failed :%s ", DHCP_NFQ_RULE);
		return false;
	}

	memset(cmd,0,sizeof(cmd));
	snprintf(cmd, sizeof(cmd) - 1, "sysctl -w net.bridge.bridge-nf-call-iptables=0");
	cmd[sizeof(cmd) - 1] = '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("DHCPrelay: bridge-nf disable failed: %s",__func__);
		return false;
	}

	memset(cmd,0,sizeof(cmd));
	snprintf(cmd, sizeof(cmd) - 1, "rm DHCP_OPTION82_CONF");
	cmd[sizeof(cmd) - 1] = '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("DHCPrelay: Delete config file failed: %s",__func__);
		return false;
	}

	return true;

}

