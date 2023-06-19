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

#include "cm2.h"
#include "json_util.h"
#include "ovsdb.h"
#include "ovsdb_sync.h"
#include "os.h"
#include "os_nif.h"

#define NBM_PORT_TABLE_NAME "newport"
#define NBM_INTERFACE_TABLE_NAME "newinterface"

/**
 * @brief creates the default parameters for creating Interface table.
 * @return json object specifing default Interface table vales
 */
static json_t *interface_row(char *if_name)
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
static json_t *port_row(char *br_name)
{
    json_t *row = NULL;

    row = json_pack("{s : s, s : [s, s]}", "name", br_name, "interfaces",
                    "named-uuid", NBM_INTERFACE_TABLE_NAME);

    return row;
}

/**
 * @brief creates the default parameters for creating Bridge table.
 * @return json object specifing default Bridge table vales
 */
static json_t *bridge_row(char *br_name)
{
    json_t *row = NULL;

    row = json_pack("[[s, s, [s, [[s, s]]]]]", "ports", "insert", "set", "named-uuid", NBM_PORT_TABLE_NAME);
    return row;
}


/**
 * @brief sets the uuid-name for the interface table
 * @return json object containing uuid-name
 */
static json_t *cm2_interface_name(void)
{
    json_t *obj = json_object();

    json_object_set_new(obj, "uuid-name", json_string(NBM_INTERFACE_TABLE_NAME));
    return obj;
}

/**
 * @brief sets the uuid-name for the port table
 * @return json object containing uuid-name
 */
static json_t *cm2_port_name(void)
{
    json_t *obj = json_object();

    json_object_set_new(obj, "uuid-name", json_string(NBM_PORT_TABLE_NAME));
    return obj;
}


/**
 * @brief sets the where parameter for any
 * json value.
 * @return json object containing uuid-name
 */
static json_t *cm2_set_where(char *name)
{
    json_t *row = NULL;

    row = json_pack("[[s, s, s]]", "name", "==", name);
    return row;
}


/**
 * @brief populates Interface and Port tables.
 * The newly populated tables are referred in Bridge.
 * @return json structure of the result.
 */

static json_t *cm2_add_port_to_br_table_params(char *port_name, char *br_name)
{
    json_t *jarray = NULL;

    /* jarray passed to ovsdb_tran_multi to will be NULL for the first transaction.
     * It will be appended in the subsequent calls. */
    jarray = ovsdb_tran_multi(jarray, cm2_interface_name(), SCHEMA_TABLE(Interface),
                              OTR_INSERT, NULL, interface_row(port_name));

    jarray = ovsdb_tran_multi(jarray, cm2_port_name(), SCHEMA_TABLE(Port), OTR_INSERT, NULL,
                              port_row(port_name));

    jarray = ovsdb_tran_multi(jarray, NULL, SCHEMA_TABLE(Bridge), OTR_MUTATE,
                              cm2_set_where(br_name), bridge_row(br_name));

    return jarray;
}


/**
 * @brief populates the port table query with
 * given port name.
 * @return json structure populated.
 */
static json_t *cm2_port_uuid_params(char *port_name)
{
    json_t *jarray = NULL;

    jarray = ovsdb_tran_multi(jarray, NULL, SCHEMA_TABLE(Port), OTR_SELECT,
                              cm2_set_where(port_name), NULL);
    return jarray;
}


/**
 * @brief gets the required "input" from the
 * given json  string.
 * @return json structure populated.
 */
static json_t *cm2_get_obj_from_json(json_t *root, char *input)
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
 * @return json structure populated.
 */
static json_t *cm2_get_del_params_from_response(json_t *response)
{
    json_t *rows;
    json_t *uuid;
    json_t *del;
    char *uuid_t;
    char *uuid_s;

    rows = cm2_get_obj_from_json(response, "rows");
    if (!rows) return NULL;

    uuid = cm2_get_obj_from_json(rows, "_uuid");
    if (!uuid) return NULL;

    json_unpack(uuid, "[s,s]", &uuid_t,  &uuid_s);
    del = json_pack("[[s, s,[s,[[s, s]]]]]", "ports", "delete", "set", uuid_t, uuid_s);

    return del;
}


/**
 * @brief populates the json with parameters
 * required to delete a port from bridge table.
 * @return json structure populated.
 */
static json_t * cm2_del_port_from_br_params(json_t *response, char *br_name)
{
    json_t *jarray = NULL;

    jarray = ovsdb_tran_multi(jarray, NULL, SCHEMA_TABLE(Bridge), OTR_MUTATE,
                              cm2_set_where(br_name), cm2_get_del_params_from_response(response));
    return jarray;
}


/**
 * @brief creates  Interface and Port tables.
 * The newly created Port is referred to the
 * provided bridge.
 * @return true on success, false on failure
 */
bool cm2_add_port_to_br(char *port_name, char *br_name)
{
    json_t *params;
    json_t *response;
    json_t *row;
    bool   row_exists;

    /* Check if port already exists.*/
    row = ovsdb_sync_select_where(SCHEMA_TABLE(Port), cm2_set_where(port_name));
    row_exists = (row != NULL);
    json_decref(row);
    row = NULL;
    if (row_exists) return false;

    /* populate the parameters required for table creation */
    params = cm2_add_port_to_br_table_params(port_name, br_name);
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


/**
 * @brief Looksup the uuid of the provied port.
 * The retrieved uuid is used to remove it
 * from the provided bridge.
 * @return true on success, false on failure
 */
bool cm2_del_port_from_br(char *port_name, char *br_name)
{
    json_t *params;
    json_t *response;
    json_t *row;
    bool row_exists = false;

    /* Check if port exists.*/
    row = ovsdb_sync_select_where(SCHEMA_TABLE(Port), cm2_set_where(port_name));
    row_exists = (row != NULL);
    json_decref(row);
    row = NULL;
    if (row_exists == false) return false;


    params = cm2_port_uuid_params(port_name);

    response = ovsdb_method_send_s(MT_TRANS, params);
    if (response == NULL)
    {
        LOG(ERR, "failed to create Interface, Port and Bridge tables");
        return false;
    }

    params = cm2_del_port_from_br_params(response, br_name);

    json_decref(response);
    response = ovsdb_method_send_s(MT_TRANS, params);
    if (response == NULL)
    {
        LOG(ERR, "failed to create Interface, Port and Bridge tables");
        return false;
    }

    json_decref(response);
    return true;
}
