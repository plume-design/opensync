/*
* Copyright (c) 2019, Sagemcom.
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
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <arpa/inet.h>

#include "log.h"
#include "os.h"
#include "target.h"
#include "json_util.h"
#include "ovsdb.h"
#include "schema.h"
#include "os.h"
#include "daemon.h"
#include "ovsdb_sync.h"

#define SOFTWARE_VERSION_SIZE_MAX 64
#define MODEL_NAME_SIZE_MAX 16
#define SERIAL_NUMBER_SIZE_MAX 32
#define DHCP_OPTION_VENDOR_IDENTIFYING_VENDOR_SPECIFIC_MAX_SIZE		255
#define DHCPC_OPTIONS_MAX_SIZE 500
#define OPT17_MAX_SIZE (DHCP_OPTION_VENDOR_IDENTIFYING_VENDOR_SPECIFIC_MAX_SIZE - 5)
#define SUBOPTION125_ENTREPRISECODE  (0x0<<24)|(0x0<<16)|(0x26<<8)|(0xba<<0)
#define DHCP_OPTION_VSI_DEVICESERIALNUMBER_SUBOPTCODE	    4
#define DHCP_OPTION_VSI_DEVICEHARDWAREVERSION_SUBOPTCODE    5
#define DHCP_OPTION_VSI_DEVICESOFTWAREVERSION_SUBOPTCODE    6
#define DHCP_OPTION_VSI_BOOTLOADERVERSION_SUBOPTCODE        7
#define DHCP_OPTION_VSI_DEVICEMANUFACTUREROUI_SUBOPTCODE    8
#define DHCP_OPTION_VSI_DEVICEMODELNAME_SUBOPTCODE          9
#define DHCP_OPTION_VSI_DEVICEMANUFACTURER_SUBOPTCODE       10
#define DUID_LLT_TIME_EPOCHE 946681200 // first of january 2000 epoche from the linux standard epoche as request in the rfc8415
#define DUID_LLT_HEADER_SIZE 16
#define DUID_LLT_MESSAGE_SIZE 12
#define WAN_HD_ADDRESS_FILE "/sys/class/net/eth0/address"

static bool wano_dhcp_option_add_send_option(int tag_val,const char *value,char *parent, char *ver)
{
	json_t	*where = NULL;
    json_t  *pwhere = NULL;
    json_t  *row = NULL;
    bool ret = 0;

	// Insert client
	row = json_object();
	json_object_set_new(row, "type", json_string("tx"));
	json_object_set_new(row, "value", json_string(value));
	json_object_set_new(row, "tag", json_integer(tag_val));
	json_object_set_new(row, "version", json_string(ver));
	json_object_set_new(row, "enable", json_boolean("true"));

	//At present tag is the only value different between DHCPv4,DHCPv6 in DHCP_Option table.
    //May require to change condition here if needed to deal with same tag value between DHCPv4,v6 in future.
	where = ovsdb_where_simple_typed("tag",&tag_val, OCLM_INT);
	if(NULL == where){
		LOGE("Insert DHCP option condition NULL");
		return false;
	}
  if(0 == strncmp(ver,"v6",sizeof("v6")))
    pwhere = ovsdb_where_simple_typed(SCHEMA_COLUMN(DHCPv6_Client,request_address),"true", OCLM_BOOL);//ovsdb_tran_cond(OCLM_BOOL, "enable", OFUNC_EQ, "true");
  else
	  pwhere = ovsdb_where_simple_typed(SCHEMA_COLUMN(DHCPv4_Client,enable),"true", OCLM_BOOL);
	if(NULL == pwhere){
		LOGE("Update %s client filter NULL",parent);
		return false;
	}

	ret = ovsdb_sync_upsert_with_parent("DHCP_Option",
			where,
			row,
			NULL,
			parent,
			pwhere,
			"send_options");
	if (!ret) {
		LOGE("Updating %s client failed to insert entry",parent);
    }

	return true;
}

static int tohex(int nb)
{
	if (nb < 10)
		return '0' + nb;
	else
		return 'a' + nb - 10;
}

static void wano_dhcp_option_bin2hexstring(const uint8_t* bin, char* hex, int binLength)
{
	int i;
	for(i=0;i<binLength;i++)
	{
		*hex++ = tohex ((*bin >> 4) & 0xf);
		*hex++ = tohex (*bin++ & 0xf);
	}
	*hex = 0;
}

static bool wano_dhcp_option_add_raw_option_in_buffer(uint8_t *buffer, uint16_t *bufferLength, const uint16_t bufferMaxLength, const uint16_t tag, const uint16_t length, const uint8_t *data)
{
	uint16_t data_size = 4 + length;
	if ( (*bufferLength + data_size) > bufferMaxLength) {
		LOGE("Buffer is too small");
		return false;
	}
	*((uint16_t*)&buffer[*bufferLength]) = htons(tag);
	*((uint16_t*)(&buffer[*bufferLength + 2])) = htons(length);
	memcpy(&buffer[*bufferLength + 4], data, length);
	*bufferLength += data_size;
	return true;
}

static bool wano_dhcp_option_set_send_VIS(char* vendor_info, size_t size)
{
	uint8_t opt_17[DHCP_OPTION_VENDOR_IDENTIFYING_VENDOR_SPECIFIC_MAX_SIZE];
	uint16_t optlenfield = 0;
	char option[DHCPC_OPTIONS_MAX_SIZE];
	char SerialNumber[SERIAL_NUMBER_SIZE_MAX];
	char* HardwareVersion = "1.0";
	char SoftwareVersion[SOFTWARE_VERSION_SIZE_MAX];
	char* BootloaderVersion = "2017.09@sc-0.60.4";
	char* ManufacturerOUI = "C8CD72";
	char ModelName[MODEL_NAME_SIZE_MAX];
	char* Manufacturer = "Sagemcom";
	bool res;

	memset(opt_17, 0, sizeof(opt_17));

	// Set enterprise number to ADSL Forum-9914 at the first 4 bytes of the buffer
	*((int32_t*) opt_17) = htonl(SUBOPTION125_ENTREPRISECODE);

	/* setup sub option 4 serial number */
	if(target_serial_get(SerialNumber, sizeof(SerialNumber)))
	{
		memset(option, 0,DHCPC_OPTIONS_MAX_SIZE);
		snprintf(option,DHCPC_OPTIONS_MAX_SIZE,"%s",SerialNumber);
		res = wano_dhcp_option_add_raw_option_in_buffer(opt_17 + 4, &optlenfield, OPT17_MAX_SIZE, //Buffer description
		DHCP_OPTION_VSI_DEVICESERIALNUMBER_SUBOPTCODE, strlen(SerialNumber),(const uint8_t*)option); //Type Length Value formation 
		if(!res)
		{
			return false;
		}
	}

	/* setup sub option 5 Hardware Version */
	memset(option, 0,DHCPC_OPTIONS_MAX_SIZE);
	snprintf(option,DHCPC_OPTIONS_MAX_SIZE,"%s",HardwareVersion);
	res = wano_dhcp_option_add_raw_option_in_buffer(opt_17 + 4, &optlenfield, OPT17_MAX_SIZE, //Buffer description
	DHCP_OPTION_VSI_DEVICEHARDWAREVERSION_SUBOPTCODE, strlen(HardwareVersion),(const uint8_t*)option); //Type Length Value formation 

	if(!res)
	{
		return false;
	}

	/* setup sub option 6 Software Version */
	if(target_sw_version_get(SoftwareVersion,sizeof(SoftwareVersion)))
	{
		memset(option, 0,DHCPC_OPTIONS_MAX_SIZE);
		snprintf(option,DHCPC_OPTIONS_MAX_SIZE,"%s",SoftwareVersion);
		res = wano_dhcp_option_add_raw_option_in_buffer(opt_17 + 4, &optlenfield, OPT17_MAX_SIZE, //Buffer description
		DHCP_OPTION_VSI_DEVICESOFTWAREVERSION_SUBOPTCODE, strlen(SoftwareVersion),(const uint8_t*)option); //Type Length Value formation 
		if(!res)
		{
			return false;
		}
	}

	/* setup sub option 7 Bootloader Version */
	memset(option, 0,DHCPC_OPTIONS_MAX_SIZE);
	snprintf(option,DHCPC_OPTIONS_MAX_SIZE,"%s",BootloaderVersion);
	res = wano_dhcp_option_add_raw_option_in_buffer(opt_17 + 4, &optlenfield, OPT17_MAX_SIZE, //Buffer description
	DHCP_OPTION_VSI_BOOTLOADERVERSION_SUBOPTCODE, strlen(BootloaderVersion),(const uint8_t*)option); //Type Length Value formation 

	if(!res)
	{
		return false;
	}

	/* setup sub option 8 Vendor OUI == ManufacturerOUI */
	memset(option, 0,DHCPC_OPTIONS_MAX_SIZE);
	snprintf(option,DHCPC_OPTIONS_MAX_SIZE,"%s",ManufacturerOUI);
	res = wano_dhcp_option_add_raw_option_in_buffer(opt_17 + 4, &optlenfield, OPT17_MAX_SIZE, //Buffer description
	DHCP_OPTION_VSI_DEVICEMANUFACTUREROUI_SUBOPTCODE, strlen(ManufacturerOUI),(const uint8_t*)option); //Type Length Value formation 

	if(!res)
	{
		return false;
	}

	/* setup sub option 9 Model name */
	if(target_model_get(ModelName,sizeof(ModelName)))
	{
		memset(option, 0,DHCPC_OPTIONS_MAX_SIZE);
		snprintf(option,DHCPC_OPTIONS_MAX_SIZE,"%s",ModelName);
		res = wano_dhcp_option_add_raw_option_in_buffer(opt_17 + 4, &optlenfield, OPT17_MAX_SIZE, //Buffer description
		DHCP_OPTION_VSI_DEVICEMODELNAME_SUBOPTCODE, strlen(ModelName),(const uint8_t*)option); //Type Length Value formation 

		if(!res)
		{
			return false;
		}
	}

	/* setup sub option 10 Hardware Version */
	memset(option, 0,DHCPC_OPTIONS_MAX_SIZE);
	snprintf(option,DHCPC_OPTIONS_MAX_SIZE,"%s",Manufacturer);
	res = wano_dhcp_option_add_raw_option_in_buffer(opt_17 + 4, &optlenfield, OPT17_MAX_SIZE, //Buffer description
	DHCP_OPTION_VSI_DEVICEMANUFACTURER_SUBOPTCODE, strlen(Manufacturer),(const uint8_t*)option); //Type Length Value formation 

	if(!res)
	{
		return false;
	}

	//opt_17[4] = optlenfield;
	optlenfield += 4;

	if(!res)
	{
		return false;
	}

	char str_opt17[optlenfield*3];
	wano_dhcp_option_bin2hexstring(opt_17, str_opt17, optlenfield);
	strncpy(vendor_info,str_opt17,size);
	return true;
}

static bool wano_dhcp_option_generate_duid(char* generated_duid, size_t size)
{

	u_int32_t time_val = time(NULL);
    FILE* file = NULL;
    int cx = 0;

    /* need to do %2^32 */
    while((time_val - DUID_LLT_TIME_EPOCHE ) > 4294967295u ){
        /* use 2^31 to avoid spurious compiler warnings */
        time_val -= 2147483648u;
        time_val -= 2147483648u;
    }
    time_val -= DUID_LLT_TIME_EPOCHE ;

    cx = snprintf(generated_duid,size, "00010001%08x",time_val);

    if( cx < 0 && cx != DUID_LLT_HEADER_SIZE + 1 ){
        LOGE("%s:Error in creating header of DUID_LLT (generated duid string size may be smaller)",__func__);
        return false;
    }

    cx = 0;
    /* Find an interface with a hardware address. */
    if ((file = fopen(WAN_HD_ADDRESS_FILE, "rb")) != NULL){

        int c = 0;
        while(cx >= 0 && (c = fgetc(file)) != EOF){

            if(c == ':' || c == '\n') /* semicolon or LF  */
                continue;

            if ((c>='0' && c<='9') || (c>='a' && c<='f') ) {
                unsigned int len = strnlen(generated_duid,size );

                if(len + 1 >= size) {
                    cx = -2;
                    break;
                }

                generated_duid[len] = (char)c;
                generated_duid[len +1] = '\0';

            } else {
                /* error detected */
                cx = -1;
            }
        }

        fclose(file);

        if (cx < 0){
            LOGE("%s:Error %d in parsing the file %s may not contain an HD address or found wrong char %d",__func__,cx,WAN_HD_ADDRESS_FILE, (char)c );
            return false;
        }

        if (DUID_LLT_MESSAGE_SIZE + DUID_LLT_HEADER_SIZE != strnlen(generated_duid,DUID_LLT_MESSAGE_SIZE + DUID_LLT_HEADER_SIZE + 1)){
            LOGE("%s:Error generated DUID message don't have the good size : %s",__func__,generated_duid );
            return false;
        }
    } else {
        LOGE("%s: can't read file %s",__func__,WAN_HD_ADDRESS_FILE);
        return false;
    }
    return true;
}

bool wano_dhcp_option_init(void)
{
    char vendor_info[1024] = {0};
    char client_duid_llt[1024] = {0};

    if(true == wano_dhcp_option_set_send_VIS(vendor_info,sizeof(vendor_info)))
    {
        if(false == wano_dhcp_option_add_send_option(125,vendor_info,"DHCPv4_Client","v4"))
        {
            LOGE("%s: DHCPv4 option entry failed",__func__);
            return false;
        }

        if(false == wano_dhcp_option_add_send_option(17,vendor_info,"DHCPv6_Client","v6"))
        {
            LOGE("%s: DHCPv6 option entry failed",__func__);
            return false;
        }
    }else{
        LOGE("%s: DHCP Vendor specific information option failed",__func__);
        return false;
    }

    if(true == wano_dhcp_option_generate_duid(client_duid_llt,sizeof(client_duid_llt)))
    {
        if(false == wano_dhcp_option_add_send_option(1,client_duid_llt,"DHCPv6_Client","v6"))
        {
            LOGE("%s: DHCPv6 option entry failed",__func__);
            return false;
        }
    }else{
        LOGE("%s: DHCP DUID_LLT generation failed",__func__);
        return false;
    }

    return true;
}
