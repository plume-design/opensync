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

#include <net/if.h>
#include <dirent.h>

#include "nm2.h"
#include "os.h"
#include "os_nif.h"

#ifndef MAC_STR_LEN
#define MAC_STR_LEN 18
#endif /* MAC_STR_LEN */

#define NBM_BRIDGE_TABLE_NAME "newbridge"
#define NBM_PORT_TABLE_NAME "newport"
#define NBM_INTERFACE_TABLE_NAME "newinterface"

/**
 * @brief creates the default parameters for creating Interface table.
 * @return json object specifing default Interface table vales
 */
json_t *interface_row(char *if_name)
{
    struct schema_Interface interface;
    pjs_errmsg_t errmsg;
    json_t *row = NULL;
    int ifindex;

    ifindex = if_nametoindex(if_name);
    if (ifindex == 0)
    {
        LOGT("%s(): failed to find interface index for %s", __func__, if_name);
    }

    /* populate default Interface configurations */
    memset(&interface, 0, sizeof(interface));
    SCHEMA_SET_STR(interface.name, if_name);
    SCHEMA_SET_STR(interface.admin_state, "up");
    SCHEMA_SET_INT(interface.ifindex, ifindex);
    SCHEMA_SET_STR(interface.link_state, "up");
    SCHEMA_SET_STR(interface.type, "internal");

    /* convert Interface schema to json object */
    row = schema_Interface_to_json(&interface, errmsg);
    if (row == NULL)
    {
        LOGE("%s: error converting Interface schema structure to JSON.", __func__);
        return row;
    }

    return row;
}

/**
 * @brief creates the default parameters for creating Port table.
 * @return json object specifing default Port table vales
 */
json_t *port_row(char *port_name)
{
    json_t *row = NULL;

    row = json_pack("{s : s, s : [s, s]}", "name", port_name, "interfaces",
                    "named-uuid", NBM_INTERFACE_TABLE_NAME);

    return row;
}

/**
 * @brief creates the default parameters for creating Bridge table.
 * @return json object specifing default Bridge table vales
 */
json_t *bridge_row(char *br_name)
{
    char mac_str[MAC_STR_LEN];
    json_t *row = NULL;
    char datapath[128];
    os_macaddr_t mac;
    bool ret;

    memset(datapath, 0, sizeof(datapath));
    memset(mac_str, 0, sizeof(mac_str));

    ret = os_nif_macaddr_get(br_name, &mac);
    if (ret == false)
    {
        LOGN("%s(): failed to get interface mac address", __func__);
    }

    sprintf(mac_str, PRI_os_macaddr_plain_t, FMT(os_macaddr_pt, &mac));
    snprintf(datapath, sizeof(datapath), "%s%s", "0000", mac_str);

    row = json_pack("{s : s, s : [s, s]}", "name", br_name, "ports", "named-uuid",
				NBM_PORT_TABLE_NAME);

    json_object_set_new(row, "datapath_id", json_string(datapath));
    json_object_set_new(row, "mcast_snooping_enable", json_boolean(true));
    json_object_set_new(row, "rstp_enable", json_boolean(false));
    json_object_set_new(row, "stp_enable", json_boolean(true));

    return row;
}

/**
 * @brief creates the row parameter value for modifing the Open_vSwitch table.
 * Ovs_vSwitch table's "bridges" will be set to default bridge name
 * @return json object specifing the bridges value
 */
static json_t *nm2_ovswitch_row(void)
{
    json_t *row = NULL;

    row = json_pack("[[s, s, [s, [[s, s]]]]]", "bridges", "insert", "set",
                    "named-uuid", NBM_BRIDGE_TABLE_NAME);
    return row;
}

/**
 * @brief sets the uuid-name for the interface table
 * @return json object containing uuid-name
 */
static json_t *nm2_interface_name(void)
{
    json_t *obj = json_object();

    json_object_set_new(obj, "uuid-name", json_string(NBM_INTERFACE_TABLE_NAME));
    return obj;
}

/**
 * @brief sets the uuid-name for the port table
 * @return json object containing uuid-name
 */
static json_t *nm2_port_name(void)
{
    json_t *obj = json_object();

    json_object_set_new(obj, "uuid-name", json_string(NBM_PORT_TABLE_NAME));
    return obj;
}

/**
 * @brief sets the uuid-name for the bridge table
 * @return json object containing uuid-name
 */
static json_t *nm2_bridge_name(void)
{
    json_t *obj = json_object();

    json_object_set_new(obj, "uuid-name", json_string(NBM_BRIDGE_TABLE_NAME));
    return obj;
}

/**
 * @brief populates Interface, Port and Bridge parameters
 * required for creating the corresponding tables.
 * @return None
 */
json_t *nm2_add_bridge_table_params(char *br_name)
{
    json_t *jarray = NULL;

    /* jarray passed to ovsdb_tran_multi to will be NULL for the first transaction.
     * It will be appended in the subsequent calls. */
    jarray = ovsdb_tran_multi(jarray, nm2_interface_name(), SCHEMA_TABLE(Interface),
                              OTR_INSERT, NULL, interface_row(br_name));

    jarray = ovsdb_tran_multi(jarray, nm2_port_name(), SCHEMA_TABLE(Port), OTR_INSERT, NULL,
                              port_row(br_name));

    jarray = ovsdb_tran_multi(jarray, nm2_bridge_name(), SCHEMA_TABLE(Bridge), OTR_INSERT,
                              NULL, bridge_row(br_name));

    jarray = ovsdb_tran_multi(jarray, NULL, SCHEMA_TABLE(Open_vSwitch), OTR_MUTATE, NULL,
                              nm2_ovswitch_row());

    return jarray;
}

json_t *existing_interface_row(char *port_name)
{
    json_t *row = NULL;

    row = json_pack("{s : s}", "name", port_name, "interfaces");

    return row;
}

json_t *existing_bridge_row(char *br_name, char *port_name)
{
    char mac_str[MAC_STR_LEN];
    json_t *row = NULL;
    char datapath[128];
    os_macaddr_t mac;
    bool ret;

    memset(datapath, 0, sizeof(datapath));
    memset(mac_str, 0, sizeof(mac_str));

    ret = os_nif_macaddr_get(port_name, &mac);
    if (ret == false)
    {
        LOGN("%s(): failed to get interface mac address", __func__);
    }

    sprintf(mac_str, PRI_os_macaddr_plain_t, FMT(os_macaddr_pt, &mac));
    snprintf(datapath, sizeof(datapath), "%s%s", "0000", mac_str);

    row = json_pack("[[s, s, [s, [[s, s]]]]]", "ports", "insert", "set", "named-uuid",
                    NBM_PORT_TABLE_NAME);

    json_object_set_new(row, "datapath_id", json_string(datapath));
    json_object_set_new(row, "mcast_snooping_enable", json_boolean(true));
    json_object_set_new(row, "rstp_enable", json_boolean(false));

    return row;
}

json_t *bridge_port_where_row(char *br_name)
{
    json_t *row = NULL;

    row = json_pack("[[s, s, s]]", "name", "==", br_name);

    return row;
}

json_t *nm2_add_ports_table_params(char *br_name, char *port_name)
{
    json_t *jarray = NULL;

    /* jarray passed to ovsdb_tran_multi to will be NULL for the first transaction.
     * It will be appended in the subsequent calls. */
    jarray = ovsdb_tran_multi(jarray, nm2_interface_name(), SCHEMA_TABLE(Interface),
                              OTR_INSERT, NULL, existing_interface_row(port_name));

    jarray = ovsdb_tran_multi(jarray, nm2_port_name(), SCHEMA_TABLE(Port), OTR_INSERT, NULL,
                              port_row(port_name));

    jarray = ovsdb_tran_multi(jarray, NULL, SCHEMA_TABLE(Bridge), OTR_MUTATE,
                              bridge_port_where_row(br_name), existing_bridge_row(br_name, port_name));

    return jarray;
}

#define MAX_PATH_LEN 1024

void nm2_default_ports_create_tables(char *br_name)
{
    json_t *params;
    json_t *response;
    DIR *dir;
    struct dirent *de;
    char bridge_path[MAX_PATH_LEN] = {0};

    TRACE();

    snprintf(bridge_path, sizeof(bridge_path), "/sys/class/net/%s/brif", br_name);

    dir = opendir(bridge_path);
    if (dir == NULL)
    {
        LOG(DEBUG, "Failed to open directory %s", bridge_path);
        return;
    }

    while ((de = readdir(dir)) != NULL)
    {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        /* populate the ters required for table creation */
        params = nm2_add_ports_table_params(br_name, de->d_name);
        if (params == NULL)
        {
            LOG(ERR, "failed to populate parameters for table creation %s", de->d_name);
            continue;
        }

        /* send request to create tables OVSDB. ovsdb_method_send_s uses params as reference
         * and will be freed, no need to free here.
         */
        response = ovsdb_method_send_s(MT_TRANS, params);
        if (response == NULL)
        {
            LOG(ERR, "failed to create Interface, Port and Bridge tables");
            continue;
        }

        json_decref(response);
    }
}

/**
 * @brief creates default Interface, Port and Bridge tables.
 *
 * @return true on success, false on failure
 */
bool nm2_default_br_create_tables(char *br_name)
{
    json_t *params;
    json_t *response;

    TRACE();
    /* populate the parameters required for table creation */
    params = nm2_add_bridge_table_params(br_name);
    if (params == NULL)
    {
        LOG(ERR, "failed to populate parameters for table creation");
        return false;
    }

    /* send request to create tables OVSDB. ovsdb_method_send_s uses params as reference
     * and will be freed, no need to free here.
     */
    response = ovsdb_method_send_s(MT_TRANS, params);
    if (response == NULL)
    {
        LOG(ERR, "failed to create Interface, Port and Bridge tables");
        return false;
    }

    json_decref(response);
    return true;
}

void nm2_default_br_init(char *br_name)
{
    nm2_default_br_create_tables(br_name);
    nm2_default_ports_create_tables(br_name);
}
