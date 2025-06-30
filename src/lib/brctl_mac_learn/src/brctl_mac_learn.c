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

/**
 * MAC learning of wired clients on the native linux bridge
 */

#include <ctype.h>
#include <ev.h>
#include <net/if.h>
#include <stdlib.h>

#include "ds.h"
#include "ds_list.h"
#include "ds_tree.h"
#include "log.h"
#include "schema.h"
#include "schema_consts.h"
#include "memutil.h"
#include "util.h"

#include "brctl_mac_learn.h"

/*****************************************************************************/

#define MAC_LEARNING_INTERVAL   10.0

#define MODULE_ID               LOG_MODULE_ID_TARGET

#if defined(CONFIG_TARGET_LAN_BRIDGE_NAME)
#define BRCTL_LAN_BRIDGE   CONFIG_TARGET_LAN_BRIDGE_NAME
#else
#define BRCTL_LAN_BRIDGE   SCHEMA_CONSTS_BR_NAME_HOME
#endif

/*****************************************************************************/

static int mac_learning_cmp(const void *_a, const void *_b);

struct mac_learning_flt_t {
    char                    brname[IFNAMSIZ];
    char                    ifname[IFNAMSIZ];
    int                     ifnum;
    struct ds_dlist_node    list;
};

struct mac_learning_t {
    struct schema_OVS_MAC_Learning  oml;
    bool                            valid;
    struct ds_tree_node             list;
};

/******************************************************************************
 *  Global definitions
 *****************************************************************************/

static struct ev_timer             g_mac_learning_timer;
static target_mac_learning_cb_t   *g_mac_learning_cb = NULL;

static ds_tree_t    g_mac_learning = DS_TREE_INIT(mac_learning_cmp,
                                                  struct mac_learning_t,
                                                  list);
static ds_dlist_t   g_mac_learning_flt = DS_DLIST_INIT(struct mac_learning_flt_t,
                                                       list);

/******************************************************************************
 *  PROTECTED definitions
 *****************************************************************************/

static struct mac_learning_flt_t *mac_learning_flt_find(int ifnum, const char *brname)
{
    struct mac_learning_flt_t *flt;

    ds_dlist_foreach(&g_mac_learning_flt, flt) {
        if ((flt->ifnum == ifnum) && (strcmp(flt->brname, brname) == 0)) {
            return flt;
        }
    }

    return NULL;
}

static bool mac_learning_flt_get(const char *brname)
{
    FILE         *fp;
    char          cmd[512];
    char          buf[512];
    const char  **iflist;
    int           ifidx;
    char          ifname[IFNAMSIZ];

    if (!is_input_shell_safe(brname)) return false;

    /* Find port numbers for eth interfaces. Expected output:
     *
     * eth1 (2)
     * eth2 (3)
     * eth3 (4)
     * lan0 (5)
     * ...
     */
    snprintf(cmd, sizeof(cmd), "brctl showstp %s | grep -E '^(eth|lan)'", brname);

    fp = popen(cmd, "r");
    if (!fp)
    {
        LOGE("BRCTLMAC: Unable to read bridge stp table! :: brname=%s", brname);
        return false;
    }

    while (fgets(buf, sizeof(buf), fp))
    {
        int                         ifnum;
        char                        eth[IFNAMSIZ];
        struct mac_learning_flt_t  *flt;

        if (2 != sscanf(buf, "%s (%d)", eth, &ifnum))
        {
            continue;
        }

        // Skip non ethernet clients ports
        snprintf(ifname, sizeof(ifname), "%s", eth);
        iflist = target_ethclient_iflist_get();
        for (ifidx=0; iflist[ifidx]; ifidx++)
        {
            if (!strcmp(ifname, iflist[ifidx]))
            {
                break;
            }
            /* For vlan, interface from the list must be found at the very beginning
               of the ifname. Also both brname and ifname must contain dot.
               For example: br-home.600, eth1.600, eth1 */
            if ((strchr(brname, '.') != NULL) &&
                (strchr(ifname, '.') != NULL) &&
                (strstr(ifname, iflist[ifidx]) == ifname))
            {
                LOGT("BRCTLMAC: Found vlan interface %s matching %s", ifname, iflist[ifidx]);
                break;
            }
        }
        if (iflist[ifidx] == NULL)
        {
            LOGT("BRCTLMAC: Skip %s", ifname);
            continue;
        }

        flt = mac_learning_flt_find(ifnum, brname);
        if (flt != NULL) {
            LOGT("BRCTLMAC: Already exists %s @ %s :: ifnum=%d", flt->ifname, brname, flt->ifnum);
            if (strcmp(flt->ifname, ifname) != 0) {
                strncpy(flt->ifname, ifname, sizeof(flt->ifname));
                LOGI("BRCTLMAC: Found ifnum=%d ifname changed to %s @ %s", flt->ifnum, flt->ifname, brname);
            }
            continue;
        }
        else
        {
            flt = CALLOC(1, sizeof(*flt));
        }

        snprintf(flt->ifname, sizeof(flt->ifname), "%s", eth);
        snprintf(flt->brname, sizeof(flt->brname), "%s", brname);
        flt->ifnum = ifnum;

        ds_dlist_insert_tail(&g_mac_learning_flt, flt);

        LOGI("BRCTLMAC: Using %s @ %s :: ifnum=%d", flt->ifname, brname, flt->ifnum);
    }

    if (fp)
        pclose(fp);

    return true;
}

static bool mac_learning_parse(const char *brname)
{
    FILE *fp;
    char cmd[512];
    char buf[512];

    if (!is_input_shell_safe(brname)) return false;

    /* Find bridge port numbers and mac addresses. Expected output:
     *
     * 6 00:c0:02:12:35:8a yes 0.00
     * 1 40:b9:3c:1d:d9:09 no 1.80
     * 1 68:05:ca:28:c1:45 no 5.40
     * ...
     */
    snprintf(cmd,
             sizeof(cmd),
             "brctl showmacs %s | awk '{ print $1\" \"$2\" \"$3\" \"$4 }'",
             brname);

    fp = popen(cmd, "r");
    if (!fp)
    {
        LOGE("BRCTLMAC: Unable to read bridge mac table! :: brname=%s", brname);
        return false;
    }

    while (fgets(buf, sizeof(buf), fp))
    {
        int   ifnum;
        float age;
        char  mac[64];
        char  local[64];

        if ((4 != sscanf(buf, "%d %64s %64s %f", &ifnum, mac, local, &age)) ||
            (0 == strcmp(local, "yes")))
        {
            continue;
        }

        // Look only at eth interfaces
        struct mac_learning_flt_t *flt = mac_learning_flt_find(ifnum, brname);
        if (flt == NULL)
        {
            continue;
        }

        struct schema_OVS_MAC_Learning oml;
        memset(&oml, 0, sizeof(oml));
        strscpy(oml.hwaddr, mac, sizeof(oml.hwaddr));
        strscpy(oml.brname, brname, sizeof(oml.brname));
        strscpy(oml.ifname, flt->ifname, sizeof(oml.ifname));
        char *str_vlanid = strchr(brname, '.');
        if (str_vlanid != NULL)
        {
            int vlanid = atoi(str_vlanid + 1);
            LOGT("BRCTLMAC: Setting vlan %d for %s", vlanid, brname);
            oml.vlan = vlanid;
        }

        // New entry
        bool update = false;
        struct mac_learning_t *ml;
        ml = ds_tree_find(&g_mac_learning, &oml);
        if (ml == NULL)
        {
            ml = CALLOC(1, sizeof(*ml));

            memcpy(&ml->oml, &oml, sizeof(ml->oml));
            ds_tree_insert(&g_mac_learning, ml, &ml->oml);

            update = true;
        }

        ml->valid = true;

        LOGT("BRCTLMAC: parsed mac table entry :: brname=%s ifname=%s mac=%s update=%s",
             oml.brname,
             oml.ifname,
             oml.hwaddr,
             update ? "true" : "false");

        // Pass new entry to NM
        if (update)
        {
            g_mac_learning_cb(&ml->oml, true);
        }
    }

    if (fp)
        pclose(fp);

    return true;
}

static void mac_learning_invalidate(void)
{
    struct mac_learning_t *ml;

    ds_tree_foreach(&g_mac_learning, ml)
    {
        ml->valid = false;
    }
}

static void mac_learning_flush(void)
{
    struct mac_learning_t  *ml;
    ds_tree_iter_t          iter;

    for (ml = ds_tree_ifirst(&iter, &g_mac_learning);
         ml != NULL;
         ml = ds_tree_inext(&iter))
    {
        if (ml->valid)
        {
            continue;
        }

        // Indicate deleted entry to NM
        g_mac_learning_cb(&ml->oml, false);

        // Remove our entry
        ds_tree_iremove(&iter);
        memset(ml, 0, sizeof(*ml));
        FREE(ml);
    }
}

static int mac_learning_cmp(const void *_a, const void *_b)
{
    const struct schema_OVS_MAC_Learning *a = _a;
    const struct schema_OVS_MAC_Learning *b = _b;

    int result = strcmp(a->hwaddr, b->hwaddr);
    if (result != 0)
    {
        return result;
    }
    return strcmp(a->ifname, b->ifname);
}

static void mac_learning_parse_vlanids(char *brname, int vlanids[], int *vlanids_len)
{
    char *brctl_show_output = strexa("brctl", "show");
    if (brctl_show_output == NULL)
    {
        LOGE("BRCTLMAC: Unable to check for vlans on home bridge");
        return;
    }
    char *line, *p;
    size_t brname_length;
    while ((line = strsep(&brctl_show_output, "\n")))
    {
        if (strstr(line, brname) == line) {
            brname_length = 0;
            p = line;
            while ((!isspace(p[0])) && (p[0] != '\0'))
            {
                brname_length++;
                p++;
            }
            if (brname_length > IFNAMSIZ)
            {
                LOGE("BRCTLMAC: Unexpected bridge name: %s", line);
                continue;
            }
            char brname_vlanid[IFNAMSIZ] = {'\0'};
            strncpy(brname_vlanid, line, brname_length);
            LOGT("BRCTLMAC: bridge name: %s", brname_vlanid);
            int vlan_id = atoi(brname_vlanid + strlen(brname) + 1);
            if ((vlan_id != 0) && ((*vlanids_len) < C_VLAN_MAX))
            {
                LOGD("BRCTLMAC: Adding %d to the vlan list", vlan_id);
                vlanids[*vlanids_len] = vlan_id;
                (*vlanids_len)++;
            }
        }
    }
}

static void mac_learing_timer_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    int vlanids[C_VLAN_MAX] = {0};
    int vlanids_len = 0;
    mac_learning_parse_vlanids(BRCTL_LAN_BRIDGE, vlanids, &vlanids_len);

    // Some vendors add/remove ethernet interface in runtime
    if (!mac_learning_flt_get(BRCTL_LAN_BRIDGE))
    {
        LOGE("BRCTLMAC: Unable to create MAC learning filter!");
        return;
    }
    int i;
    for (i=0; i < vlanids_len; i++)
    {
        char brname_vlanid[IFNAMSIZ] = {'\0'};
        snprintf(brname_vlanid, IFNAMSIZ, BRCTL_LAN_BRIDGE ".%d", vlanids[i]);
        if (!mac_learning_flt_get(brname_vlanid))
        {
            LOGE("BRCTLMAC: Unable to create MAC learning filter for %s", brname_vlanid);
            return;
        }
    }

    LOGD("BRCTLMAC: refreshing mac learning table");
    mac_learning_invalidate();
    mac_learning_parse(BRCTL_LAN_BRIDGE);
    for (i=0; i < vlanids_len; i++)
    {
        char brname_vlanid[IFNAMSIZ] = {'\0'};
        snprintf(brname_vlanid, IFNAMSIZ, BRCTL_LAN_BRIDGE ".%d", vlanids[i]);
        mac_learning_parse(brname_vlanid);
    }
    mac_learning_flush();
}

/******************************************************************************
 *  PUBLIC API definitions
 *****************************************************************************/

bool brctl_mac_learning_register(target_mac_learning_cb_t *omac_cb)
{
    if (g_mac_learning_cb)
        return false;

    // Init NM callback
    g_mac_learning_cb = omac_cb;

    // Init timer
    ev_timer_init(&g_mac_learning_timer,
                  mac_learing_timer_cb,
                  MAC_LEARNING_INTERVAL,
                  MAC_LEARNING_INTERVAL);
    ev_timer_start(EV_DEFAULT, &g_mac_learning_timer);

    LOGN("BRCTLMAC: Successfully registered MAC learning. :: brname=%s",
            BRCTL_LAN_BRIDGE);

    return true;
}
