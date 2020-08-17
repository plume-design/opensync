#include <stdbool.h>

#include "osn_mapt.h"

bool osn_mapt_configure(
        const char *brprefix,
        int ratio,
        const char *lan_ifname,
        const char *wan_ifname,
        const char *ipv6_prefix,
        const char *ipv4_subnet,
        const char *ipv4_addr,
        int psid_offset,
        int psid)
{
    (void)brprefix;
    (void)ratio;
    (void)lan_ifname;
    (void)wan_ifname;
    (void)ipv6_prefix;
    (void)ipv4_subnet;
    (void)ipv4_addr;
    (void)psid_offset;
    (void)psid;

    return true;
}

bool osn_mapt_stop(void)
{
    return true;
}
