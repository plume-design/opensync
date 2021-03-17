/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "osn_dhcp.h"

typedef struct opt_info
{
    int code;
    const char *name;
} opt_info_t;

/* REQ options available via enum osn_dhcp_option, supported by udhcp client */
static const opt_info_t udhcp_public_opts[] =
{
    { .code = DHCP_OPTION_SUBNET_MASK,      .name = "subnet" },
    { .code = DHCP_OPTION_ROUTER,           .name = "gateway" },
    { .code = DHCP_OPTION_DNS_SERVERS,      .name = "dns" },
    { .code = DHCP_OPTION_HOSTNAME,         .name = "hostname" }, // SET option too
    { .code = DHCP_OPTION_DOMAIN_NAME,      .name = "domain" },
    { .code = DHCP_OPTION_IP_TTL,           .name = "ipttl" },
    { .code = DHCP_OPTION_MTU,              .name = "mtu" },
    { .code = DHCP_OPTION_BCAST_ADDR,       .name = "broadcast" },
    { .code = DHCP_OPTION_ROUTES,           .name = "routes" },
    { .code = DHCP_OPTION_LEASE_TIME,       .name = "lease" },
    { .code = DHCP_OPTION_SERVER_ID,        .name = "serverid" },
    { .code = DHCP_OPTION_STATIC_ROUTES,    .name = "staticroutes" },
    { .code = DHCP_OPTION_MS_STATIC_ROUTES, .name = "msstaticroutes" },
    
    // vendor specific is customized by Plume with patch
    { .code = DHCP_OPTION_VENDOR_SPECIFIC,  .name = "vendorspec" }
};

/* REQ options not listed in enum osn_dhcp_option, supported by udhcp client */
static const opt_info_t udhcp_hidden_opts[] =
{
    { .code = 0x0, .name = "ip" }, // special case to support leased IP addr reporting
    { .code = 0x4, .name = "timesrv" },
    { .code = 0x5, .name = "namesrv" },
    { .code = 0x7, .name = "logsrv" },
    { .code = 0x8, .name = "cookiesrv" },
    { .code = 0x9, .name = "lprsrv" },
    { .code = 0x10, .name = "swapsrv" },
    { .code = 0x2a, .name = "ntpsrv" },
    { .code = 0x28, .name = "nisdomain" },
    { .code = 0x29, .name = "nissrv" },
    { .code = 0x2c, .name = "wins" },
    { .code = 0x38, .name = "message" }
};

/* REQ options inaccessible via udhcp client env variables but 
 * for some reason included in the enum public list */
static const opt_info_t udhcp_noaccess_opts[] =
{
    { .code = DHCP_OPTION_PARAM_LIST,       .name = "optlist "},
    { .code = DHCP_OPTION_DOMAIN_SEARCH,    .name = "domain_search" },
    { .code = DHCP_OPTION_MSG_TYPE,         .name = "msgtype" }
};

/* SET options supported by udhcp client */
static const opt_info_t udhcp_set_opts[] =
{
    { .code = DHCP_OPTION_VENDOR_CLASS,     .name = "vendor_class" },
    { .code = DHCP_OPTION_HOSTNAME,         .name = "hostname" },    // REQ option too
    { .code = DHCP_OPTION_ADDRESS_REQUEST,  .name = "requested_ip" },
    { .code = DHCP_OPTION_OSYNC_SWVER,      .name = "osync_swver" },
    { .code = DHCP_OPTION_OSYNC_PROFILE,    .name = "osync_profile" },
    { .code = DHCP_OPTION_OSYNC_SERIAL_OPT, .name = "osync_serial_opt" }
};

static const struct s_dhcp_options
{
    const opt_info_t * const opt_array;
    size_t opt_length;
} dhcp_options[] =
{
    { .opt_array = udhcp_public_opts,   .opt_length = ARRAY_SIZE(udhcp_public_opts) },
    { .opt_array = udhcp_hidden_opts,   .opt_length = ARRAY_SIZE(udhcp_hidden_opts) },
    { .opt_array = udhcp_set_opts,      .opt_length = ARRAY_SIZE(udhcp_set_opts) },
    { .opt_array = udhcp_noaccess_opts, .opt_length = ARRAY_SIZE(udhcp_noaccess_opts) },
};

static inline bool valid_id(int id)
{
    return (id >= 0 && id <= 255);
}

static const opt_info_t * find_opt_by_id(int id, const opt_info_t *p_opt, size_t len)
{
    if (valid_id(id))
    {
        const opt_info_t * p_end = p_opt + len;
        for (; p_opt < p_end; p_opt++)
        {
            if (p_opt->code == id)
            {
                return p_opt;
            }
        }
    }
    return NULL;
}

static const opt_info_t * find_opt_by_name(const char *name, const opt_info_t *p_opt, size_t len)
{
    if (name != NULL && name[0] != '\0')
    {
        const opt_info_t * p_end = p_opt + len;
        for (; p_opt < p_end; p_opt++)
        {
            if (0 == strcmp(name, p_opt->name))
            {
                return p_opt;
            }
        }
    }
    return NULL;
}

const char* dhcp_option_name(int opt_id)
{
    size_t n;
    for (n = 0; n < ARRAY_SIZE(dhcp_options); ++n)
    {
        const opt_info_t * p_opt = find_opt_by_id(opt_id, dhcp_options[n].opt_array, dhcp_options[n].opt_length);
        if (NULL != p_opt)
        {
            return p_opt->name;
        }
    }
    // return empty string when option not found
    return "";
}

int dhcp_option_id(const char *opt_name)
{
    size_t n;
    for (n = 0; n < ARRAY_SIZE(dhcp_options); ++n)
    {
        const opt_info_t * p_opt = find_opt_by_name(opt_name, dhcp_options[n].opt_array, dhcp_options[n].opt_length);
        if (NULL != p_opt)
        {
            return p_opt->code;
        }
    }
    return -1;
}

bool udhcp_client_is_set_option(int id)
{
    return (NULL != find_opt_by_id(id, ARRAY_AND_SIZE(udhcp_set_opts)));
}

bool udhcp_client_is_req_option(int id)
{
    return (NULL != find_opt_by_id(id, ARRAY_AND_SIZE(udhcp_public_opts))
         || NULL != find_opt_by_id(id, ARRAY_AND_SIZE(udhcp_hidden_opts)));
}

bool udhcp_client_is_supported_option(int id)
{
    return udhcp_client_is_req_option(id) || udhcp_client_is_set_option(id);
}

int udhcp_client_get_option_id(const char *opt_name)
{
    const opt_info_t *p_opt;
    if ((p_opt = find_opt_by_name(opt_name, ARRAY_AND_SIZE(udhcp_public_opts)), p_opt != NULL) || 
        (p_opt = find_opt_by_name(opt_name, ARRAY_AND_SIZE(udhcp_hidden_opts)), p_opt != NULL))
    {
        return p_opt->code;
    }
    return -1;
}
