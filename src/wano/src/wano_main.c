/*
* Copyright (c) 2019, Sagemcom.
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

#include "log.h"
#include "wano_nat.h"
#include "wano_ovsdb.h"
#include "os.h"
#include "target.h"
#include "os_backtrace.h"
#include "json_util.h"
#include "evsched.h"
#include "ovsdb.h"
#include "wano_dhcp_option.h"
#include "wano_ntp.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

static log_severity_t wano_log_severity = LOG_SEVERITY_INFO;

static void wano_init(void)
{
	wano_nat_init();
	wano_ovsdb_init();
	wano_dhcp_option_init();
	wano_time_table_init();
}

int main(int argc, char **argv)
{
	struct ev_loop *loop = EV_DEFAULT;
	if (os_get_opt(argc, argv, &wano_log_severity)) {
		LOGE("Initializing WAN orchestrator: failed to get options");
		return -1;
	}

	target_log_open("WANO", 0);
	LOGN("Starting WAN orchestrator - WANO");
	log_severity_set(wano_log_severity);
	log_register_dynamic_severity(loop);

	backtrace_init();
	json_memdbg_init(loop);
	if (evsched_init(loop) == false) {
		LOGE("Initializing WAN orchestrator: failed to initialize event loop");
		return -1;
	}

	if (!target_init(TARGET_INIT_MGR_WANO, loop)) {
		LOGE("Initializing WAN orchestrator: failed to initialize target");
		return -1;
	}
	if (!ovsdb_init_loop(loop, "WANO")) {
		LOGE("Initializing WAN orchestrator: failed to initialize OVS database");
		return -1;
	}

	wano_init();

	ev_run(loop, 0);
	target_close(TARGET_INIT_MGR_WANO, loop);

	if (!ovsdb_stop_loop(loop)) {
		LOGE("Stopping WAN orchestrator: failed to stop OVS database");
	}
	ev_default_destroy();
	LOGN("Exiting WAN orchestrator - WANO");
	return 0;
}

