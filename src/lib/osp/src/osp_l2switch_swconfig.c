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

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "const.h"
#include "ds_tree.h"
#include "execsh.h"
#include "log.h"
#include "os_regex.h"
#include "os_util.h"

#define SWCONFIG_SLOTS_MAX          128         /* Maximum number of slots */
#define SWCONFIG_PORTS_MAX          16          /* Maximum ports per slot */
#define SWCONFIG_PARSE_SLOTS_ARGS   (int[]){ -1 }
#define SWCONFIG_PORT_TAGGED        (1 << 24)   /* Bit that flags the port as tagged */

struct swconfig_slot
{
    int         vs_vid;                         /* VLAN assigned to this slot or C_VLAN_INVALID if free */
    int         vs_ports[SWCONFIG_PORTS_MAX];   /* Configured ports for this slot */
    int         vs_ports_len;                   /* Number of configured ports */
};

static struct swconfig_slot swconfig_slot_list[SWCONFIG_SLOTS_MAX];

/*
 * Command for adding a VLAN port configuration:
 *
 * $1 = switch device
 * $2 = VLAN slot
 * $3 = VLAN ID
 * $4 = Port definition, for example "0t 5t"
 */
static char swconfig_set_cmd[] = _S(
        swconfig dev "$1" vlan "$2" set vid "$3";
        swconfig dev "$1" vlan "$2" set ports "$4";
        swconfig dev "$1" set apply);

static bool swconfig_init(void);
static bool swconfig_slot_apply(int slot);
static execsh_fn_t swconfig_parse_slots_fn;
static int swconfig_ifnametoport(const char *ifname);
static int swconfig_slot_get(int vlanid);
static int swconfig_slot_put(int vlanid);
static bool swconfig_port_set(int slotid, int port, bool tagged);
static bool swconfig_port_unset(int slotid, int port);


/**
 * Initialize l2switch subsystem
 * @return true on success
 */
bool osp_l2switch_init(void)
{
    LOG(INFO, "swconfig: Swconfig configuration: device=%s ports: CPU=%d %s",
            CONFIG_OSP_L2SWITCH_SWCONFIG_DEVICE,
            CONFIG_OSP_L2SWITCH_SWCONFIG_PORT_CPU,
            CONFIG_OSP_L2SWITCH_SWCONFIG_PORT_MAP);

    if (swconfig_init())
    {
        LOG(ERR, "swconfig initialization failed.");
    }

    return true;
}

/**
 * Set the port's vlanid
 * @return true on success
 */
bool osp_l2switch_vlan_set(char *ifname, const int32_t vlan, bool tagged)
{
    int slot;
    int port;

    port = swconfig_ifnametoport(ifname);
    if (port < 0)
    {
        LOG(DEBUG, "swconfig: No port mapping for interface %s. Skipping.", ifname);
        return true;
    }

    slot = swconfig_slot_get(vlan);
    if (slot < 0)
    {
        LOG(ERR, "swconfig: Error adding VLAN %d. Unable to allocate slot.", vlan);
        return false;
    }

    if (!swconfig_port_set(slot, port, tagged))
    {
        LOG(WARN, "swconfig: Error assigning port %d(%s) to vlan %d.",
                port,
                tagged ? "tagged" : "untagged",
                vlan);
        return false;
    }

    /* Apply slot configuration */
    if (!swconfig_slot_apply(slot))
    {
        LOG(ERR, "swconfig: Error applying slot configuration.");
        return false;
    }

    return true;
}

/**
 * Remove port's vlanid
 * @return true on success
 */
bool osp_l2switch_vlan_unset(char *ifname, const int32_t vlan)
{
    int slot;
    int port;

    port = swconfig_ifnametoport(ifname);
    if (port < 0)
    {
        return true;
    }

    slot = swconfig_slot_get(vlan);
    if (slot < 0)
    {
        LOG(WARN, "swconfig: %s: Error unsetting VLAN %d, it may not exist.", ifname, vlan);
        return true;
    }

    if (!swconfig_port_unset(slot, port))
    {
        LOG(DEBUG, "swconfig: %s: Unable to unset port %d (on VLAN %d). It may not be set.",
                ifname, port, vlan);
        return true;
    }

    if (!swconfig_slot_apply(slot))
    {
        LOG(ERR, "swconfig: %s: Error unsetting port %d from slot %d (vlan %d).",
                ifname, port, slot, swconfig_slot_list[slot].vs_vid);
        return false;
    }

    slot = swconfig_slot_put(vlan);
    if (slot < 0)
    {
        /* No allocated slot for this VLAN */
        LOG(WARN, "swconfig: No allocated slot for VLAN %d.", vlan);
    }

    return true;
}

bool osp_l2switch_new(char *ifname)
{
    (void)ifname;
    return true;
}

void osp_l2switch_del(char *ifname)
{
    (void)ifname;
    return;
}

bool osp_l2switch_apply(char *ifname)
{
    (void)ifname;
    return true;
}

/*
 * ===========================================================================
 *  Private and utility functions
 * ===========================================================================
 */
bool swconfig_init(void)
{
    int rc;
    int ii;

    for (ii = 0; ii < ARRAY_LEN(swconfig_slot_list); ii++)
    {
        swconfig_slot_list[ii].vs_vid = C_VLAN_INVALID;
    }

    rc = execsh_fn(
            swconfig_parse_slots_fn,
            SWCONFIG_PARSE_SLOTS_ARGS,
            _S(swconfig dev "$1" show), CONFIG_OSP_L2SWITCH_SWCONFIG_DEVICE);
    if (rc != 0)
    {
        LOG(DEBUG, "swconfig: swconfig failed, rc = %d", rc);
    }

    (void)swconfig_ifnametoport;

    return true;
}

bool swconfig_parse_slots_fn(void *ctx, int msg_type, const char *buf)
{
    int m;
    char ps[64];
    long val;
    regmatch_t pm[2];

    /*
     * Regex used to parse the output of "swconfig dev switch0 show"
     */
    static os_reg_list_t swconfig_init_re[] =
    {
        OS_REG_LIST_ENTRY(0, "^VLAN "RE_GROUP(RE_NUM)":"),  /* Match: VLAN X: */
        OS_REG_LIST_ENTRY(1, "\tvid: "RE_GROUP(RE_NUM)),    /* Match: <tab>vid: X */
        OS_REG_LIST_END(-1)
    };

    int *slot = ctx;

    LOG(DEBUG, "swconfig> %s", buf);

    /* Parse only stdout */
    if (msg_type != EXECSH_PIPE_STDOUT) return true;

    m = os_reg_list_match(swconfig_init_re, (char *)buf, pm, ARRAY_LEN(pm));
    switch (m)
    {
        case 0:
            os_reg_match_cpy(ps, sizeof(ps), buf, pm[1]);
            if (!os_strtoul(ps, &val, 0))
            {
                LOG(DEBUG, "swconfig: Error parsing current configuration. VLAN slot ID is invalid: %s", ps);
                break;
            }
            *slot = val;
            break;

        case 1:
            if (*slot == -1) break;

            os_reg_match_cpy(ps, sizeof(ps), buf, pm[1]);
            if (!os_strtoul(ps, &val, 0))
            {
                LOG(DEBUG, "Error parsing VID: %s", ps);
                break;
            }

            if (val < C_VLAN_MIN || val > C_VLAN_MAX)
            {
                LOG(ERR, "swconfig: Error parsing current configuration. VLAN ID %ld is outside of valid range.", val);
                break;
            }

            if (*slot >= SWCONFIG_SLOTS_MAX)
            {
                LOG(ERR, "swconfig: Error parsing current configuration. Slot %d is outside maximum range.", *slot);
                return true;
            }

            swconfig_slot_list[*slot].vs_vid = val;
            LOG(DEBUG, "swconfig: Reserved slot %d for VLAN %ld.", *slot, val);
            *slot = -1;
            break;

        default:
            break;
    }

    return true;
}

bool swconfig_slot_apply(int slot)
{
    int rc;

    char sslot[C_INT32_LEN];
    char svlan[C_INT32_LEN];
    char sports[256];

    snprintf(sslot, sizeof(sslot), "%d", slot);
    snprintf(svlan, sizeof(svlan), "%d", swconfig_slot_list[slot].vs_vid);

    if (swconfig_slot_list[slot].vs_ports_len == 0)
    {
        STRSCPY(sports, "");
    }
#ifdef CONFIG_OSP_L2SWITCH_SWCONFIG_VLAN0_PORTS
    else if (swconfig_slot_list[slot].vs_vid == 0)
    {
        STRSCPY(sports, CONFIG_OSP_L2SWITCH_SWCONFIG_VLAN0_PORTS);
    }
#endif
    else
    {
        /* Create a ports strings, for example "0t 5t". The CPU port is always tagged. */
        int pi;
        snprintf(sports, sizeof(sports), "%dt", CONFIG_OSP_L2SWITCH_SWCONFIG_PORT_CPU);
        for (pi = 0; pi < swconfig_slot_list[slot].vs_ports_len; pi++)
        {
            bool tagged;
            int port;

            port = swconfig_slot_list[slot].vs_ports[pi];
            tagged = port & SWCONFIG_PORT_TAGGED;
            port &= ~SWCONFIG_PORT_TAGGED;

            snprintf(sports + strlen(sports), sizeof(sports) - strlen(sports), " %d%s",
                port,
                tagged ? "t" : "");
        }
    }

    rc = execsh_log(
            LOG_SEVERITY_DEBUG,
            swconfig_set_cmd,
            CONFIG_OSP_L2SWITCH_SWCONFIG_DEVICE,
            sslot,
            svlan,
            sports);
    if (rc != 0)
    {
        LOG(ERR, "swconfig: Error applying configuration for slot %d (VLAN %d).",
                slot, swconfig_slot_list[slot].vs_vid);
        return false;
    }

    return true;
}

/*
 * Map the interface name @p ifname to its assigned port number
 */
int swconfig_ifnametoport(const char *ifname)
{
    char *p;
    char *pws;

    char buf[] = CONFIG_OSP_L2SWITCH_SWCONFIG_PORT_MAP;

    /* Split at whitespaces */
    for (pws = buf; (p = strsep(&pws, " \t")) != NULL;)
    {
        char *pport;
        char *pif;
        long pval;

        char *peq = p;

        pif = strsep(&peq, "=");
        pport = strsep(&peq, "=");

        if (pif == NULL || pport == NULL)
        {
            LOG(ERR, "swconfig: Error parsing port map configuration: %s", CONFIG_OSP_L2SWITCH_SWCONFIG_PORT_MAP);
            return -1;
        }

        if (strcmp(pif, ifname) != 0) continue;

        if (!os_strtoul(pport, &pval, 0))
        {
            LOG(ERR, "swconfig: Invalid port value for interface %s in port map configuration: %s",
                    pif,
                    CONFIG_OSP_L2SWITCH_SWCONFIG_PORT_MAP);
            return -1;
        }

        return (int)pval;
    }

    return -1;
}

/*
 * Allocate a slot for @p vlanid. Return -1 in case of an allocation error
 * otherwise the slotid associated with the VLAN
 */
int swconfig_slot_get(int vlanid)
{
    int slot;
    int ii;

    for (ii = 0; ii < SWCONFIG_SLOTS_MAX; ii++)
    {
        slot = (ii + vlanid) % SWCONFIG_SLOTS_MAX;
        if (swconfig_slot_list[slot].vs_vid == vlanid) return slot;
        if (swconfig_slot_list[slot].vs_vid == C_VLAN_INVALID) break;
    }

    if (ii >= SWCONFIG_SLOTS_MAX)
    {
        /* All slots are full */
        return -1;
    }

    LOG(NOTICE, "swconfig: Allocated slot %d for VLAN %d.", slot, vlanid);
    swconfig_slot_list[slot].vs_vid = vlanid;
    return slot;
}

/*
 * Free the slot that holds information about @p vlanid
 */
int swconfig_slot_put(int vlanid)
{
    int slot;
    int si;

    for (si = 0; si < SWCONFIG_SLOTS_MAX; si++)
    {
        slot = (si + vlanid) % SWCONFIG_SLOTS_MAX;
        if (swconfig_slot_list[slot].vs_vid == C_VLAN_INVALID) break;
        if (swconfig_slot_list[slot].vs_vid != vlanid) continue;

        if (swconfig_slot_list[slot].vs_ports_len == 0)
        {
            swconfig_slot_list[slot].vs_vid = C_VLAN_INVALID;
        }

        return slot;
    }

    return -1;
}

/*
 * Assign port to slot
 */
bool swconfig_port_set(int slotid, int port, bool tagged)
{
    int pi;

    struct swconfig_slot *pss = &swconfig_slot_list[slotid];

    for (pi = 0; pi < pss->vs_ports_len; pi++)
    {
        if ((pss->vs_ports[pi] & (~SWCONFIG_PORT_TAGGED)) == port) return true;
    }

    /* Out of ports */
    if (pi >= SWCONFIG_PORTS_MAX)
    {
        LOG(ERR, "swconfig: Reached maximum number of ports in slot %d (vlan %d).",
                slotid, pss->vs_vid);
        return false;
    }

    pss->vs_ports[pi] = port | (tagged ? SWCONFIG_PORT_TAGGED : 0);
    pss->vs_ports_len++;

    return true;
}

/*
 * Unassign port from slot
 */
bool swconfig_port_unset(int slotid, int port)
{
    int pi;

    struct swconfig_slot *pss = &swconfig_slot_list[slotid];

    for (pi = 0; pi < pss->vs_ports_len; pi++)
    {
        if ((pss->vs_ports[pi] & (~SWCONFIG_PORT_TAGGED)) == port) break;
    }

    if (pi >= pss->vs_ports_len)
    {
        LOG(DEBUG, "swconfig: Port %d not found in slot %d (vlan %d).",
                port, slotid, pss->vs_vid);
        return false;
    }

    if (pi + 1 < pss->vs_ports_len)
    {
        /* Swap last element with the current one (at index pi) */
        pss->vs_ports[pi] = pss->vs_ports[pss->vs_ports_len - 1];
    }
    pss->vs_ports_len--;

    return true;
}
