#!/bin/sh

# Copyright (c) 2015, Plume Design Inc. All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    1. Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#    2. Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#    3. Neither the name of the Plume Design Inc. nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# {# jinja-parse #}
INSTALL_PREFIX={{INSTALL_PREFIX}}

. /lib/opensync_functions.sh

Healthcheck_Fatal()
{
    no_fatal=""
    if [ -f /opt/tb/cm-disable-fatal ]; then
        no_fatal="/opt/tb/cm-disable-fatal"
    fi

    # safeupdate is running -- we should prevent reboots
    [ -z "$no_fatal" ] && {
        pgrep safeupdate > /dev/null && no_fatal="safeupdate"
    }

    # Firmware is downloading or being flashed, do not reboot
    [ -z "$no_fatal" ] && {
        UST=$(ovsdb_upgrade_status)
        [ ${UST} -gt 0 ] && no_fatal="upgrade"
    }

    {% raw -%}
    if [ ${#no_fatal} -gt 0 ]; then
        log_emerg "### Healthcheck Fatal: Not rebooting due to $no_fatal"
        return
    fi
    {%- endraw %}

    log_emerg "### Healthcheck Fatal: Rebooting pod ###"

    ${INSTALL_PREFIX}/scripts/reset_feature_flags.sh # Clear persistent feature flags

    sleep 5 # Give logpull a chance to collect logs
    {% if CONFIG_OSP_REBOOT_CLI_OVERRIDE == 'y' -%}
    fatal_reason="Healthcheck failed."
    [ ! -z $1 ] && fatal_reason="$fatal_reason Last failed script $1 $2"
    /sbin/reboot -Rtype=healthcheck -Rreason="$fatal_reason"
    {%- else -%}
    /sbin/reboot
    {%- endif %}
}
