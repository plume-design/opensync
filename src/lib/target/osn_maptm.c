/*
* Copyright (c) 2020, Sagemcom.
* All rights reserved.
*
* The information and source code contained herein is the exclusive property of
* Sagemcom and may not be disclosed, examined, or reproduced in whole or in part
* without explicit written authorization of Sagemcom.
*/

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include "target.h"
#include "osn_maptm.h"
#include "log.h"

#ifdef MAPTM_DEBUG
#undef LOGI
#define LOGI    printf
#define LOGA    printf
#define LOGE    printf
#endif

bool osn_mapt_configure(const char* brprefix, int ratio, const char* intfname, 
							const char* wanintf, const char* IPv6prefix, const char* subnetcidr4, 
							const char* ipv4PublicAddress, int PSIDoffset, int PSID);
bool osn_mapt_stop();


bool osn_mapt_configure(const char* brprefix, int ratio, const char* intfname, 
							const char* wanintf, const char* IPv6prefix, const char* subnetcidr4, 
							const char* ipv4PublicAddress, int PSIDoffset, int PSID) {
	char cmd[MAPTM_CMD_LEN]={0x0};

    if (brprefix==NULL || intfname==NULL || wanintf==NULL || IPv6prefix==NULL || subnetcidr4==NULL || ipv4PublicAddress==NULL)
	{
	    LOG(ERR, "map-t: %s: Invalid parameter.", intfname);
	    return false;
	}
	
		snprintf(cmd, MAPTM_CMD_LEN,"ivictl -r -d -P %s -R %d -T ", brprefix, ratio);
		/* We have to verify why cmd_log return false otherwise cmd is exc */
        LOGT("cmd:%s",cmd);
		cmd_log(cmd);

		snprintf(cmd, MAPTM_CMD_LEN, "ivictl -s -i %s -I %s -P %s -H -N -a %s -A %s -z %d -R %d -o %d -F 1 -T",
                                  intfname, wanintf, IPv6prefix, subnetcidr4, ipv4PublicAddress, PSIDoffset, 
                                  ratio, PSID);
		LOGT("cmd:%s",cmd);
		/* We have to verify why cmd_log return false otherwise cmd is exc */
        cmd_log(cmd);

		return true;
	
}

bool osn_mapt_stop() {
	char cmd[MAPTM_CMD_LEN]={0x0};
	snprintf(cmd, sizeof(cmd),"ivictl -q");
	if (cmd_log(cmd))
	{
		return true;
	}
	
	return false;
}
