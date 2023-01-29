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

#include <jansson.h>
#include <stdio.h>
#include <stdbool.h>

#include "adt_upnp_json_report.h"
#include "const.h"
#include "fsm.h"
#include "fsm_dpi_adt_upnp.h"
#include "json_mqtt.h"
#include "os_types.h"

#ifndef MAC_STR_LEN
#define MAC_STR_LEN         18
#endif /* MAC_STR_LEN */

/**
 * @brief encodes a upnp report into a json object
 *
 * @param session fsm session storing the header information
 * @param to_report upnp information to report
 * @return the json encoded information
 *
 * @remark Caller is responsible for ensuring parameters are not NULL
 * @remark Caller needs to free the JSON object through a json_decref() call.
 */
json_t *
json_adt_upnp_report(struct fsm_session *session,
                     struct adt_upnp_report *to_report)
{
    char str_mac[MAC_STR_LEN] = { 0 };
    struct adt_upnp_key_val *elem;
    json_t *body_envelope;
    json_t *json_report;
    os_macaddr_t *mac;
    json_t *body;
    char *label;
    int nelems;
    char *str;
    int i;

    json_report  = json_object();
    body_envelope = json_array();
    body = json_object();

    /* Encode header */
    jencode_header(session, json_report);

    /* Encode body */
    mac = &to_report->url->udev->device_mac;
    snprintf(str_mac, sizeof(str_mac),
             PRI(os_macaddr_t), FMT(os_macaddr_pt, mac));
    str = str_mac;
    json_object_set_new(body, "deviceMac", json_string(str));

    str = json_mqtt_get_network_id(session, mac);
    if (str != NULL) json_object_set_new(body, "networkZone", json_string(str));

    nelems = to_report->nelems;
    elem = to_report->first;
    for (i = 0; i < nelems; i++)
    {
        label = elem->key;
        str = elem->value;

        /* Verify the lengths of the strings are not 0 */
        if ((label[0] != '\0') && (str[0] != '\0'))
        {
            json_object_set_new(body, label, json_string(str));
        }

        elem++;
    }

    /* Encode body envelope */
    json_array_append_new(body_envelope, body);
    json_object_set_new(json_report, "upnpInfo", body_envelope);

    return json_report;
}

/**
 * @brief encodes a upnp report in json format
 *
 * @param session fsm session storing the header information
 * @param to_report upnp information to report
 * @return a string containing the json encoded information
 *
 * @remark Caller needs to free the string pointer through a json_free() call.
 */
char *
jencode_adt_upnp_report(struct fsm_session *session,
                        struct adt_upnp_report *to_report)
{
    json_t *json_report;
    char *json_msg;
    bool ready;

    if (session == NULL) return NULL;
    if (to_report == NULL) return NULL;

    ready = jcheck_header_info(session);
    if (ready == false) return NULL;

    json_report = json_adt_upnp_report(session, to_report);

    /* Convert json object in a compact string */
    json_msg = json_dumps(json_report, JSON_COMPACT);
    json_decref(json_report);

    return json_msg;
}
