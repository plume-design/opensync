/*
* Copyright (c) 2020, Charter, Inc.
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

#include <pwm_bridge.h>
#include <pwm_port.h>
#include <pwm.h>
#include <os.h>
#include <log.h>
#include <stdio.h>

#define MODULE_ID LOG_MODULE_ID_MISC

struct pwm_bridge pwm_bridge = {
	.created = false,
	.tag = -1,
	.ports = DS_DLIST_INIT(struct pwm_port, elt)
};

static struct pwm_port *pwm_get_port(const char *name)
{
	struct pwm_port *port;

	if (!name || !name[0]) {
		return NULL;
	}
	ds_dlist_foreach(&pwm_bridge.ports, port) {
		if (!strncmp(name, port->name, sizeof(port->name))) {
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

static bool pwm_update_bridge_tag(int tag)
{
	int errcode;
	struct pwm_port *port;

	if (pwm_bridge.tag == tag) {
		return true;
	}
	pwm_bridge.tag = tag;

	ds_dlist_foreach(&pwm_bridge.ports, port) {
		errcode = pwm_update_port_tag(port, pwm_bridge.tag);
		if (!errcode) {
			LOGE("Update public WiFi bridge tag: update port tag failed");
			return false;
		}
	}
	return true;
}

static bool snmhm_add_bridge_openflow(void)
{
	int err;
	char cmd[1024];

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl del-flows %s", PWM_BR_IF_NAME);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Add Public WiFi bridge OpenFlow: delete all OpenFlows failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl add-flow %s \"table=0, dl_src=01:00:00:00:00:00/01:00:00:00:00:00, actions=drop\"",
			PWM_BR_IF_NAME);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Add Public WiFi bridge OpenFlow: add OpenFlow failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl add-flow %s \"table=0, dl_dst=01:80:c2:00:00:00/ff:ff:ff:ff:ff:f0, actions=drop\"",
			PWM_BR_IF_NAME);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Add Public WiFi bridge OpenFlow: add OpenFlow failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl add-flow %s \"table=0, priority=0, actions=resubmit(,1)\"",
			PWM_BR_IF_NAME);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Add Public WiFi bridge OpenFlow: add OpenFlow failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl add-flow %s \"table=1, priority=10, in_port=LOCAL, vlan_tci=0, actions=resubmit(,2)\"",
			PWM_BR_IF_NAME);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Add Public WiFi bridge OpenFlow: add OpenFlow failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl add-flow %s \"table=1, priority=0, actions=drop\"",
			PWM_BR_IF_NAME);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Add Public WiFi bridge OpenFlow: add OpenFlow failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl add-flow %s \"table=2, priority=0, actions=learn(table=10, hard_timeout=60, NXM_OF_VLAN_TCI[0..11], NXM_OF_ETH_DST[]=NXM_OF_ETH_SRC[], load:NXM_OF_IN_PORT[]->NXM_NX_REG0[0..15]), resubmit(,3)\"",
			PWM_BR_IF_NAME);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Add Public WiFi bridge OpenFlow: add OpenFlow failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl add-flow %s \"table=3, priority=0, actions=resubmit(,10), resubmit(,5)\"",
			PWM_BR_IF_NAME);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Add Public WiFi bridge OpenFlow: add OpenFlow failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl add-flow %s \"table=5, priority=0, actions=flood\"",
			PWM_BR_IF_NAME);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Add Public WiFi bridge OpenFlow: add OpenFlow failed");
		return false;
	}
	return true;
}

static bool snmhm_delete_bridge_openflow(void)
{
	int err;
	char cmd[1024];

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl del-flows %s", PWM_BR_IF_NAME);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Delete Public WiFi bridge OpenFlow: delete all OpenFlows failed");
		return false;
	}
	return true;
}

static bool pwm_create_bridge(void)
{
	bool errcode;
	int err;
	char cmd[1024];

	if (pwm_bridge.created) {
		return true;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-vsctl --may-exist add-br %s", PWM_BR_IF_NAME);
	cmd[sizeof(cmd) - 1] = '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Create Public WiFi bridge: create %s failed", PWM_BR_IF_NAME);
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ip link set dev %s up", PWM_BR_IF_NAME);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Create Public WiFi bridge: set %s up failed", PWM_BR_IF_NAME);
		return false;
	}

	errcode = snmhm_add_bridge_openflow();
	if (!errcode) {
		LOGE("Create Public WiFi bridge: add OpenFlow failed");
		return false;
	}

	pwm_bridge.tag = -1;
	pwm_bridge.created = true;
	LOGD("Public WiFi bridge %s created", PWM_BR_IF_NAME);
	return true;
}

static bool pwm_delete_bridge(void)
{
	bool errcode;
	struct pwm_port *port;
	char cmd[1024];
	int err;

	if (!pwm_bridge.created) {
		return true;
	}

	ds_dlist_foreach(&pwm_bridge.ports, port) {
		errcode = pwm_delete_port(port);
			if (!errcode) {
				LOGE("Delete Public WiFi bridge: delete port failed");
				return false;
			}
	}

	errcode = snmhm_delete_bridge_openflow();
	if (!errcode) {
		LOGE("Delete Public WiFi bridge: delete OpenFlow failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ip link set dev %s down", PWM_BR_IF_NAME);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Delete Public WiFi bridge: set %s down failed", PWM_BR_IF_NAME);
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-vsctl --if-exists del-br %s", PWM_BR_IF_NAME);
	cmd[sizeof(cmd) - 1] = '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Delete Public WiFi bridge: create %s failed", PWM_BR_IF_NAME);
		return false;
	}

	pwm_bridge.tag = -1;
	LOGD("Public WiFi bridge %s deleted", PWM_BR_IF_NAME);
	return true;
}

bool pwm_update_bridge(bool enable,struct schema_PublicWiFi* conf)
{
	bool errcode;

	if (enable && conf) {
		errcode = pwm_create_bridge();
		if (!errcode) {
			LOGE("Update Public WiFi bridge: create Public WiFi bridge failed");
			return false;
		}

		errcode = pwm_update_bridge_tag(conf->vlanid);
		if (!errcode) {
			LOGE("Update Public WiFi bridge: update tag failed");
			return false;
		}
		errcode = pwm_create_option82_proc(conf);
		if (!errcode) {
			LOGE("Update Public WiFi bridge: create option82 failed");
			return false;
		}
	} else {
		errcode = pwm_delete_bridge();
		if (!errcode) {
			LOGE("Update Public WiFi bridge: delete Public WiFi bridge failed");
			return false;
		}
		errcode = pwm_remove_option82_proc();
		if (!errcode) {
			LOGE("Update Public WiFi bridge:  remove option82 failed");
			return false;
		}
	}
	return true;
}

bool pwm_add_port_to_bridge(const char *name, enum pwm_port_type type)
{
	bool errcode;
	struct pwm_port *port;

	if (!name || !name[0]) {
		LOGE("Add port to Public WiFi bridge: invalid argument");
		return false;
	} else if (!pwm_bridge.created) {
		LOGE("Add port to Public WiFi bridge: bridge %s is not created", PWM_BR_IF_NAME);
		return false;
	}

	port = pwm_get_port(name);
	if (port) {
		LOGE("Add port to Public WiFi bridge: port %s already exist", name);
		return false;
	}

	if (type == PWM_PORT_TYPE_OPERATOR) {
		port = pwm_get_port_operator();
		if (port) {
			LOGE("Add port to Public WiFi bridge: operator port already exist");
			return false;
		}
	}

	errcode = pwm_create_port(name, type, pwm_bridge.tag);
	if (!errcode) {
		LOGE("Delete port from Public WiFi bridge: delete port failed");
		return false;
	}

	LOGD("Port %s added to Public WiFi %s bridge", name, PWM_BR_IF_NAME);
	return true;
}

bool pwm_delete_port_from_bridge(const char *name)
{
	bool errcode;
	struct pwm_port *port;

	if (!name || !name[0]) {
		LOGE("Delete port from Public WiFi bridge: invalid argument");
		return false;
	} else if (!pwm_bridge.created) {
		LOGD("Delete port from Public WiFi bridge: bridge %s is not created",
				PWM_BR_IF_NAME);
		return true;
	}

	port = pwm_get_port(name);
	if (!port) {
		LOGE("Delete port from Public WiFi bridge: port %s doesn't exist", name);
		return false;
	}

	errcode = pwm_delete_port(port);
	if (!errcode) {
		LOGE("Delete port from Public WiFi bridge: delete port failed");
		return false;
	}

	LOGD("Port %s deleted from Public WiFi %s bridge", name, PWM_BR_IF_NAME);
	return true;
}

