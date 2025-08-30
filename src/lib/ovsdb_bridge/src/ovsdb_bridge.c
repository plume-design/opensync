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
#include <stdbool.h>
#include <stdint.h>
#include <net/if.h>

#include "os.h"
#include "os_nif.h"
#include "target.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "ovsdb_bridge.h"
#include "hw_acc.h"

#ifndef MAC_STR_LEN
#define MAC_STR_LEN 18
#endif /* MAC_STR_LEN */

#define NBM_BRIDGE_TABLE_NAME    "newbridge"
#define NBM_PORT_TABLE_NAME      "newport"
#define NBM_INTERFACE_TABLE_NAME "newinterface"

enum timeout_cmd_type
{
    WITHOUT_T_SWITCH,
    WITH_T_SWITCH,
};

static enum timeout_cmd_type ovsdb_bridge_get_timeout_cmd_type(void)
{
    static bool checked;
    static enum timeout_cmd_type cmd_type;
    if (!checked)
    {
        const char *result = strexa("timeout", "-t", "0", "true");
        const bool success = (result != NULL);
        cmd_type = (success ? WITH_T_SWITCH : WITHOUT_T_SWITCH);
    }
    return cmd_type;
}

/**
 * @brief checks if a bridge with @p br_name already exists
 * in the Bridge table
 *
 * @param br_name   Bridge name
 *
 * @return true if bridge exists, false otherwise
 */
static bool ovsdb_bridge_br_exists(const char *br_name)
{
    json_t *where;

    where = json_array();
    json_array_append_new(where, ovsdb_tran_cond_single("name", OFUNC_EQ, br_name));

    json_t *rows = ovsdb_sync_select_where(SCHEMA_TABLE(Bridge), where);
    json_decref(rows);
    if (rows != NULL) return true;

    return false;
}

/**
 * @brief sets the uuid-name for the Interface table
 *
 * @return json object containing uuid-name
 */
static json_t *ovsdb_bridge_interface_name(void)
{
    json_t *obj = json_object();

    json_object_set_new(obj, "uuid-name", json_string(NBM_INTERFACE_TABLE_NAME));
    return obj;
}

/**
 * @brief sets the uuid-name for the Port table
 *
 * @return json object containing uuid-name
 */
static json_t *ovsdb_bridge_port_name(void)
{
    json_t *obj = json_object();

    json_object_set_new(obj, "uuid-name", json_string(NBM_PORT_TABLE_NAME));

    return obj;
}

/**
 * @brief sets the uuid-name for the Bridge table
 *
 * @return json object containing uuid-name
 */
static json_t *ovsdb_bridge_br_name(void)
{
    json_t *obj = json_object();

    json_object_set_new(obj, "uuid-name", json_string(NBM_BRIDGE_TABLE_NAME));
    return obj;
}

/**
 * @brief creates the default parameters for creating the Interface table.
 *
 * @param if_name   Interface name
 *
 * @return json object specifying default Interface table vales
 */
static json_t *ovsdb_bridge_interface_row(const char *if_name)
{
    struct schema_Interface interface;
    pjs_errmsg_t errmsg;
    json_t *row = NULL;
    int ifindex;

    ifindex = if_nametoindex(if_name);
    if (ifindex == 0) LOGT("%s(): failed to find interface index for %s", __func__, if_name);

    /* Populate default Interface configuration fields */
    memset(&interface, 0, sizeof(interface));
    SCHEMA_SET_STR(interface.name, if_name);
    SCHEMA_SET_STR(interface.admin_state, "up");
    SCHEMA_SET_INT(interface.ifindex, ifindex);
    SCHEMA_SET_STR(interface.link_state, "up");
    SCHEMA_SET_STR(interface.type, "internal");

    /* Convert Interface schema to json object */
    row = schema_Interface_to_json(&interface, errmsg);
    if (row == NULL)
    {
        LOGE("%s: error converting Interface schema structure to JSON.", __func__);
        return row;
    }

    return row;
}

/**
 * @brief creates the default parameters for creating the Port table.
 *
 * @param port_name   Port name
 *
 * @return json object specifying default Port table vales
 */
static json_t *ovsdb_bridge_port_row(const char *port_name)
{
    json_t *row = NULL;

    row = json_pack("{s : s, s : [s, s]}", "name", port_name, "interfaces", "named-uuid", NBM_INTERFACE_TABLE_NAME);

    return row;
}

/**
 * @brief creates the default parameters for creating the Bridge table.
 *
 * @param br_name   Bridge name
 *
 * @return json object specifying default Bridge table vales
 */
static json_t *ovsdb_bridge_br_row(const char *br_name)
{
    char mac_str[MAC_STR_LEN];
    json_t *row = NULL;
    char datapath[128];
    os_macaddr_t mac;
    bool ret;

    memset(datapath, 0, sizeof(datapath));
    memset(mac_str, 0, sizeof(mac_str));

    ret = os_nif_macaddr_get(br_name, &mac);
    if (ret == false) LOGN("%s(): failed to get interface mac address", __func__);

    sprintf(mac_str, PRI_os_macaddr_plain_t, FMT(os_macaddr_pt, &mac));
    snprintf(datapath, sizeof(datapath), "%s%s", "0000", mac_str);

    row = json_pack("{s : s, s : [s, s]}", "name", br_name, "ports", "named-uuid", NBM_PORT_TABLE_NAME);

    json_object_set_new(row, "datapath_id", json_string(datapath));
    json_object_set_new(row, "mcast_snooping_enable", json_boolean(true));
    json_object_set_new(row, "rstp_enable", json_boolean(false));
    json_object_set_new(row, "stp_enable", json_boolean(true));

    return row;
}

/**
 * @brief creates the row parameter value for modifying the Open_vSwitch table.
 * Open_vSwitch table's "bridges" will be set to the default bridge name
 *
 * @return json object specifying the bridges value
 */
static json_t *ovsdb_bridge_ovswitch_row(void)
{
    json_t *row = NULL;

    row = json_pack("[[s, s, [s, [[s, s]]]]]", "bridges", "insert", "set", "named-uuid", NBM_BRIDGE_TABLE_NAME);
    return row;
}

/**
 * @brief populates Interface, Port and Bridge parameters
 * required for creating the corresponding tables.
 *
 * @param br_name   Bridge name
 *
 * @return None
 */
static json_t *ovsdb_bridge_add_br_table_params(const char *br_name)
{
    json_t *jarray = NULL;

    /*
     * 'jarray' passed to 'ovsdb_tran_multi' will be NULL for the first
     * transaction. It will be appended in subsequent calls.
     */
    jarray = ovsdb_tran_multi(
            jarray,
            ovsdb_bridge_interface_name(),
            SCHEMA_TABLE(Interface),
            OTR_INSERT,
            NULL,
            ovsdb_bridge_interface_row(br_name));

    jarray = ovsdb_tran_multi(
            jarray,
            ovsdb_bridge_port_name(),
            SCHEMA_TABLE(Port),
            OTR_INSERT,
            NULL,
            ovsdb_bridge_port_row(br_name));

    jarray = ovsdb_tran_multi(
            jarray,
            ovsdb_bridge_br_name(),
            SCHEMA_TABLE(Bridge),
            OTR_INSERT,
            NULL,
            ovsdb_bridge_br_row(br_name));

    jarray = ovsdb_tran_multi(jarray, NULL, SCHEMA_TABLE(Open_vSwitch), OTR_MUTATE, NULL, ovsdb_bridge_ovswitch_row());

    return jarray;
}

bool ovsdb_bridge_create(const char *br_name)
{
    json_t *params;
    json_t *response;

    TRACE();

    if (ovsdb_bridge_br_exists(br_name)) return true;

    /* Populate the parameters required for table creation */
    params = ovsdb_bridge_add_br_table_params(br_name);
    if (params == NULL)
    {
        LOGE("%s: Failed to populate parameters for Interface, Port and Bridge table creation", __func__);
        return false;
    }

    /*
     * Send request to create OVSDB tables. 'ovsdb_method_send_s' uses 'params' as
     * a reference and will free the memory so there is no need to free here.
     */
    response = ovsdb_method_send_s(MT_TRANS, params);
    if (response == NULL)
    {
        LOGE("%s: Failed to create Interface, Port and Bridge tables", __func__);
        return false;
    }
    json_decref(response);

    return true;
}

/**
 * @brief sets the where parameter for any
 * json value.
 *
 * @return json object containing uuid-name
 */
static json_t *ovsdb_bridge_set_where(const char *name)
{
    json_t *row = NULL;

    row = json_pack("[[s, s, s]]", "name", "==", name);
    return row;
}

static bool ovsdb_bridge_port_exists(const char *port_name)
{
    json_t *where;

    where = json_array();
    json_array_append_new(where, ovsdb_tran_cond_single("name", OFUNC_EQ, port_name));

    json_t *rows = ovsdb_sync_select_where(SCHEMA_TABLE(Port), where);
    json_decref(rows);
    if (rows != NULL) return true;

    return false;
}

/**
 * @brief gets the required "input" from the
 * given json  string.
 *
 * @return json structure populated.
 */
static json_t *ovsdb_bridge_get_obj_from_json(json_t *root, const char *input)
{
    json_t *arr = NULL;
    json_t *obj = NULL;
    size_t i;

    for (i = 0; i < json_array_size(root); i++)
    {
        arr = json_array_get(root, i);

        if (!arr) return NULL;

        obj = json_object_get(arr, input);
        if (!obj) return NULL;
    }
    return obj;
}

/**
 * @brief populates the json with parameters
 * which hold the transaction string to perform
 * deleting port from bridge.
 * Uses the provided json string to extract
 * uuid and set it in the transaction string.
 *
 * @return json structure populated.
 */
static json_t *ovsdb_bridge_get_del_params_from_response(json_t *response)
{
    json_t *rows;
    json_t *uuid;
    json_t *del;
    char *uuid_t;
    char *uuid_s;

    rows = ovsdb_bridge_get_obj_from_json(response, "rows");
    if (!rows) return NULL;

    uuid = ovsdb_bridge_get_obj_from_json(rows, "_uuid");
    if (!uuid) return NULL;

    json_unpack(uuid, "[s,s]", &uuid_t, &uuid_s);
    del = json_pack("[[s, s,[s,[[s, s]]]]]", "ports", "delete", "set", uuid_t, uuid_s);

    return del;
}

static json_t *ovsdb_bridge_existing_interface_row(const char *port_name)
{
    json_t *row = NULL;

    row = json_pack("{s : s}", "name", port_name, "interfaces");

    return row;
}

static json_t *ovsdb_bridge_where_port_row(const char *br_name)
{
    json_t *row = NULL;

    row = json_pack("[[s, s, s]]", "name", "==", br_name);

    return row;
}

static json_t *ovsdb_bridge_existing_bridge_row(const char *br_name, const char *port_name)
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

    row = json_pack("[[s, s, [s, [[s, s]]]]]", "ports", "insert", "set", "named-uuid", NBM_PORT_TABLE_NAME);

    json_object_set_new(row, "datapath_id", json_string(datapath));
    json_object_set_new(row, "mcast_snooping_enable", json_boolean(true));
    json_object_set_new(row, "rstp_enable", json_boolean(false));

    return row;
}

static bool ovsdb_bridge_add_port_to_br_tables(const char *port_name, const char *br_name)
{
    json_t *jarray = NULL;
    json_t *response;

    jarray = ovsdb_tran_multi(
            jarray,
            ovsdb_bridge_interface_name(),
            SCHEMA_TABLE(Interface),
            OTR_INSERT,
            NULL,
            ovsdb_bridge_existing_interface_row(port_name));

    jarray = ovsdb_tran_multi(
            jarray,
            ovsdb_bridge_port_name(),
            SCHEMA_TABLE(Port),
            OTR_INSERT,
            NULL,
            ovsdb_bridge_port_row(port_name));

    jarray = ovsdb_tran_multi(
            jarray,
            NULL,
            SCHEMA_TABLE(Bridge),
            OTR_MUTATE,
            ovsdb_bridge_where_port_row(br_name),
            ovsdb_bridge_existing_bridge_row(br_name, port_name));

    /*
     * Send request to create entries in Port and Interface OVSDB tables and update
     * the UUID reference in Bridge. ovsdb_method_send_s uses params as reference
     * and will be freed, no need to free here.
     */
    response = ovsdb_method_send_s(MT_TRANS, jarray);
    if (response == NULL)
    {
        LOGE("%s: Failed to add port [%s] to bridge [%s]", __func__, port_name, br_name);
        return false;
    }
    json_decref(response);

    return true;
}

static bool ovsdb_bridge_remove_port_from_br_tables(const char *port_name, const char *br_name)
{
    json_t *params;
    json_t *response;

    /* First get UUID of port to be deleted */
    params = ovsdb_tran_multi(NULL, NULL, SCHEMA_TABLE(Port), OTR_SELECT, ovsdb_bridge_set_where(port_name), NULL);
    response = ovsdb_method_send_s(MT_TRANS, params);
    if (response == NULL)
    {
        LOGE("%s: Failed to get port UUID", __func__);
        return false;
    }

    /* Then delete Bridge OVSDB table entry */
    params = ovsdb_tran_multi(
            NULL,
            NULL,
            SCHEMA_TABLE(Bridge),
            OTR_MUTATE,
            ovsdb_bridge_set_where(br_name),
            ovsdb_bridge_get_del_params_from_response(response));
    json_decref(response);
    response = ovsdb_method_send_s(MT_TRANS, params);
    if (response == NULL)
    {
        LOGE("%s: Failed to remove port [%s] from bridge [%s]", __func__, port_name, br_name);
        return false;
    }
    json_decref(response);

    return true;
}

static void ovsdb_bridge_port_brctl_cmd(const char *port_name, const char *br_name, bool add)
{
    char *op = "";

    if (add)
        op = "addif";
    else
        op = "delif";

    const char *result = (ovsdb_bridge_get_timeout_cmd_type() == WITHOUT_T_SWITCH)
                                 ? strexa("timeout", "10", "brctl", op, br_name, port_name)
                                 : strexa("timeout", "-t", "10", "brctl", op, br_name, port_name);

    LOGI("%s: bridge=%s port_name=%s op=%s result=%s",
         __func__,
         br_name,
         port_name,
         op,
         (result == NULL ? "failed" : "success"));

    hw_acc_flush_all_flows();
}

bool ovsdb_bridge_manage_port(const char *port_name, const char *br_name, bool add)
{
    bool port_exists;
    bool ret;

    port_exists = ovsdb_bridge_port_exists(port_name);

    /* Return if adding an existing port or deleting a non-existent port */
    if ((add && port_exists) || (!add && !port_exists))
    {
        LOGI("Port [%s] %s", port_name, port_exists ? "already exists" : "does not exist");
        return true;
    }

    LOGI("%s port [%s] in bridge [%s]", add ? "Adding" : "Removing", port_name, br_name);

    /*
     * Call brctl commands as a temporary measure to ensure port_name gets added/removed to
     * bridge_name before CM continues. The correct approach would be to only update OVSDB
     * tables and let NM run those commands, however we have no mechanism in place to allow
     * NM to signal its completion. Therefore it is better to run the same commands twice
     * than letting CM continue to run on the false assumption that the bridge has already
     * been updated. This workaround may be removed once NM is fixed to properly update
     * Open_vSwitch table fields cur_cfg and next_cfg as a way to signal its completion.
     */
    ovsdb_bridge_port_brctl_cmd(port_name, br_name, add);

    if (add)
        ret = ovsdb_bridge_add_port_to_br_tables(port_name, br_name);
    else
        ret = ovsdb_bridge_remove_port_from_br_tables(port_name, br_name);

    if (!ret)
    {
        LOGE("Failed to %s port [%s] in bridge [%s]", add ? "add" : "remove", port_name, br_name);
        /* Revert 'brctl' command if OVSDB transaction was unsuccessful */
        ovsdb_bridge_port_brctl_cmd(port_name, br_name, !add);
        return false;
    }

    return true;
}

char *ovsdb_bridge_port_get_bridge(const char *port_name)
{
    json_t *where = json_pack("[[s, s, s]]", "name", "==", port_name);
    json_t *rows = ovsdb_sync_select_where(SCHEMA_TABLE(Port), where);
    json_t *row = json_array_get(rows, 0);
    struct schema_Port port;
    pjs_errmsg_t err;
    const bool port_ok = row && schema_Port_from_json(&port, row, false, err);
    json_decref(rows);
    if (port_ok == false) return NULL;

    where = json_pack("[[s, s, [s, s]]]", "ports", "includes", "uuid", port._uuid.uuid);
    rows = ovsdb_sync_select_where(SCHEMA_TABLE(Bridge), where);
    row = json_array_get(rows, 0);
    struct schema_Bridge br;
    const bool br_ok = row && schema_Bridge_from_json(&br, row, false, err);
    json_decref(rows);
    if (br_ok == false) return NULL;

    return STRDUP(br.name);
}
