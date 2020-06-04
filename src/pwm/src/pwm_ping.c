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

#include <pwm_ping.h>
#include <pwm_ovsdb.h>

static int ping_fail_count;
static ev_timer	 keep_alive_timer;
pint_data_t ping_addr;
static bool timer_already_up=false;

static int get_ping_fail_count()
{
	return ping_fail_count;
}

static void set_ping_fail_count(int val)
{
	ping_fail_count = val;
}

static void check_ip_alive()
{
	int err=0 , ct=0;
	char cmd[156];

	if(AF_INET6 == ping_addr.inet)
	{
		snprintf(cmd, sizeof(cmd) - 1, "ping6 -c 1 -W 1 %s",ping_addr.ip);
	}
	else if(AF_INET == ping_addr.inet)
	{
		snprintf(cmd, sizeof(cmd) - 1, "ping -c 1 -W 1 %s",ping_addr.ip);
	}else
	{
		LOGE("Invalid inet family");
		return;
	}
	cmd[sizeof(cmd) - 1] = '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Ping to Public WiFi end-point %s failed", ping_addr.ip);
		ct = get_ping_fail_count();
		set_ping_fail_count(ct+1);
	}
	else
		set_ping_fail_count(0);

	return;
}

static void keep_alive_timer_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
	if(!is_SNM_H_enabled())
	{
		ev_timer_stop(EV_DEFAULT,&keep_alive_timer);
		return;
	}
	check_ip_alive();

	if(get_ping_fail_count() >= PING_MAX_FAIL_COUNT)
	{
		//Ping failed 3 times
		LOGW("Tunnel connection broken");
		pwm_reset_tunnel();
		ev_timer_stop(EV_DEFAULT,&keep_alive_timer);
		return;
	}

	ev_timer_init(&keep_alive_timer, keep_alive_timer_cb, KEEP_ALIVE_CHECK_TIME, 0);
	ev_timer_start(EV_DEFAULT, &keep_alive_timer);
}

bool init_keep_alive( int inet,const char *dip)
{
	if(!dip)
	{
		LOGE("[%s] Invalid argument",__func__);
		return false;
	}

	ping_addr.inet = inet;
	strncpy(ping_addr.ip,dip,sizeof(ping_addr.ip) -1);
	ping_addr.ip[sizeof(ping_addr.ip)-1] = '\0';

	check_ip_alive();
	if(!timer_already_up)
	{
		ev_timer_init(&keep_alive_timer, keep_alive_timer_cb, KEEP_ALIVE_CHECK_TIME, 0);
		ev_timer_start(EV_DEFAULT, &keep_alive_timer);
		timer_already_up=true;
	}
	return true;

}

bool stop_keep_alive(void)
{
	memset(&ping_addr,0,sizeof(ping_addr));
	ev_timer_stop(EV_DEFAULT,&keep_alive_timer);
	timer_already_up=false;
	return true;
}

