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

#include <pwm_utils.h>
#include <log.h>
#include <string.h>
#include <arpa/inet.h>

#define MODULE_ID LOG_MODULE_ID_COMMON

bool pwm_is_ipv6_gua_addr(const char *addr)
{
	struct in6_addr addr_bin;
	char addr_full_str[INET6_ADDRSTRLEN + 10];
	const char *addr_str = NULL;

	strncpy(addr_full_str, addr, sizeof(addr_full_str) - 1);
	addr_full_str[sizeof(addr_full_str) - 1] = '\0';

	addr_str = strtok(addr_full_str, "/");
	if (!addr_str) {
		LOGE("Is IPv6 GUA address: invalid address %s", addr);
		return false;
	}

	if (!inet_pton(AF_INET6, addr_str, &addr_bin)) {
		LOGE("Is IPv6 GUA address: invalid address %s", addr);
		return false;
	}

	/* GUA is prefix 2000::/3 */
	if ((addr_bin.s6_addr[0] & 0xE0) == 0x20) {
		return true;
	}
	return false;
}

int pwm_get_addr_family(const char *addr)
{
	struct in_addr tmp4;
	struct in6_addr tmp6;
	int err = -1;

	if (!addr) {
		return AF_UNSPEC;
	}

	err = inet_pton(AF_INET, addr, &tmp4);
	if (err == 1) {
		return AF_INET;
	}

	err = inet_pton(AF_INET6, addr, &tmp6);
	if (err == 1) {
		return AF_INET6;
	}

	return AF_UNSPEC;

}

