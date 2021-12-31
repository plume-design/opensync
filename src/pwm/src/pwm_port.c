/*
 * Copyright (c) 2021, Sagemcom.
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

#include <stdio.h>

#include "os.h"
#include "log.h"

#include "pwm_port.h"
#include "pwm_bridge.h"

#define MODULE_ID LOG_MODULE_ID_MISC
#define CMD_LEN 1024

static bool pwm_port_add_ovs(struct pwm_port *port)
{
    int err;
    char cmd[CMD_LEN];

    if (port->added) {
        return true;
    }

    if (port->ofport_request == PWM_PORT_EMPTY_OFPORT) {
        snprintf(cmd, CMD_LEN - 1, "ovs-vsctl --may-exist add-port %s %s", CONFIG_PWM_BR_IF_NAME, port->name);
    } else {
        LOGD("Adding ofport_request %d to port %s", port->ofport_request, port->name);
        snprintf(cmd, CMD_LEN - 1,
                 "ovs-vsctl --may-exist add-port %1$s %2$s \
                 -- set interface %2$s ofport_request=%3$d",
                 CONFIG_PWM_BR_IF_NAME, port->name, port->ofport_request);
    }

    err = cmd_log(cmd);
    if (err) {
        LOGE("Add OVS port to PWM bridge: add %s to %s failed", port->name, CONFIG_PWM_BR_IF_NAME);
        return false;
    }

    port->added = true;
    LOGD("OVS port %s added to PWM %s bridge", port->name, CONFIG_PWM_BR_IF_NAME);
    return true;
}

static bool pwm_port_delete_ovs(struct pwm_port *port)
{
    int err;
    char cmd[CMD_LEN];

    if (!port->added) {
        return true;
    }

    snprintf(cmd, CMD_LEN - 1, "ovs-vsctl --if-exists del-port %s %s", CONFIG_PWM_BR_IF_NAME, port->name);
    err = cmd_log(cmd);
    if (err) {
        LOGE("Delete OVS port from PWM bridge: add %s to %s failed", port->name, CONFIG_PWM_BR_IF_NAME);
        return false;
    }

    port->added = false;
    LOGD("OVS port %s deleted from PWM %s bridge", port->name, CONFIG_PWM_BR_IF_NAME);
    return true;
}

static bool pwm_port_update_ofid(struct pwm_port *port)
{
    bool errcode = true;
    char cmd[CMD_LEN];
    FILE *result = NULL;
    int err;

    snprintf(cmd, CMD_LEN - 1,
             "ovs-vsctl --timeout=3 wait-until Interface %s \"ofport>=0\"",
             port->name);
    cmd_log(cmd);

    snprintf(cmd, CMD_LEN - 1, "ovs-vsctl get Interface %s ofport", port->name);
    result = popen(cmd, "r");
    if (!result) {
        LOGE("Update %s OpenFlow identifier: pipe failed", port->name);
        errcode = false;
        goto out;
    }

    err = fscanf(result, "%d", &port->ofid);
    if (err != 1) {
        LOGE("Update %s OpenFlow identifier: read file failed", port->name);
        errcode = false;
        goto out;
    } else if (port->ofid < 0) {
        LOGE("Update %s OpenFlow identifier: invalid OpenFlow identifier %d", port->name, port->ofid);
        errcode = true;
        goto out;
    }

    LOGD("Update OpenFlow identifier: %s has %d", port->name, port->ofid);
out:
    if (result) {
        pclose(result);
    }
    return errcode;
}

static bool pwm_port_init(struct pwm_port *port,
                          const char *name,
                          enum pwm_port_type type,
                          int tag,
                          int ofport_request)
{
    bool errcode;
    struct pwm_bridge *stPwBridge;

    stPwBridge = pwm_bridge_get_struct();
    ds_dlist_insert_tail(&stPwBridge->ports, port);
    STRSCPY_WARN(port->name, name);
    port->name[sizeof(port->name) - 1] = '\0';
    port->ofid = -1;
    port->isolated = false;
    port->added = false;
    port->type = type;
    port->tag = tag;
    port->ofport_request = ofport_request;

    errcode = pwm_port_add_ovs(port);
    if (!errcode) {
        LOGE("Initialize PWM bridge port: add OVS port failed");
        return false;
    }

    errcode = pwm_port_update_ofid(port);
    if (!errcode) {
        LOGE("Initialize PWM bridge port: update openFlow identifier failed");
        return false;
    }

    return true;
}

static bool pwm_port_deinit(struct pwm_port *port)
{
    bool errcode;
    struct pwm_bridge *stPwBridge;

    errcode = pwm_port_delete_ovs(port);
    if (!errcode) {
        LOGE("Deinitialize PWM bridge port: delete OVS port failed");
        return false;
    }
    stPwBridge = pwm_bridge_get_struct();
    ds_dlist_remove(&stPwBridge->ports, port);
    return true;
}

bool pwm_port_delete(struct pwm_port *port)
{
    bool errcode;

    errcode = pwm_port_deinit(port);
    if (!errcode) {
        LOGE("Delete PWM bridge port: deinitialize port failed");
        return false;
    }
    free(port);

    return true;
}

bool pwm_port_create(const char *name, enum pwm_port_type type, int tag, int ofport_request)
{
    bool errcode;
    struct pwm_port *port;

    port = (struct pwm_port *) malloc(sizeof(*port));
    if (!port) {
        LOGE("Create PWM bridge port: memeory allocation failed");
        return false;
    }

    errcode = pwm_port_init(port, name, type, tag, ofport_request);
    if (!errcode) {
        LOGE("Create PWM bridge port: initialize port failed");
        pwm_port_delete(port);
        return false;
    }

    return true;
}
