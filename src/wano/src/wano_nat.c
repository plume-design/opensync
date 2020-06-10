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

#include "wano_nat.h"
#include "wano_ovsdb.h"
#include "log.h"
#include "ovsdb_sync.h"
#include <string.h>
#include <stdio.h>



/*
* Update the table Netfilter row WANO_NAT_NF_NAME.
* Enable or disable it.
* Set the IP address of the WAN interface to use for the source NAT.
*/
static bool wano_nat_set_nf_rule(bool enable, const char *ip)
{
	struct schema_Netfilter set;
	char target[64];
	json_t *where = NULL;
	int rc = 0;

	memset(&set, 0, sizeof(set));
	set._partial_update = true;
	SCHEMA_SET_INT(set.enable, enable);
	snprintf(target, sizeof(target) - 1, "SNAT --to-source %s", ip);
	target[sizeof(target) - 1] = '0';
	SCHEMA_SET_STR(set.target, target);

	where = ovsdb_where_simple(SCHEMA_COLUMN(Netfilter, name), WANO_NAT_NF_NAME);
	if (!where) {
		LOGE("[%s] Set NAT Netfilter rule: create filter failed", WANO_NAT_NF_NAME);
		return false;
	}

	rc = ovsdb_table_update_where(&table_Netfilter, where, &set);
	if (rc != 1) {
		LOGE("[%s] Set NAT Netfilter rule: unexpected result count %d", WANO_NAT_NF_NAME, rc);
		return false;
	}
	return true;
}

/*
* Do nothing if it is not related to the WAN interface.
* Otherwise, check if the address is valid (not empty, not zeroed) and update
* the NAT rule.
*/
bool wano_nat_nm2_is_modified(struct schema_Wifi_Inet_State *conf)
{
	bool errcode = true;
	bool enable = false;

	if (strncmp(conf->if_name, WANO_IFC_WAN, sizeof(WANO_IFC_WAN))) {
		return true;
	}

	if (conf->inet_addr[0] && strncmp(conf->inet_addr, "0.0.0.0", sizeof(conf->inet_addr))) {
		enable = true;
	}

	errcode = wano_nat_set_nf_rule(enable, conf->inet_addr);
	if (!errcode) {
		LOGE("[%s] Disable the Netfilter rule failed", WANO_NAT_NF_NAME);
		return false;
	}
	return true;
}

bool wano_nat_init(void)
{
	return true;
}

