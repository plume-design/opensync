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

#include <pwm_port.h>
#include <pwm_bridge.h>
#include <pwm.h>
#include <os.h>
#include <log.h>
#include <stdio.h>

#define MODULE_ID LOG_MODULE_ID_MISC

static bool pwm_add_ovs_port(struct pwm_port *port)
{
	int err;
	char cmd[1024];

	if (port->added) {
		return true;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-vsctl --may-exist add-port %s %s",
			PWM_BR_IF_NAME, port->name);
	cmd[sizeof(cmd) - 1] = '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Add OVS port to Public WiFi bridge: add %s to %s failed",
				port->name, PWM_BR_IF_NAME);
		return false;
	}

	port->added = true;
	LOGD("OVS port %s added to Public WiFi %s bridge", port->name, PWM_BR_IF_NAME);
	return true;
}

static bool pwm_delete_ovs_port(struct pwm_port *port)
{
	int err;
	char cmd[1024];

	if (!port->added) {
		return true;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-vsctl --if-exists del-port %s %s",
			PWM_BR_IF_NAME, port->name);
	cmd[sizeof(cmd) - 1] = '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Delete OVS port from Public WiFi bridge: add %s to %s failed",
				port->name, PWM_BR_IF_NAME);
		return false;
	}

	port->added = false;
	LOGD("OVS port %s deleted from Public WiFi %s bridge", port->name, PWM_BR_IF_NAME);
	return true;
}

static bool pwm_update_port_ofid(struct pwm_port *port)
{
	bool errcode = true;
	char cmd[1024];
	FILE *result = NULL;
	int err;

	snprintf(cmd, sizeof(cmd) - 1, "ovs-vsctl --timeout=3 wait-until Interface %s \"ofport>=0\"",
			port->name);
	cmd[sizeof(cmd) - 1] = '\0';
	cmd_log(cmd);

	snprintf(cmd, sizeof(cmd) - 1, "ovs-vsctl get Interface %s ofport", port->name);
	cmd[sizeof(cmd) - 1] = '\0';
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
		LOGE("Update %s OpenFlow identifier: invalid OpenFlow identifier %d",
				port->name, port->ofid);
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

static bool pwm_isolate_customer_port(struct pwm_port *port)
{
	char cmd[1024];
	int err;

	if (port->isolated) {
		return true;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl add-flow %s \"table=1, priority=10, in_port=%d, vlan_tci=0, actions=resubmit(,2)\"",
			PWM_BR_IF_NAME, port->ofid);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Isolate Public WiFi customer port: add OpenFlow failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl add-flow %s \"table=3, priority=10, in_port=%d, actions=resubmit(,4)\"",
			PWM_BR_IF_NAME, port->ofid);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Isolate Public WiFi customer port: add OpenFlow failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl add-flow %s \"table=5, priority=20, reg0=%d, in_port=LOCAL, actions=output:%d\"",
			PWM_BR_IF_NAME, port->ofid, port->ofid);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Isolate Public WiFi customer port: add OpenFlow failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl add-flow %s \"table=5, priority=10, reg0=%d, actions=output:LOCAL, output:%d\"",
			PWM_BR_IF_NAME, port->ofid, port->ofid);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Isolate Public WiFi customer port: add OpenFlow failed");
		return false;
	}

	port->isolated = true;
	LOGD("Public WiFi icustomer port %s is isolated", port->name);
	return true;
}

static bool pwm_isolate_operator_port(struct pwm_port *port)
{
	char cmd[1024];
	int err;

	if (port->isolated) {
		return true;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl add-flow %s \"table=1, priority=10, in_port=%d, dl_vlan=%d, actions=strip_vlan, resubmit(,3)\"",
			PWM_BR_IF_NAME, port->ofid, port->tag);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Isolate Public WiFi operator port: add OpenFlow failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl add-flow %s \"table=4, priority=0, actions=output:LOCAL, mod_vlan_vid:%d, output:%d\"",
			PWM_BR_IF_NAME, port->tag, port->ofid);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Isolate Public WiFi operator port: add OpenFlow failed");
		return false;
	}

	port->isolated = true;
	LOGD("Public WiFi operator port %s is isolated", port->name);
	return true;
}

static bool pwm_isolate_port(struct pwm_port *port)
{
	bool errcode;

	if (port->type == PWM_PORT_TYPE_OPERATOR) {
		errcode = pwm_isolate_operator_port(port);
		if (!errcode) {
			LOGE("Isolate Public WiFi port: isolate operator port failed");
			return false;
		}
	} else {
		errcode = pwm_isolate_customer_port(port);
		if (!errcode) {
			LOGE("Isolate Public WiFi port: isolate customer port failed");
			return false;
		}
	}
	return true;
}

static bool pwm_deisolate_customer_port(struct pwm_port *port)
{
	char cmd[1024];
	int err;

	if (!port->isolated) {
		return true;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl del-flows %s \"table=1, priority=10, in_port=%d, vlan_tci=0\" --strict",
			PWM_BR_IF_NAME, port->ofid);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Deisolate Public WiFi customer port: delete OpenFlow failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl del-flows %s \"table=3, priority=10, in_port=%d\" --strict",
			PWM_BR_IF_NAME, port->ofid);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Deisolate Public WiFi customer port: delete OpenFlow failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl del-flows %s \"table=5, priority=20, reg0=%d, in_port=LOCAL\" --strict",
			PWM_BR_IF_NAME, port->ofid);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Deisolate Public WiFi customer port: delete OpenFlow failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl del-flows %s \"table=5, priority=10, reg0=%d\" --strict",
			PWM_BR_IF_NAME, port->ofid);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Deisolate Public WiFi customer port: delete OpenFlow failed");
		return false;
	}

	port->isolated = false;
	LOGD("Public WiFi customer port %s is deisolated", port->name);
	return true;
}

static bool pwm_deisolate_operator_port(struct pwm_port *port)
{
	char cmd[1024];
	int err;

	if (!port->isolated) {
		return true;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl del-flows %s \"table=1, priority=10, in_port=%d, dl_vlan=%d\" --strict",
			PWM_BR_IF_NAME, port->ofid, port->tag);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Deisolate Public WiFi operator port: delete OpenFlow failed");
		return false;
	}

	snprintf(cmd, sizeof(cmd) - 1, "ovs-ofctl del-flows %s \"table=4, priority=0\"",
			PWM_BR_IF_NAME);
	cmd[sizeof(cmd) - 1]= '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Isolate Public WiFi operator port: delete OpenFlow failed");
		return false;
	}

	port->isolated = false;
	LOGD("Public WiFi operator port %s is deisolated", port->name);
	return true;
}

static bool pwm_deisolate_port(struct pwm_port *port)
{
	bool errcode;

	if (port->type == PWM_PORT_TYPE_OPERATOR) {
		errcode = pwm_deisolate_operator_port(port);
		if (!errcode) {
			LOGE("Deisolate Public WiFi port: deisolate operator port failed");
			return false;
		}
	} else {
		errcode = pwm_deisolate_customer_port(port);
		if (!errcode) {
			LOGE("Desolate Public WiFi port: deisolate customer port failed");
			return false;
		}
	}
	return true;
}

static bool pwm_reisolate_port(struct pwm_port *port)
{
	bool errcode;

	errcode = pwm_deisolate_port(port);
	if (!errcode) {
		LOGE("Reisolate Public WiFi port: deisolate port failed");
		return false;
	}

	errcode = pwm_isolate_port(port);
	if (!errcode) {
		LOGE("Reisolate Public WiFi port: Isolate port failed");
		return false;
	}
	return true;
}

static bool pwm_init_port(struct pwm_port *port, const char *name,
		enum pwm_port_type type, int tag)
{
	bool errcode;

	ds_dlist_insert_tail(&pwm_bridge.ports, port);
	strncpy(port->name, name, sizeof(port->name) - 1);
	port->name[sizeof(port->name) - 1] = '\0';
	port->ofid = -1;
	port->isolated = false;
	port->added = false;
	port->type = type;
	port->tag = tag;

	errcode = pwm_add_ovs_port(port);
	if (!errcode) {
		LOGE("Initialize Public WiFi bridge port: add OVS port failed");
		return false;
	}

	errcode = pwm_update_port_ofid(port);
	if (!errcode) {
		LOGE("Initialize Public WiFi bridge port: update openFlow identifier failed");
		return false;
	}

	errcode = pwm_isolate_port(port);
	if (!errcode) {
		LOGE("Initialize Public WiFi bridge port: isolate failed");
		return false;
	}
	return true;
}

static bool pwm_deinit_port(struct pwm_port *port)
{
	bool errcode;

	errcode = pwm_deisolate_port(port);
	if (!errcode) {
		LOGE("Deinitialize Public WiFi bridge port: deisolate failed");
		return false;
	}

	errcode = pwm_delete_ovs_port(port);
	if (!errcode) {
		LOGE("Deinitialize Public WiFi bridge port: delete OVS port failed");
		return false;
	}

	ds_dlist_remove(&pwm_bridge.ports, port);
	return true;
}

bool pwm_delete_port(struct pwm_port *port)
{
	bool errcode;

	errcode = pwm_deinit_port(port);
	if (!errcode) {
		LOGE("Delete Public WiFi bridge port: deinitialize port failed");
		return false;
	}

	free(port);
	return true;
}

bool pwm_create_port(const char *name, enum pwm_port_type type, int tag)
{
	bool errcode;
	struct pwm_port *port;

	port = (struct pwm_port *) malloc(sizeof(*port));
	if (!port) {
		LOGE("Create Public WiFi bridge port: memeory allocation failed");
		return false;
	}

	errcode = pwm_init_port(port, name, type, tag);
	if (!errcode) {
		LOGE("Create Public WiFi bridge port: initialize port failed");
		pwm_delete_port(port);
		return false;
	}
	return true;
}

bool pwm_update_port_tag(struct pwm_port *port, int tag)
{
	bool errcode;

	if (port->tag == tag) {
		return true;
	}
	port->tag = tag;

	if (port->type == PWM_PORT_TYPE_CUSTOMER) {
		return true;
	}

	errcode = pwm_reisolate_port(port);
	if (!errcode) {
		LOGE("Update Public WiFi port tag: reisolate port failed");
		return false;
	}
	return true;
}

