#!/bin/sh
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
