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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "ovsdb_update.h"
#include "target.h"
#include "unity.h"
#include "log.h"
#include "util.h"
#include "nm2.h"
#include "nm2_nb_bridge.h"
#include "nm2_nb_interface.h"
#include "nm2_nb_port.h"

ovsdb_update_monitor_t g_mon;

static struct nm2_iface *nm2_dummy_add_inet_conf(struct schema_Wifi_Inet_Config *iconf)
{
    enum nm2_iftype iftype;
    struct nm2_iface *piface = NULL;


    if (!nm2_iftype_fromstr(&iftype, iconf->if_type))
    {
        LOG(ERR,
            "nm2_dummy_add_inet_conf: %s: Unknown interface type %s. Unable to create "
            "interface.",
            iconf->if_name, iconf->if_type);
        return NULL;
    }

    piface = nm2_iface_new(iconf->if_name, iftype);
    if (piface == NULL)
    {
        LOG(ERR, "nm2_dummy_add_inet_conf: %s (%s): Unable to create interface.",
            iconf->if_name, iconf->if_type);
        return NULL;
    }

    return piface;
}

static void nm2_dummy_del_inet_conf(struct schema_Wifi_Inet_Config *old_rec)
{
    struct nm2_iface *piface = NULL;

    LOG(INFO, "nm2_dummy_del_inet_conf: interface %s (type %s).", old_rec->if_name,
        old_rec->if_type);

    piface = nm2_iface_get_by_name(old_rec->if_name);
    if (piface == NULL)
    {
        LOG(ERR, "nm2_dummy_del_inet_conf: Unable to delete non-existent interface %s.",
            old_rec->if_name);
    }

    if (piface != NULL && !nm2_iface_del(piface))
    {
        LOG(ERR, "nm2_dummy_del_inet_conf: Error during destruction of interface %s.",
            old_rec->if_name);
    }

    return;
}

static struct nm2_iface *nm2_dummy_modify_inet_conf(struct schema_Wifi_Inet_Config *iconf)
{
    enum nm2_iftype iftype;
    struct nm2_iface *piface = NULL;

    LOG(INFO, "nm2_dummy_modify_inet_conf: UPDATE interface %s (type %s).", iconf->if_name,
        iconf->if_type);

    if (!nm2_iftype_fromstr(&iftype, iconf->if_type))
    {
        LOG(ERR,
            "nm2_dummy_modify_inet_conf: %s: Unknown interface type %s. Unable to create "
            "interface.",
            iconf->if_name, iconf->if_type);
        return NULL;
    }

    piface = nm2_iface_get_by_name(iconf->if_name);
    if (piface == NULL)
    {
        LOG(ERR,
            "nm2_dummy_modify_inet_conf: %s (%s): Unable to modify interface, didn't find "
            "it.",
            iconf->if_name, iconf->if_type);
    }

    return piface;
}

static void callback_Wifi_Inet_Config(ovsdb_update_monitor_t *mon,
                                      struct schema_Wifi_Inet_Config *old_rec,
                                      struct schema_Wifi_Inet_Config *iconf)
{
    struct nm2_iface *piface = NULL;

    TRACE();

    switch (mon->mon_type)
    {
    case OVSDB_UPDATE_NEW:
        piface = nm2_dummy_add_inet_conf(iconf);
        break;

    case OVSDB_UPDATE_DEL:
        nm2_dummy_del_inet_conf(old_rec);
        break;

    case OVSDB_UPDATE_MODIFY:
        piface = nm2_dummy_modify_inet_conf(iconf);
        break;

    default:
        LOG(ERR, "Invalid Wifi_Inet_Config mon_type(%d)", mon->mon_type);
    }

    if (mon->mon_type == OVSDB_UPDATE_NEW || mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        if (!piface)
        {
            LOG(ERR, "callback_Wifi_Inet_Config: Couldn't get the interface(%s)",
                iconf->if_name);
            return;
        }

        /* Apply the configuration */
        if (!nm2_iface_apply(piface))
        {
            LOG(ERR, "callback_Wifi_Inet_Config: %s (%s): Unable to apply configuration.",
                iconf->if_name, iconf->if_type);
        }
    }

    return;
}

static void dump_ports_list(ds_tree_t *nm2_port_list)
{
    struct nm2_port *port;

    port = ds_tree_head(nm2_port_list);

    TRACE();

    while (port != NULL)
    {
        LOGT("%s(): %s", __func__, port->port_uuid.uuid);
        port = ds_tree_next(nm2_port_list, port);
    }
}

void __ev_wait_dummy1(EV_P_ ev_timer *w, int revent)
{
    (void)loop;
    (void)w;
    (void)revent;
    TRACE();
}

bool ev_wait1(int *trigger, double timeout)
{
    bool retval = false;

    /* the dnsmasq process is started with a short delay, sleep for few seconds before continuing */
    ev_timer ev;
    ev_timer_init(&ev, __ev_wait_dummy1, timeout, 0.0);

    ev_timer_start(EV_DEFAULT, &ev);

    while (ev_run(EV_DEFAULT, EVRUN_ONCE))
    {
        if (!ev_is_active(&ev))
        {
            break;
        }

        if (trigger != NULL && *trigger != 0)
        {
            retval = true;
            break;
        }
    }

    if (ev_is_active(&ev)) ev_timer_stop(EV_DEFAULT, &ev);

    return retval;
}



void test_port_table(void)
{
    ds_tree_t *nm2_port_list;
    struct nm2_port *port;

    nm2_port_list = nm2_port_get_list();

    TRACE();

    struct schema_Port port_schema[] = {{.name = "port_1", ._uuid = {"uuid_port_1"}},
                                        {.name = "port_2", ._uuid = {"uuid_port_2"}}};

    TRACE();

    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Port(&g_mon, NULL, &port_schema[0]);
    port = ds_tree_find(nm2_port_list, port_schema[0]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(port);

    callback_Port(&g_mon, NULL, &port_schema[1]);
    nm2_port_list = nm2_port_get_list();
    port = ds_tree_find(nm2_port_list, port_schema[1]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(port);

    /* delete the ports */
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    /* just pass the old configuration as delete */
    callback_Port(&g_mon, &port_schema[0], &port_schema[0]);
    port = ds_tree_find(nm2_port_list, port_schema[0]._uuid.uuid);
    LOGT("%s(): port address %p", __func__, port);
    TEST_ASSERT_NULL(port);
    dump_ports_list(nm2_port_list);
    callback_Port(&g_mon, &port_schema[1], &port_schema[0]);
    port = ds_tree_find(nm2_port_list, port_schema[1]._uuid.uuid);
    TEST_ASSERT_NULL(port);
}

void test_bridge_port_first(void)
{
    ds_tree_t *nm2_bridge_list;
    ds_tree_t *nm2_port_list;
    struct nm2_port *port;
    struct nm2_bridge *br;
    struct schema_Port port_schema[] = {
        {
        .name = "port_1",
        ._uuid = {"uuid_port_1"}
        },
        {
        .name = "port_2",
        ._uuid = {"uuid_port_2"}
        }
    };

    struct schema_Bridge br_schema[] = {
        {
        .name = "br-home",
        ._uuid = {"uuid_br_home"},
        .ports[0] = {"uuid_port_1"},
        .ports[1] = {"uuid_port_2"},
        .ports_len = 2,
        }
    };

    struct schema_Bridge modify_br = {
        .name = "br-home",
        ._uuid = {"uuid_br_home"},
        .ports[0] = {"uuid_port_2"},
        .ports_len = 1,
    };

    nm2_bridge_list = nm2_bridge_get_list();
    nm2_port_list = nm2_port_get_list();

    TRACE();
    /* Send port updates */
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Port(&g_mon, NULL, &port_schema[0]);
    port = ds_tree_find(nm2_port_list, port_schema[0]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(port);

    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Port(&g_mon, NULL, &port_schema[1]);
    port = ds_tree_find(nm2_port_list, port_schema[1]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(port);

    /* send bridge updates */
    /* At this time linux bridge will be created and 2 ports will be added */
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Bridge(&g_mon, NULL, &br_schema[0]);
    br = ds_tree_find(nm2_bridge_list, br_schema[0]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(br);

    /* Modify Bridge port uuid_port_1 is removed */
    LOGT("%s(): modifying bridge port", __func__);
    g_mon.mon_type = OVSDB_UPDATE_MODIFY;
    callback_Bridge(&g_mon, &br_schema[0], &modify_br);

    /* Delete port uuid_port_1*/
    TRACE();
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    /* just pass the old configuration as delete */
    callback_Port(&g_mon, &port_schema[0], &port_schema[0]);
    TRACE();
    port = ds_tree_find(nm2_port_list, port_schema[0]._uuid.uuid);
    TRACE();
    LOGT("%s(): port value is %p", __func__, port);
    /* port is deleted value should not be found.*/
    TEST_ASSERT_NULL(port);

    callback_Port(&g_mon, &port_schema[1], &port_schema[1]);
    callback_Bridge(&g_mon, &br_schema[0], &br_schema[0]);
}

/* test the scenario where port is configured later */
void test_bridge_port_second(void)
{
    ds_tree_t *nm2_bridge_list;
    ds_tree_t *nm2_port_list;
    struct nm2_port *port;
    struct nm2_bridge *br;
    struct schema_Port port_schema[] = {{.name = "port_1", ._uuid = {"uuid_port_1"}},
                                        {.name = "port_2", ._uuid = {"uuid_port_2"}}};

    struct schema_Bridge br_schema[] = {{
        .name = "br-home",
        ._uuid = {"uuid_br_home"},
        .ports[0] = {"uuid_port_1"},
        .ports[1] = {"uuid_port_2"},
        .ports_len = 2,
    }};

    struct schema_Bridge modify_br = {
        .name = "br-home",
        ._uuid = {"uuid_br_home"},
        .ports[0] = {"uuid_port_2"},
        .ports_len = 1,
    };

    nm2_bridge_list = nm2_bridge_get_list();
    nm2_port_list = nm2_port_get_list();

    TRACE();
    /* send bridge updates */
    /* At this time linux bridge will be created and 2 ports will be added */
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Bridge(&g_mon, NULL, &br_schema[0]);
    br = ds_tree_find(nm2_bridge_list, br_schema[0]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(br);

    /* Send port updates */
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Port(&g_mon, NULL, &port_schema[0]);
    port = ds_tree_find(nm2_port_list, port_schema[0]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(port);

    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Port(&g_mon, NULL, &port_schema[1]);
    port = ds_tree_find(nm2_port_list, port_schema[1]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(port);

    /* Modify Bridge port uuid_port_1 is removed */
    LOGT("%s(): modifying bridge port", __func__);
    g_mon.mon_type = OVSDB_UPDATE_MODIFY;
    callback_Bridge(&g_mon, &br_schema[0], &modify_br);

    /* Delete port uuid_port_1*/
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    /* just pass the old configuration as delete */
    callback_Port(&g_mon, &port_schema[0], &port_schema[0]);
    port = ds_tree_find(nm2_port_list, port_schema[0]._uuid.uuid);
    /* port is deleted value should not be found.*/
    TEST_ASSERT_NULL(port);

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_Port(&g_mon, &port_schema[1], &port_schema[1]);
    callback_Bridge(&g_mon, &br_schema[0], &br_schema[0]);
}

void test_interface_table(void)
{
    ds_tree_t *nm2_port_list;
    ds_tree_t *nm2_if_list;
    struct nm2_port *port;
    TRACE();

    struct nbm_interface *interface;
    struct schema_Interface inf_schema[] = {{.name = "intf_1", ._uuid = {"uuid_intf_1"}},
                                            {.name = "intf_2", ._uuid = {"uuid_intf_2"}}};

    struct schema_Port port_schema[] = {{
                                            .name = "eth0",
                                            ._uuid = {"uuid_port1"},
                                            .interfaces[0] = {"uuid_intf_1"},
                                            .interfaces[1] = {"uuid_intf_2"},
                                            .interfaces_len = 2,
                                        },
                                        {.name = "eth1", ._uuid = {"uuid_port2"}}};

    struct schema_Port modify_port = {
        .name = "eth0",
        ._uuid = {"uuid_port1"},
        .interfaces_len = 0,
    };

    nm2_if_list = nm2_if_get_list();
    nm2_port_list = nm2_port_get_list();

    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Interface(&g_mon, NULL, &inf_schema[0]);
    interface = ds_tree_find(nm2_if_list, inf_schema[0]._uuid.uuid);
    LOGT("%s(): interface value is %p", __func__, interface);
    TEST_ASSERT_NOT_NULL(interface);
    g_mon.mon_uuid = inf_schema[1]._uuid.uuid;
    callback_Interface(&g_mon, NULL, &inf_schema[1]);
    interface = ds_tree_find(nm2_if_list, inf_schema[1]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(interface);

    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Port(&g_mon, NULL, &port_schema[0]);
    port = ds_tree_find(nm2_port_list, port_schema[0]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(port);

    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Port(&g_mon, NULL, &port_schema[1]);
    port = ds_tree_find(nm2_port_list, port_schema[1]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(port);

    /* Modify Port both the interfaces are removed.*/
    LOGT("%s(): modifying bridge port", __func__);
    g_mon.mon_type = OVSDB_UPDATE_MODIFY;
    callback_Port(&g_mon, &port_schema[0], &modify_port);

    /* Delete interface */
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    /* just pass the old configuration as delete */
    callback_Interface(&g_mon, &inf_schema[0], NULL);
    interface = ds_tree_find(nm2_if_list, inf_schema[0]._uuid.uuid);
    /* interface is deleted value should not be found.*/
    TEST_ASSERT_NULL(interface);

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    /* just pass the old configuration as delete */
    callback_Interface(&g_mon, &inf_schema[1], NULL);
    interface = ds_tree_find(nm2_if_list, inf_schema[1]._uuid.uuid);
    /* interface is deleted value should not be found.*/
    TEST_ASSERT_NULL(interface);
}

void test_wifi_inet_table(void)
{

    struct schema_Wifi_Inet_Config winet_schema[] = {{
        .if_name = "brhome.dhcp",
        ._uuid = {"uuid_intf_1"},
        .if_type = "tap",
    }};

    ds_tree_t *nm2_bridge_list;
    ds_tree_t *nm2_port_list;
    struct nm2_port *port;
    struct nm2_bridge *br;
    struct schema_Port port_schema[] = {
        {
        .name = "brhome.dhcp",
        ._uuid = {"uuid_port_1"}
        },
        {
        .name = "port_2",
        ._uuid = {"uuid_port_2"}
        }
    };

    struct schema_Bridge br_schema[] = {
        {
        .name = "br-home",
        ._uuid = {"uuid_br_home"},
        .ports[0] = {"uuid_port_1"},
        .ports[1] = {"uuid_port_2"},
        .ports_len = 2,
        }
    };

    /* remove port1 from bridge */
    struct schema_Bridge modify_br = {
        .name = "br-home",
        ._uuid = {"uuid_br_home"},
        .ports[0] = {"uuid_port_2"},
        .ports_len = 1,
    };

    nm2_bridge_list = nm2_bridge_get_list();
    nm2_port_list = nm2_port_get_list();

    nm2_inet_config_init();
    nm2_inet_state_init();

    /* Send port updates */
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Port(&g_mon, NULL, &port_schema[0]);
    port = ds_tree_find(nm2_port_list, port_schema[0]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(port);

    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Port(&g_mon, NULL, &port_schema[1]);
    port = ds_tree_find(nm2_port_list, port_schema[1]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(port);

    /* send bridge updates */
    /* At this time linux bridge will be created and 2 ports will be added */
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Bridge(&g_mon, NULL, &br_schema[0]);
    br = ds_tree_find(nm2_bridge_list, br_schema[0]._uuid.uuid);
    if (br == NULL) LOGT("%s(): br is NULL", __func__);
    TEST_ASSERT_NOT_NULL(br);

    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Wifi_Inet_Config(&g_mon, NULL, &winet_schema[0]);
    ev_wait1(NULL, 10.0);

    /* Modify Bridge port uuid_port_1 is removed */
    LOGT("%s(): modifying bridge port", __func__);
    g_mon.mon_type = OVSDB_UPDATE_MODIFY;
    callback_Bridge(&g_mon, &br_schema[0], &modify_br);

    /* Delete port uuid_port_1*/
    LOGT("%s(): removing port %s", __func__, port_schema[0]._uuid.uuid);
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    /* just pass the old configuration as delete */
    callback_Port(&g_mon, &port_schema[0], &port_schema[0]);

    /* Delete the bridge object */
    callback_Bridge(&g_mon, &br_schema[0], &br_schema[0]);

    callback_Wifi_Inet_Config(&g_mon, &winet_schema[0], &winet_schema[0]);
}

