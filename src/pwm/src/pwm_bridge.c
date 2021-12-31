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

#include "pwm_bridge.h"
#include "pwm_port.h"
#include "execsh.h"

#define MODULE_ID LOG_MODULE_ID_MISC
#define CMD_LEN 1024

struct pwm_bridge pwm_bridge =
{
    .created = false,
    .tag = -1,
    .ports = DS_DLIST_INIT(struct pwm_port, elt)
};

struct pwm_bridge *pwm_bridge_get_struct(void)
{
    return &pwm_bridge;
}

static struct pwm_port *pwm_get_port(const char *name)
{
    struct pwm_port *port;
    int ret;

    if (!name || !name[0]) {
        return NULL;
    }
    ds_dlist_foreach(&pwm_bridge.ports, port) {
        ret = strncmp(name, port->name, sizeof(port->name));
        if (ret == 0) {
            return port;
        }
    }
    return NULL;
}

static struct pwm_port *pwm_get_port_operator(void)
{
    struct pwm_port *port;

    ds_dlist_foreach(&pwm_bridge.ports, port) {
        if (port->type == PWM_PORT_TYPE_OPERATOR) {
            return port;
        }
    }
    return NULL;
}

bool pwm_bridge_add_port(const char *name, enum pwm_port_type type, int ofport_request)
{
    bool errcode;
    struct pwm_port *port;

    if (!name || !name[0]) {
        LOGE("Add port to PWM bridge: invalid argument");
        return false;
    }

    port = pwm_get_port(name);
    if (port) {
        LOGE("Add port to PWM bridge: port %s already exist", name);
        return false;
    }

    if (type == PWM_PORT_TYPE_OPERATOR)
    {
        port = pwm_get_port_operator();
        if (port) {
            LOGE("Add port to PWM bridge: operator port already exist");
            return false;
        }
    }

    errcode = pwm_port_create(name, type, pwm_bridge.tag, ofport_request);
    if (!errcode) {
        LOGE("Delete port from PWM bridge: delete port failed");
        return false;
    }

    LOGD("Port %s added to PWM %s bridge", name, CONFIG_PWM_BR_IF_NAME);
    return true;
}

bool pwm_bridge_delete_port(const char *name)
{
    bool errcode;
    struct pwm_port *port;

    if (!name || !name[0]) {
        LOGE("Delete port from PWM bridge: invalid argument");
        return false;
    }

    port = pwm_get_port(name);
    if (!port) {
        LOGI("Delete port from PWM bridge: port %s doesn't exist", name);
        return true;
    }

    errcode = pwm_port_delete(port);
    if (!errcode) {
        LOGE("Delete port from PWM bridge: delete port failed");
        return false;
    }

    LOGD("Port %s deleted from PWM %s bridge", name, CONFIG_PWM_BR_IF_NAME);
    return true;
}
