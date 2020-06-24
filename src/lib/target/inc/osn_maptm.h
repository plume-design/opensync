/*
* Copyright (c) 2020, Sagemcom.
* All rights reserved.
*
* The information and source code contained herein is the exclusive property of
* Sagemcom and may not be disclosed, examined, or reproduced in whole or in part
* without explicit written authorization of Sagemcom.
*/

#ifndef OSN_MAPTM_H_INCLUDED
#define OSN_MAPTM_H_INCLUDED

#include "schema.h"

#define MAPTM_CMD_LEN               256

/**
* @brief Initialize map-t module
* @return true on success
*/
bool osn_mapt_init(void);                       //Initialize MAP-T functionality 

/**
* @brief Configure and start MAP-T module 
* @param[in] brprefix string value of border relay prefix
* @param[in] ratio int value of address sharing ratio
* @param[in] intfname string value of interface name
* @param[in] wanintf string value of wan interface name
* @param[in] IPv6prefix string value of the IPv6 prefix
* @param[in] subnetcidr4 string value of CIDR subnet 
* @param[in] ipv4PublicAddress string value of IPv4 Shared Address  
* @param[in] PSIDoffset type of value of PSID offset
* @param[in] PSID type of value of PSID

* @return true on success
*/
bool osn_mapt_configure(const char* brprefix,     //Configure and start MAP-T module   
						   int ratio, 
						   const char* intfname, 
						   const char* wanintf, 
						   const char* IPv6prefix, 
						   const char* subnetcidr4, 
						   const char* ipv4PublicAddress, 
						   int PSIDoffset, 
						   int PSID);

/**
* @brief Stop map-t module
* @return true on success
*/						   
bool osn_mapt_stop();                             //Stop MAP-T functionality 

#endif /* OSN_MAPTM_H_INCLUDED */

