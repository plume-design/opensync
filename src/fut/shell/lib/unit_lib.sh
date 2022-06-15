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


# Include basic environment config
export FUT_UNIT_LIB_SRC=true
[ "${FUT_BASE_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/base_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/unit_lib.sh sourced"

####################### INFORMATION SECTION - START ###########################
#
#   Common library of shared functions, used globally
#
####################### INFORMATION SECTION - STOP ############################

####################### UTILITY SECTION - START ###############################

###############################################################################
# DESCRIPTION:
#   Function returns path to the script manipulating OpenSync managers.
#   Function assumes one of the two manager scripts exist on system;
#       - opensync
#       - manager
#   Raises exception if manager script does not exist.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   Echoes path to manager script.
# USAGE EXAMPLE(S):
#   get_managers_script
###############################################################################
get_managers_script()
{
    if [ -e /etc/init.d/opensync ]; then
        echo "/etc/init.d/opensync"
    elif [ -e /etc/init.d/manager ]; then
        echo "/etc/init.d/manager"
    else
        raise "FAIL: Missing the script to start OS managers" -l "unit_lib:get_managers_script" -ds
    fi
}

###############################################################################
# DESCRIPTION:
#   Function returns filename of the script manipulating openvswitch.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   Echoes path to openvswitch script.
# USAGE EXAMPLE(S):
#   get_openvswitch_script
###############################################################################
get_openvswitch_script()
{
    echo "/etc/init.d/openvswitch"
}

###############################################################################
# DESCRIPTION:
#   Function echoes processes print-out in wide format.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   Echoes list of processes.
# USAGE EXAMPLE(S):
#   get_process_cmd
###############################################################################
get_process_cmd()
{
    echo "ps -w"
}

###############################################################################
# DESCRIPTION:
#   Function checks if ovsdb-server is running, if not, it will put device into initial state
#    - Check is done with checking of PID of ovsdb-server
#    - If PID is not found, function runs device_init and restart_managers
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   If ovsdb-server is running
#   1   If ovsdb-server was not running and managers were restarted - OVSDBServerCrashed exception is raised
#   1   If function failed to retrieve PID of ovsdb-server
# RAISES:
# USAGE EXAMPLE(S):
#   check_restore_ovsdb_server
###############################################################################
check_restore_ovsdb_server()
{
    log -deb "unit_lib:check_restore_ovsdb_server - Checking if ovsdb-server is running"
    res=$($(get_process_cmd) | grep "ovsdb-server" | grep -v "grep" | awk '{ print $1 }')
    if [ "$?" != 0 ]; then
        log -err "unit_lib:check_restore_ovsdb_server - Acquire of PID for ovsdb-server failed to execute"
        return 1
    fi
    if [ -z "${res}" ]; then
        # Re-init device before raising exception
        device_init &&
            log -deb "unit_lib:check_restore_ovsdb_server - Device initialized - Success" ||
            log -err "unit_lib:check_restore_ovsdb_server - device_init - Could not initialize device"

        start_openswitch &&
            log -deb "unit_lib:check_restore_ovsdb_server - OpenvSwitch started - Success" ||
            log -err "unit_lib:check_restore_ovsdb_server - start_openswitch - Could not start OpenvSwitch"
        restart_managers
        log -deb "unit_lib:check_restore_ovsdb_server - Executed restart_managers, exit code: $?"
        raise "CRITICAL: ovsdb-server crashed" -l "unit_lib:check_restore_ovsdb_server" -osc
    else
        log -dev "unit_lib:check_restore_ovsdb_server - ovsdb-server is running"
    fi
}

###############################################################################
# DESCRIPTION:
#   Function echoes single quoted input argument. Used for ovsh tool.
#   It is imperative that this function does not log or echo anything, as its main
#   functionality is to echo and the value being used upstream.
# INPUT PARAMETER(S):
#   arg: string containing double quotes, command or other special characters (string, required)
# PRINTS:
#   Single quoted input argument.
# RETURNS:
#   returns exit code of printf operation: 0 for success, >0 for failure
# USAGE EXAMPLE(S):
#   single_quote_arg "[map,[[encryption,WPA-PSK],[key,FutTestPSK],[mode,2]]]"
###############################################################################
single_quote_arg()
{
    printf %s\\n "$1" | sed "s/'/'\\\\''/g;1s/^/'/;\$s/\$/'/" ;
}

###############################################################################
# DESCRIPTION:
#   Function gets MAC address of a provided interface.
#   Function supports ':' delimiter only.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
# RETURNS:
#   HW address of an interface.
# USAGE EXAMPLE(S):
#   get_radio_mac_from_system eth0
###############################################################################
get_radio_mac_from_system()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:get_radio_mac_from_system requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1

    # Match 2 alfanum chars and a : five times plus additional 2 alfanum chars
    ifconfig "$if_name" | grep -o -E '([A-F0-9]{2}:){5}[A-F0-9]{2}'
}

###############################################################################
# DESCRIPTION:
#   Function returns MAC of radio interface from Wifi_Radio_State table.
#   Using condition string interface can be selected by name, channel,
#   frequency band etc. See USAGE EXAMPLE(S).
# INPUT PARAMETER(S):
#   $1 condition string (string, required)
# RETURNS:
#   Radio interface MAC address.
# USAGE EXAMPLES(S):
#   get_radio_mac_from_ovsdb "freq_band==5GL"
#   get_radio_mac_from_ovsdb "if_name==wifi1"
#   get_radio_mac_from_ovsdb "channel==44"
###############################################################################
get_radio_mac_from_ovsdb()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:get_radio_mac_from_ovsdb requires ${NARGS} input argument(s), $# given" -arg
    local where_clause=$1

    # No logging, this function echoes the requested value to caller!
    ${OVSH} s Wifi_Radio_State -w ${where_clause} mac -r
    return $?
}

###############################################################################
# DESCRIPTION:
#   Function returns MAC of vif interface from Wifi_VIF_State table.
#   Using condition string interface can be selected by name, channel, etc.
# INPUT PARAMETER(S):
#   $1 condition string (string, required)
# RETURNS:
#   VIF interface MAC address.
# USAGE EXAMPLES(S):
#   get_vif_mac_from_ovsdb "if_name==bhaul-sta-24"
#   get_vif_mac_from_ovsdb "channel==6"
###############################################################################
get_vif_mac_from_ovsdb()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:get_vif_mac_from_ovsdb requires ${NARGS} input argument(s), $# given" -arg
    local where_clause=$1

    # No logging, this function echoes the requested value to caller!
    ${OVSH} s Wifi_VIF_State -w ${where_clause} mac -r
    return $?
}

###############################################################################
# DESCRIPTION:
#   Function echoes interface name used by CM for WAN uplink.
#   No checks are made for number of echoed elements in case none, one or
#   multiple interfaces are used.
# INPUT PARAMETER(S):
#   None
# RETURNS:
#   Used WAN interface
# USAGE EXAMPLES(S):
#   var=$(get_wan_uplink_interface_name)
###############################################################################
get_wan_uplink_interface_name()
{
    # No logging, this function echoes the requested value to caller!
    ${OVSH} s Connection_Manager_Uplink -w is_used==true if_name -r
    return $?
}

###############################################################################
# DESCRIPTION:
#   Function starts DHCP client on a provided interface.
#   If client aleady runs, kills old one and starts new DHCP client process.
#   Function uses udhcpc service, not universal accross devices.
#   If parameter should_get_address is true function waits for an IP to be
#   provided. Parameter defaults to false.
#   If IP is not provided within timeout, function raises an exception.
# INPUT PARAMETER(S):
#   $1 interface name (string, required)
#   $2 option to select if IP address is requested (bool, optional)
# RETURNS:
#   0   On success
#   See DESCRIPTION.
# USAGE EXAMPLES(S):
#   start_udhcpc eth0 true
#   start_udhcpc eth0 false
###############################################################################
start_udhcpc()
{
    NARGS_MIN=1
    NARGS_MAX=2
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "unit_lib:start_udhcpc requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    if_name=$1
    should_get_address=${2:-false}

    log -deb "unit_lib:start_udhcpc - Starting udhcpc on $if_name"

    ps_out=$(pgrep "/sbin/udhcpc.*$if_name")
    if [ $? -eq 0 ]; then
        # shellcheck disable=2086
        kill $ps_out && log -deb "unit_lib:start_udhcpc - Old udhcpc pid killed for $if_name"
    fi

    /sbin/udhcpc -i "$if_name" -f -p /var/run/udhcpc-"$if_name".pid -s ${OPENSYNC_ROOTDIR}/bin/udhcpc.sh -t 60 -T 1 -S --no-default-options &>/dev/null &

    if [ "$should_get_address" = "true" ]; then
        wait_for_function_response 'notempty' "check_interface_ip_address_set_on_system $if_name" &&
            log -deb "unit_lib:start_udhcpc - DHCPC provided address to '$if_name' - Success" ||
            raise "FAIL: DHCPC did not provide address to '$if_name'" -l "unit_lib:start_udhcpc" -ds
    fi

    return 0
}

####################### UTILITY SECTION - STOP ################################


####################### PROCESS SECTION - START ###############################

###############################################################################
# DESCRIPTION:
#   Function echoes PID of provided process.
# INPUT PARAMETER(S):
#   $1 process name (string, required)
# ECHOES:
#   PID value.
# USAGE EXAMPLE(S):
#   get_pid "healthcheck"
###############################################################################
get_pid()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:get_pid requires ${NARGS} input argument(s), $# given" -arg
    process_name=$1

    # Match parameter string, but exclude lines containing 'grep'.
    PID=$($(get_process_cmd) | grep -e "$process_name" | grep -v 'grep' | awk '{ print $1 }')
    echo "$PID"
}

###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
#   $1  process status type (string, required)
#   $2  process file (string, required)
# RETURNS:
#   0   process file does not exist and process should be dead
#   0   process file exists and process is alive
#   1   process file does not exist and process should be alive
#   1   process file exists and process should be dead
# USAGE EXAMPLE(S):
#   check_pid_file alive \"/var/run/udhcpc-$if_name.pid\"
#   check_pid_file dead \"/var/run/udhcpc-$if_name.pid\"
###############################################################################
check_pid_file()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:check_pid_file requires ${NARGS} input argument(s), $# given" -arg
    type=$1
    file=$2

    if [ "$type" = "dead" ] && [ ! -f "$file" ]; then
        log -deb "unit_lib:check_pid_file - Process '$file' is dead"
        return 0
    elif [ "$type" = "alive" ] && [ -f "$file" ]; then
        log -deb "unit_lib:check_pid_file - Process '$file' is alive"
        return 0
    elif [ "$type" = "dead" ] && [ -f "$file" ]; then
        log -deb "unit_lib:check_pid_file - Process is alive"
        return 1
    else
        log -deb "unit_lib:check_pid_file - Process is dead"
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function checks if udhcps service (DHCP client) on provided interface is
#   running. It does so by checking existence of PID of DHCP client service.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
# RETURNS:
#   0   PID found, udhcpc service is running
#   1   PID not found, udhcpc service is not running
# USAGE EXAMPLE(S):
#   check_pid_udhcp eth0
###############################################################################
check_pid_udhcp()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:check_pid_udhcp requires ${NARGS} input argument(s), $# given" -arg
    local if_name="${1}"

    PID=$($(get_process_cmd) | grep -e udhcpc | grep -e "${if_name}" | grep -v 'grep' | awk '{ print $1 }')
    if [ -z "$PID" ]; then
        log -deb "unit_lib:check_pid_udhcp - DHCP client not running on '${if_name}'"
        return 1
    else
        log -deb "unit_lib:check_pid_udhcp - DHCP client running on '${if_name}', PID=${PID}"
        return 0
    fi
}

###############################################################################
# DESCRIPTION:
#   Function kills process of given name.
#   Requires binary/tool pidof installed on system.
# INPUT PARAMETER(S):
#   $1 process name (string, required)
# RETURNS:
#   None.
# USAGE EXAMPLE(S):
#   killall_process_by_name <process name>
###############################################################################
killall_process_by_name()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:killall_process_by_name requires ${NARGS} input argument(s), $# given" -arg
    process_name=$1
    local PROCESS_PID

    PROCESS_PID="$(pidof "${process_name}")"
    if [ -n "$PROCESS_PID" ]; then
        # In case of several returned values
        for P in $PROCESS_PID; do
            for S in SIGTERM SIGINT SIGHUP SIGKILL; do
                kill -s "${S}" "${P}"
                kill -0 "${P}"
                if [ $? -ne 0 ]; then
                    break
                fi
            done
            if [ $? -eq 0 ]; then
                log -deb "unit_lib:killall_process_by_name - killed process: ${P} with signal: ${S}"
            else
                log -deb "unit_lib:killall_process_by_name - killall_process_by_name - could not kill process: ${P}"
            fi
        done
    fi
}

####################### PROCESS SECTION - STOP ################################


####################### SETUP SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function initializes device for use in FUT.
#   It stops healthcheck service to prevent the device from rebooting.
#   It calls a function that instructs CM to prevent the device from rebooting.
#   It stops all managers.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   Last exit status.
# USAGE EXAMPLE(S):
#   device_init
###############################################################################
device_init()
{
    stop_managers &&
        log -deb "unit_lib:device_init - Managers stopped - Success" ||
        raise "FAIL: Could not stop managers" -l "unit_lib:device_init" -ds

    stop_healthcheck &&
        log -deb "unit_lib:device_init - Healthcheck stopped - Success" ||
        raise "FAIL: Could not stop healthcheck" -l "unit_lib:device_init" -ds

    disable_fatal_state_cm &&
        log -deb "unit_lib:device_init - CM fatal state disabled - Success" ||
        raise "FAIL: Could not disable CM fatal state" -l "unit_lib:device_init" -ds

    return $?
}

###############################################################################
# DESCRIPTION:
#   Function stops healthcheck process and disables it.
#   Checks if healthcheck already stopped, does nothing if already stopped.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   1   healthcheck process is not stopped.
#   0   healthcheck process is stopped.
# USAGE EXAMPLE(S):
#   stop_healthcheck
###############################################################################
stop_healthcheck()
{
    if [ -n "$(get_pid "healthcheck")" ]; then
        log -deb "unit_lib:stop_healthcheck - Disabling healthcheck."
        /etc/init.d/healthcheck stop || true

        log -deb "unit_lib:stop_healthcheck - Check if healthcheck is disabled"
        wait_for_function_response 1 "$(get_process_cmd) | grep -e 'healthcheck' | grep -v 'grep'"
        if [ "$?" -ne 0 ]; then
            log -deb "unit_lib:stop_healthcheck - Healthcheck is NOT disabled! PID: $(get_pid "healthcheck")"
            return 1
        else
            log -deb "unit_lib:stop_healthcheck - Healthcheck is disabled."
        fi
    else
        log -deb "unit_lib:stop_healthcheck - Healthcheck is already disabled."
    fi

    return 0
}

####################### SETUP SECTION - STOP ##################################

####################### OpenSwitch SECTION - START ############################

###############################################################################
# DESCRIPTION:
#   Function starts openvswitch and ovsdb-server.
#   First it checks if openvswitch is alredy running. If running return 0.
#   Otherwise starts openvswitch and checks if started and
#   raises exception if it did not start.
#   Checks if pidof of openvswitch is running by getting its PID.
#   Checks if pidof of ovsdb-server is running by getting its PID.
#   Raises exception if either openvswitch or ovsdb-server is not started.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   If openvswitch initially started.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   start_openswitch
###############################################################################
start_openswitch()
{
    log -deb "unit_lib:start_openswitch - Starting Open vSwitch"

    # shellcheck disable=SC2034,2091
    ovs_run=$($(get_process_cmd)  | grep -v "grep" | grep "ovs-vswitchd")
    if [ "$?" -eq 0 ]; then
        log -deb "unit_lib:start_openswitch - Open vSwitch already running"
        return 0
    fi

    OPENVSWITCH_SCRIPT=$(get_openvswitch_script)
    ${OPENVSWITCH_SCRIPT} start ||
        raise "FAIL: Issue during Open vSwitch start" -l "unit_lib:start_openswitch" -ds

    wait_for_function_response 0 "pidof ovs-vswitchd" &&
        log -deb "unit_lib:start_openswitch - ovs-vswitchd running - Success" ||
        raise "FAIL: Could not start ovs-vswitchd" -l "unit_lib:start_openswitch" -ds

    wait_for_function_response 0 "pidof ovsdb-server" &&
        log -deb "unit_lib:start_openswitch - ovsdb-server running - Success" ||
        raise "FAIL: Could not start ovsdb-server" -l "unit_lib:start_openswitch" -ds

    sleep 1

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function stops openvswitch.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   If openvswitch initially started.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   stop_openswitch
###############################################################################
stop_openswitch()
{
    log "unit_lib:stop_openswitch - Stopping Open vSwitch"

    OPENVSWITCH_SCRIPT=$(get_openvswitch_script)
    ${OPENVSWITCH_SCRIPT} stop &&
        log -deb "unit_lib:stop_openswitch - Open vSwitch stopped - Success" ||
        raise "FAIL: Issue during Open vSwitch stop" -l "unit_lib:stop_openswitch" -ds

    return 0
}

####################### OpenSwitch SECTION - STOP #############################


####################### RESOURCE SECTION - START ##############################

####################### RESOURCE SECTION - STOP ###############################


####################### MANAGERS SECTION - START ##############################

###############################################################################
# DESCRIPTION:
#   Function starts all OpenSync managers.
#   Executes managers script with start option.
#   Raises an exception if managers script is not successfully executed or if
#   DM (Diagnostic Manager) slave or master is not started.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   start_managers
###############################################################################
start_managers()
{
    log -deb "unit_lib:start_managers Starting OpenSync managers"

    MANAGER_SCRIPT=$(get_managers_script)
    ret=$($MANAGER_SCRIPT start)
    # Make sure to define return value on success or failure
    if [ $? -ne 1 ]; then
        raise "FAIL: Issue during OpenSync manager start" -l "unit_lib:start_managers" -ds
    else
        log -deb "unit_lib:start_managers - OpenSync managers started - Success"
    fi

    # Check dm slave PID
    # shellcheck disable=2091
    PID=$($(get_process_cmd) | grep -e "${OPENSYNC_ROOTDIR}/bin/dm" | grep -v 'grep' | grep -v slave | awk '{ print $1 }')
    if [ -z "$PID" ]; then
        raise "FAIL: Issue during manager start, dm slave not running" -l "unit_lib:start_managers" -ds
    else
        log -deb "unit_lib:start_managers - dm slave PID = $PID"
    fi

    # Check dm master PID
    # shellcheck disable=2091
    PID=$($(get_process_cmd) | grep -e "${OPENSYNC_ROOTDIR}/bin/dm" | grep -v 'grep' | grep -v master | awk '{ print $1 }')
    if [ -z "$PID" ]; then
        raise "FAIL: Issue during manager start, dm master not running" -l "unit_lib:start_managers" -ds
    else
        log -deb "unit_lib:start_managers - dm master PID = $PID"
    fi

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function re-starts all OpenSync managers.
#   Executes managers script with restart option.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   Last exit status.
# USAGE EXAMPLE(S):
#   restart_managers
###############################################################################
restart_managers()
{
    log -deb "unit_lib:restart_managers - Restarting OpenSync managers"
    MANAGER_SCRIPT=$(get_managers_script)
    # shellcheck disable=2034
    ret=$($MANAGER_SCRIPT restart)
    ec=$?
    log -deb "unit_lib:restart_managers - manager restart exit code ${ec}"
    return $ec
}

###############################################################################
# DESCRIPTION:
#   Function stops all OpenSync managers. Executes managers script with
#   stop option. Raises an execption if managers cannot be stopped.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   stop_managers
###############################################################################
stop_managers()
{
    log -deb "unit_lib:stop_managers - Stopping OpenSync managers"
    MANAGER_SCRIPT=$(get_managers_script)
    $MANAGER_SCRIPT stop &&
        log -deb "unit_lib:stop_managers - OpenSync manager stopped - Success" ||
        raise "FAIL: Issue during OpenSync manager stop" -l "unit_lib:stop_managers" -ds

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function starts a single specific OpenSync manager.
#   Raises an exception if managers script is not executable or if
#   it does not exist.
# INPUT PARAMETER(S):
#   $1  manager name (string, required)
#   $2  option used with start manager (string, optional)
# RETURNS:
#   0   On success.
#   1   Manager is not executable.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   start_specific_manager cm -v
#   start_specific_manager cm -d
###############################################################################
start_specific_manager()
{
    NARGS_MIN=1
    NARGS_MAX=2
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "unit_lib:start_specific_manager requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    manager="${OPENSYNC_ROOTDIR}/bin/$1"
    option=$2

    # Check if executable
    if [ ! -x "$manager" ]; then
        log -deb "unit_lib:start_specific_manager - Manager $manager does not exist or is not executable"
        return 1
    fi

    # Start manager
    # shellcheck disable=SC2018,SC2019
    log -deb "unit_lib:start_specific_manager - Starting $manager $option" | tr a-z A-Z

    if [ "$1" == "wm" ]; then
        ps_out=$(pgrep $manager)
        if [ $? -eq 0 ]; then
            kill -9 $ps_out && log -deb "unit_lib:start_specific_manager - Old pid killed for $manager"
        fi
        sleep 10
    fi

    $manager "$option" >/dev/null 2>&1 &
    sleep 1
}

###############################################################################
# DESCRIPTION:
#   Function sets log severity for manager by managing AW_Debug table.
#   Possible log severity levels are INFO, TRACE, DEBUG..
#   Raises an exception if log severity is not succesfully set.
# INPUT PARAMETER(S):
#   $1 manager name (string, required)
#   $2 log severity (string, required)
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   set_manager_log NM TRACE
#   set_manager_log WM TRACE
###############################################################################
set_manager_log()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:set_manager_log requires ${NARGS} input argument(s), $# given" -arg
    name=$1
    log_severity=$2

    log -deb "unit_lib:set_manager_log - Checking if AW_Debug contains ${name}"
    check_ovsdb_entry AW_Debug -w name "${name}"
    if [ "$?" == 0 ];then
        log -deb "unit_lib:set_manager_log - AW_Debug contains ${name}, will update"
        update_ovsdb_entry AW_Debug -w name "${name}" -u log_severity "${log_severity}" &&
            log -deb "unit_lib:set_manager_log - AW_Debug ${name} updated to ${log_severity}" ||
            raise "FAIL: Could not update AW_Debug ${name} to ${log_severity}" -l "unit_lib:set_manager_log" -oe
    else
        log -deb "unit_lib:set_manager_log - Adding ${name} to AW_Debug with severity ${log_severity}"
        insert_ovsdb_entry AW_Debug -i name "${name}" -i log_severity "${log_severity}" ||
            raise "FAIL: Could not insert to table AW_Debug::log_severity" -l "unit_lib:set_manager_log" -oe
    fi
    log -deb "unit_lib:set_manager_log - Dumping table AW_Debug"
    print_tables AW_Debug || true
}

###############################################################################
# DESCRIPTION:
#   Function prevents CM fatal state thus prevents restarting managers or
#   rebooting device. In normal operation CM would be constantly performing
#   connectivity tests to the Cloud and without connection it would perform
#   device reboot. In FUT environment device has no connection to the Cloud,
#   so restarts and reboots must be prevented.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   Last exit code of file creation.
# USAGE EXAMPLE(S):
#   disable_fatal_state_cm
###############################################################################
disable_fatal_state_cm()
{
    log -deb "unit_lib:disable_fatal_state_cm - Disabling CM manager restart procedure"
    if [ ! -d /opt/tb ]; then
        mkdir -p /opt/tb/
    fi
    # Create cm-disable-fatal file in /opt/tb/
    touch /opt/tb/cm-disable-fatal
    if [ $? != 0 ]; then
        log -deb "unit_lib:disable_fatal_state_cm - /opt/tb is not writable, mount a tmpfs over it"
        mount -t tmpfs tmpfs /opt/tb
        touch /opt/tb/cm-disable-fatal
    fi
}

###############################################################################
# DESCRIPTION:
#   Function enables CM fatal state thus enables restarting managers
#   and device reboot.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   Last exit code of file removal.
# USAGE EXAMPLE(S):
#   enable_fatal_state_cm
###############################################################################
enable_fatal_state_cm()
{
    log -deb "unit_lib:enable_fatal_state_cm - Enabling CM manager restart procedure"
    # Delete cm-disable-fatal file in /opt/tb/
    rm -f /opt/tb/cm-disable-fatal
}

###############################################################################
# DESCRIPTION:
#   Function checks if manager is alive by checking its PID.
# INPUT PARAMETER(S):
#   $1  manager bin file (string, required)
# RETURNS:
#   0   Manager is alive.
#   1   Manager is not alive.
# USAGE EXAMPLE(S):
#   check_manager_alive <manager_bin_file>
###############################################################################
check_manager_alive()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:check_manager_alive requires ${NARGS} input argument(s), $# given" -arg
    manager_bin_file=$1

    pid_of_manager=$(get_pid "$manager_bin_file")
    if [ -z "$pid_of_manager" ]; then
        log -deb "unit_lib:check_manager_alive - $manager_bin_file PID not found"
        return 1
    else
        log -deb "unit_lib:check_manager_alive - $manager_bin_file PID found"
        return 0
    fi
}

####################### MANAGERS SECTION - STOP ###############################


####################### OVSDB SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function echoes field value from ovsdb table.
#   It can be used with supported option(s):
#   -w (where)  field value used as a condition to select ovsdb table column
#
#   If -w option is used then two additional parameters must follow to
#   define condition string. Several -w options are possible, but for any
#   additional -w option used, there must always be 2 additional parameters.
#   In short, optional parameters come in groups of 3.
#
#   -r (raw)    output value is echoed without formatting
#
# INPUT PARAMETER(S):
#   $1  ovsdb table (string, required)
#   $2  ovsdb field in ovsdb table (string, required)
#   $3  option, supported options: -w, -raw (string, optional, see DESCRIPTION)
#   $4  ovsdb field in ovsdb table (string, optional, see DESCRIPTION)
#   $5  ovsdb field value (string, optional, see DESCRIPTION)
#   ...
# ECHOES:
#   Echoes field value.
# USAGE EXAMPLE(S):
#   get_ovsdb_entry_value AWLAN_Node firmware_version
#   get_ovsdb_entry_value Manager target -r
###############################################################################
get_ovsdb_entry_value()
{
    ovsdb_table=$1
    ovsdb_field=$2
    shift 2
    conditions_string=""
    raw="false"
    json_value="false"

    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -w)
                conditions_string="$conditions_string -w $1==$2"
                shift 2
                ;;
            -r)
                raw="true"
                shift
                ;;
            -json_value)
                json_value=$1
                shift
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "unit_lib:get_ovsdb_entry_value" -arg
                ;;
        esac
    done

    # shellcheck disable=SC2086
    if [ "$json_value" == "false" ]; then
        raw_field_value=$(${OVSH} s "$ovsdb_table" $conditions_string "$ovsdb_field" -r) ||
            return 1
    else
        raw_field_value=$(${OVSH} s "$ovsdb_table" $conditions_string "$ovsdb_field" -j) ||
            return 1
    fi

    echo "$raw_field_value" | grep -q '"uuid"'
    uuid_check_res="$?"
    if [ "$json_value" == "false" ] && [ "$raw" == "false" ] && [ "$uuid_check_res" == "0" ]; then
        value=$(echo "$raw_field_value" | cut -d ',' -f 2 | cut -d '"' -f 2)
    elif [ "$json_value" != "false" ]; then
        value=$(echo "$raw_field_value" | sed -n "/${json_value}/{n;p;}")
        if [ ${?} != 0 ]; then
            value=$(echo "$raw_field_value" | awk "/${json_value}/{getline; print}")
        fi
        # Remove leading whitespaces from json output
        value=$(echo "${value}" | sed 's/^ *//g')
    else
        value="$raw_field_value"
    fi

    echo -n "$value"
}

###############################################################################
# DESCRIPTION:
#   Function checks if the actual value of the requested field in the
#   specified table is equal to the expected one.
#   It can be used with supported option(s):
#   -w (where)  field value used as a condition to select ovsdb table column
#
#   If -w option is used then two additional parameters must follow to
#   define condition string. Several -w options are possible, but for any
#   additional -w option used, there must always be 2 additional parameters.
#   In short, optional parameters come in groups of 3.
#
# INPUT PARAMETER(S):
#   $1  ovsdb table (string, required)
#   $2  option, supported options: -w (string, optional, see DESCRIPTION)
#   $3  ovsdb field in ovsdb table (string, optional, see DESCRIPTION)
#   $4  ovsdb field value (string, optional, see DESCRIPTION)
#   ...
# RETURNS:
#   0   Value is as expected.
#   1   Value is not as expected.
# USAGE EXAMPLE(S):
#   check_ovsdb_entry AWLAN_Node -w model <model>
###############################################################################
check_ovsdb_entry()
{
    ovsdb_table=$1
    shift 1
    conditions_string=""
    transact_string='["Open_vSwitch",{"op": "select","table": "'$ovsdb_table'","where":['
    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -w)
                echo ${2} | grep -e "[ \"]" -e '\\' &&
                    conditions_string="$conditions_string -w $1==$(single_quote_arg "${2}")" ||
                    conditions_string="$conditions_string -w $1==$2"
                    val_str="$2"
                    echo "$2" | grep -q "map"
                    if [ "$?" != "0" ]; then
                        echo "$2" | grep -q "false\|true"
                        if [ "$?" != "0" ]; then
                            echo "$2" | grep -q "\""
                            if [ "$?" != "0" ]; then
                                [ "$2" -eq "$2" ] 2>/dev/null && is_number="0" || is_number="1"
                                if [ "${is_number}" == "1" ]; then
                                    val_str='"'$2'"'
                                fi
                            fi
                        fi
                    fi
                    transact_string=$transact_string'["'$1'","==",'$val_str'],'
                shift 2
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "unit_lib:check_ovsdb_entry" -arg
                ;;
        esac
    done
    transact_string="$transact_string]}]"
    # remove last , from where statement ],]}]
    transact_string=${transact_string//],]/]]}
    check_cmd="${OVSH} s $ovsdb_table $conditions_string"
    log -deb "unit_lib:check_ovsdb_entry - Checking if entry exists:\n\t$check_cmd"
    eval "$check_cmd"
    if [ "$?" == 0 ]; then
        log -deb "unit_lib:check_ovsdb_entry - Entry exists"
        return 0
    else
        log -deb "unit_lib:check_ovsdb_entry - Entry does not exists or there is issue with check, will re-check with ovsdb-client transact command"
        log -deb "unit_lib:check_ovsdb_entry - Transact string: ovsdb-client transact \'${transact_string}\'"
        res=$(eval ovsdb-client transact \'${transact_string}\')
        if [ "$?" == "0" ]; then
            echo "${res}" | grep '\[{"rows":\[\]}\]'
            if [ "$?" == "0" ]; then
                log -err "unit_lib:check_ovsdb_entry - Entry does not exists"
                return 1
            else
                log -deb "unit_lib:check_ovsdb_entry - Entry exists"
                return 0
            fi
        else
            log -err "unit_lib:check_ovsdb_entry - Entry does not exists"
            return 1
        fi
    fi
}

###############################################################################
# DESCRIPTION:
#   Function deletes all entries in ovsdb table.
#   Raises an exception if table entries cannot be deleted.
# INPUT PARAMETER(S):
#   $1  ovsdb table (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   empty_ovsdb_table AW_Debug
#   empty_ovsdb_table Wifi_Stats_Config
###############################################################################
empty_ovsdb_table()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:empty_ovsdb_table requires ${NARGS} input argument(s), $# given" -arg
    ovsdb_table=$1

    log -deb "unit_lib:empty_ovsdb_table - Clearing $ovsdb_table table"
    ${OVSH} d "$ovsdb_table" ||
        raise "FAIL: Could not delete table $ovsdb_table" -l "unit_lib:empty_ovsdb_table" -oe
}

###############################################################################
# DESCRIPTION:
#   Function checks if field exists in the specific table.
# INPUT PARAMETER(S):
#   $1  ovsdb table name (string, required)
#   $2  field name in the ovsdb table (string, required)
# RETURNS:
#   0   If field exists in ovsdb table.
#   1   If field does not exist in ovsdb table.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_ovsdb_table_field_exists AWLAN_Node device_mode
###############################################################################
check_ovsdb_table_field_exists()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:check_ovsdb_table_field_exists requires ${NARGS} input argument(s), $# given" -arg
    ovsdb_table=$1
    field_name=$2

    $(${OVSH} s "$ovsdb_table" "$field_name" &> /dev/null)
    if [ $? -eq 0 ]; then
        return 0
    else
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function checks if the OVSDB table exists or not.
#   Function uses ovsdb-client tool.
# INPUT PARAMETER(S):
#   $1  OVSDB table name (string, required).
# RETURNS:
#   0 if the OVSDB table exist.
#   1 if the OVSDB table does not exist.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_ovsdb_table_exist AWLAN_Node
###############################################################################
check_ovsdb_table_exist()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:check_ovsdb_table_exist requires ${NARGS} input argument(s), $# given" -arg
    ovsdb_table_name=${1}
    check=$(ovsdb-client list-tables | grep ${ovsdb_table_name})
    if [ -z "$check" ]; then
        return 1
    else
        return 0
    fi
}

###############################################################################
# DESCRIPTION:
#   Function waits for ovsdb table removal within given timeout.
#   Some ovsdb tables take time to be emptied. This function is used for
#   such tables and would raise exception if such table was not emptied
#   after given time.
# INPUT PARAMETER(S):
#   $1  ovsdb table (string, required)
#   $2  wait timeout in seconds (int, optional, defaults to DEFAULT_WAIT_TIME)
# RETURNS:
#   0   On success.
#   1   On fail.
# USAGE EXAMPLE(S):
#    wait_for_empty_ovsdb_table Wifi_VIF_State 60
###############################################################################
wait_for_empty_ovsdb_table()
{
    NARGS_MIN=1
    NARGS_MAX=2
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "unit_lib:wait_for_empty_ovsdb_table requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    ovsdb_table=$1
    empty_timeout=${2:-$DEFAULT_WAIT_TIME}

    log "unit_lib:wait_for_empty_ovsdb_table - Waiting for table $ovsdb_table deletion"
    wait_time=0
    while [ $wait_time -le $empty_timeout ]; do
        wait_time=$((wait_time+1))

        log -deb "unit_lib:wait_for_empty_ovsdb_table - Select $ovsdb_table, try: $wait_time"
        table_select=$(${OVSH} s "$ovsdb_table") || true

        if [ -z "$table_select" ]; then
            log -deb "unit_lib:wait_for_empty_ovsdb_table - Table $ovsdb_table is empty!"
            break
        fi

        sleep 1
    done

    if [ $wait_time -gt "$empty_timeout" ]; then
        raise "FAIL: Could not delete table $ovsdb_table" -l "unit_lib:wait_for_empty_ovsdb_table" -oe
        return 1
    else
        log -deb "unit_lib:wait_for_empty_ovsdb_table - Table $ovsdb_table deleted - Success"
        return 0
    fi
}

###############################################################################
# DESCRIPTION:
#   Function waits removal of selected entry from ovsdb table. Always
#   waits for default wait time (DEFAULT_WAIT_TIME). If selected entry is not
#   removed after wait time, it raises an exception.
#
#   It can be used with supported option(s):
#   -w (where)  field value used as a condition to select ovsdb table column
#
#   If -w option is used then two additional parameters must follow to
#   define condition string. Several -w options are possible, but for any
#   additional -w option used, there must always be 2 additional parameters.
#   In short, optional parameters come in groups of 3.
#
# INPUT PARAMETER(S):
#   $1  ovsdb table (string, required)
#   $2  option, supported options: -w (string, optional, see DESCRIPTION)
#   $3  ovsdb field in ovsdb table (string, optional, see DESCRIPTION)
#   $4  ovsdb field value (string, optional, see DESCRIPTION)
# RETURNS:
#   0   On success.
#   1   On fail.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   wait_ovsdb_entry_remove Wifi_VIF_Config -w vif_radio_idx 2 -w ssid custom_ssid
###############################################################################
wait_ovsdb_entry_remove()
{
    ovsdb_table=$1
    shift
    conditions_string=""
    info_string="unit_lib:wait_ovsdb_entry_remove - Waiting for entry removal:\n"

    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -w)
                conditions_string="$conditions_string -w $1==$2"
                info_string="$info_string where $1 is $2\n"
                shift 2
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "unit_lib:wait_ovsdb_entry_remove" -arg
                ;;
        esac
    done

    log "$info_string"
    select_entry_command="$ovsdb_table $conditions_string"
    wait_time=0
    while [ $wait_time -le $DEFAULT_WAIT_TIME ]; do
        wait_time=$((wait_time+1))

        # shellcheck disable=SC2086
        entry_select=$(${OVSH} s $select_entry_command) || true
        if [ -z "$entry_select" ]; then
            break
        fi

        sleep 1
    done

    if [ $wait_time -gt "$DEFAULT_WAIT_TIME" ]; then
        raise "FAIL: Could not remove entry from $ovsdb_table" -l "unit_lib:wait_ovsdb_entry_remove" -ow
        return 1
    else
        log -deb "unit_lib:wait_ovsdb_entry_remove - Entry deleted - Success"
        return 0
    fi
}

###############################################################################
# DESCRIPTION:
#   Function updates selected field value in ovsdb table.
#   If selected field is not updated after wait time, it raises an exception.
#
#   It can be used with supported option(s):
#   -m (update method)
#
#   -w (where)  field value used as a condition to select ovsdb table column
#
#   If -w option is used then two additional parameters must follow to
#   define condition string. Several -w options are possible, but for any
#   additional -w option used, there must always be 2 additional parameters.
#   In short, optional parameters come in groups of 3.
#
#   -u (update)
#
#   -force (force)  forces update to selected field
#
# INPUT PARAMETER(S):
#   $1  ovsdb table (string, required)
#   $2  option, supported options: -m, -w, -u, -force
#   $3  ovsdb field in ovsdb table (-w option); update method (-m option)
#   $4  ovsdb field value (-w option)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   update_ovsdb_entry Wifi_Inet_Config -w if_name br-home -u ip_assign_scheme static
###############################################################################
update_ovsdb_entry()
{
    ovsdb_table=$1
    shift
    conditions_string=""
    update_string=""
    update_method=":="
    force_insert=1

    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -m)
                update_method="$1"
                shift
                ;;
            -w)
                conditions_string="$conditions_string -w $1==$2"
                shift 2
                ;;
            -u)
                echo ${2} | grep -e "[ \"]" -e '\\' &&
                    update_string="${update_string} ${1}${update_method}$(single_quote_arg "${2}")" ||
                    update_string="${update_string} ${1}${update_method}${2}"
                shift 2
                update_method=":="
                ;;
            -force)
                force_insert=0
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "unit_lib:update_ovsdb_entry" -arg
                ;;
        esac
    done

    entry_command="${OVSH} u $ovsdb_table $conditions_string $update_string"
    log -deb "unit_lib:update_ovsdb_entry - Executing update command:\n\t$entry_command"

    eval ${entry_command}
    # shellcheck disable=SC2181
    if [ "$?" -eq 0 ]; then
        log -deb "unit_lib:update_ovsdb_entry - Entry updated"
        log -deb "${OVSH} s $ovsdb_table $conditions_string"
        # shellcheck disable=SC2086
        ${OVSH} s "$ovsdb_table" $conditions_string ||
            log -deb "unit_lib:update_ovsdb_entry - Failed to print entry"
    else
        ${OVSH} s "$ovsdb_table" || log -deb "unit_lib:update_ovsdb_entry - Failed to print table $ovsdb_table"

        if [ $force_insert -eq 0 ]; then
            log -deb "unit_lib:update_ovsdb_entry - Force insert, not failing!"
        else
            raise "FAIL: Could not update entry in $ovsdb_table" -l "unit_lib:update_ovsdb_entry" -oe
        fi
    fi

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function deletes selected row from ovsdb table. Raises an exception if
#   selected row cannot be deleted.
#   It can be used with supported option(s):
#
#   -w (where)  field value used as a condition to select ovsdb table column
#
#   If -w option is used then two additional parameters must follow to
#   define condition string. Several -w options are possible, but for any
#   additional -w option used, there must always be 2 additional parameters.
#   In short, optional parameters come in groups of 3.
#
# INPUT PARAMETER(S):
#   $1  ovsdb table (string, required)
#   $2  option, supported options: -w (optional, see DESCRIPTION)
#   $3  ovsdb field in ovsdb table (optional, see DESCRIPTION)
#   $4  ovsdb field value (optional, see DESCRIPTION)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   remove_ovsdb_entry Wifi_Inet_Config -w if_name eth0
#   remove_ovsdb_entry Wifi_VIF_Config -w vif_radio_idx 2 -w ssid custom_ssid
###############################################################################
remove_ovsdb_entry()
{
    ovsdb_table=$1
    shift
    conditions_string=""

    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -w)
                conditions_string="$conditions_string -w $1==$2"
                shift 2
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "unit_lib:remove_ovsdb_entry" -arg
                ;;
        esac
    done

    remove_command="${OVSH} d $ovsdb_table $conditions_string"
    log -deb "unit_lib:remove_ovsdb_entry - $remove_command"
    ${remove_command}
    if [ "$?" -eq 0 ]; then
        log -deb "unit_lib:remove_ovsdb_entry - Entry removed"
    else
        print_tables "$ovsdb_table" ||
            log -deb "unit_lib:remove_ovsdb_entry - Failed to print table $ovsdb_table"
        raise  "FAIL: Could not remove entry from $ovsdb_table" -l "unit_lib:remove_ovsdb_entry" -oe
    fi

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function inserts entry to provided table. Raises an exception if
#   selected entry cannot be inserted.
#   It can be used with supported option(s):
#   -w (where)  field value used as a condition to select ovsdb table column
#
#   If -w option is used then two additional parameters must follow to
#   define condition string. Several -w options are possible, but for any
#   additional -w option used, there must always be 2 additional parameters.
#   In short, optional parameters come in groups of 3.
#
#   -i (insert)         Insert requires 2 additional parameters: field and
#                       value forming insert string.
#
# INPUT PARAMETER(S):
#   $1  ovsdb table (string, required)
#   $2  option, supported options: -i
#   $3  ovsdb field in ovsdb table
#   $4  ovsdb field value
# RETURNS:
#   0   On success.
#   See DESCRIPTION:
# USAGE EXAMPLE(S):
#   insert_ovsdb_entry Wifi_Master_State -i if_type eth
###############################################################################
insert_ovsdb_entry()
{
    local ovsdb_table=$1
    shift
    local insert_string=""
    local conditions_string=""
    local insert_method=":="

    while [ -n "${1}" ]; do
        option=${1}
        shift
        case "$option" in
            -i)
                echo ${2} | grep -e "[ \"]" -e '\\' &&
                    insert_string="${insert_string} "${1}"${insert_method}"$(single_quote_arg "${2}") ||
                    insert_string="${insert_string} "${1}"${insert_method}"${2}
                insert_method=":="
                shift 2
                ;;
            -m)
                insert_method="$1"
                shift
                ;;
            -w)
                conditions_string="$conditions_string -w $1==$2"
                shift 2
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "unit_lib:insert_ovsdb_entry" -arg
                ;;
        esac
    done

    entry_command="${OVSH} i $ovsdb_table $insert_string $conditions_string"
    log -deb "unit_lib:insert_ovsdb_entry - Executing ${entry_command}"
    eval ${entry_command}
    if [ $? -eq 0 ]; then
        log -deb "unit_lib:insert_ovsdb_entry - Entry inserted to $ovsdb_table - Success"
        ${OVSH} s "$ovsdb_table"
        return 0
    else
        ${OVSH} s "$ovsdb_table"
        raise  "FAIL: Could not insert entry to $ovsdb_table" -l "unit_lib:insert_ovsdb_entry" -oe
    fi
}

###############################################################################
# DESCRIPTION:
#   Function waits for entry in ovsdb table to become
#   expected value or satisfy condition. Waits for default wait time
#   It can be used with supported option(s):
#
#   -w (where)  field value used as a condition to select ovsdb column
#   -wn (where not) field value used as a condition to ignore ovsdb column
#
#   If -w or -wn option is used then two additional parameters must follow to
#   define condition string. Several -w options are possible, but for any
#   additional -w option used, there must always be 2 additional parameters.
#   In short, optional parameters come in groups of 3.
#
#   -is             value is as provided in parameter.
#   -is_not         value is not as provided in parameter.
#
#   If -is or -is_not option is used then two additional parameters must
#   follow to define condition string. Several options are possible, but for
#   any additional option used, there must always be 2 additional parameters.
#   In short, optional parameters come in groups of 3
#
#   -ec             exit code is used as a condition
#   -ec option requires 1 addtional parameter specifying exit code.
#
#   -t              timeout in seconds
#
# INPUT PARAMETER(S):
#   $1  ovsdb table (string, required)
#   $2  option, supported options: -w, -wn, -is, -is_not, -ec
#   $3  ovsdb field in ovsdb table, exit code
#   $4  ovsdb field value
# RETURNS:
#   0   On success.
#   1   Value is not as required within timeout.
# USAGE EXAMPLE(S):
#   wait_ovsdb_entry Manager -is is_connected true
#   wait_ovsdb_entry Wifi_Inet_State -w if_name eth0 -is NAT true
#   wait_ovsdb_entry Wifi_Radio_State -w if_name wifi0 \
#   -is channel 1 -is ht_mode HT20 -t 60
###############################################################################
wait_ovsdb_entry()
{
    ovsdb_table=$1
    shift
    conditions_string=""
    where_is_string=""
    where_is_not_string=""
    expected_ec=0
    ovsh_cmd=${OVSH}
    wait_entry_not_equal_command=""
    wait_entry_not_equal_command_ec="0"
    wait_entry_equal_command_ec="0"

    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -w)
                echo ${2} | grep -e "[ \"]" -e '\\' &&
                    conditions_string="$conditions_string -w $1==$(single_quote_arg "${2}")" ||
                    conditions_string="$conditions_string -w $1==$2"
                shift 2
                ;;
            -wn)
                echo ${2} | grep -e "[ \"]" -e '\\' &&
                    conditions_string="$conditions_string -w $1!=$(single_quote_arg "${2}")" ||
                    conditions_string="$conditions_string -w $1!=$2"
                shift 2
                ;;
            -is)
                echo ${2} | grep -e "[ \"]" -e '\\' &&
                    where_is_string="$where_is_string $1:=$(single_quote_arg "${2}")" ||
                    where_is_string="$where_is_string $1:=$2"
                shift 2
                ;;
            -is_not)
                # Due to ovsh limitation, in -n option, we need to seperatly wait for NOT equal part
                echo ${2} | grep -e "[ \"]" -e '\\' &&
                    where_is_not_string="$where_is_not_string -n $1:=$(single_quote_arg "${2}")" ||
                    where_is_not_string="$where_is_not_string -n $1:=$2"
                shift 2
                ;;
            -ec)
                expected_ec=1
                ;;
            -t)
                # Timeout is given in seconds, ovsh takes milliseconds
                ovsh_cmd="${ovsh_cmd} --timeout ${1}000"
                shift
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "unit_lib:wait_ovsdb_entry" -arg
                ;;
        esac
    done

    if [ -n "${where_is_string}" ]; then
        wait_entry_equal_command="$ovsh_cmd wait $ovsdb_table $conditions_string $where_is_string"
    fi

    if [ -n "${where_is_not_string}" ]; then
        wait_entry_not_equal_command="$ovsh_cmd wait $ovsdb_table $conditions_string $where_is_not_string"
    fi

    if [ -n "${wait_entry_equal_command}" ]; then
        log "unit_lib:wait_ovsdb_entry - Waiting for entry:\n\t$wait_entry_equal_command"
        eval ${wait_entry_equal_command}
        wait_entry_equal_command_ec="$?"
    fi

    if [ -n "${wait_entry_not_equal_command}" ]; then
        log -deb "unit_lib:wait_ovsdb_entry - Waiting for entry:\n\t$wait_entry_not_equal_command"
        eval ${wait_entry_not_equal_command}
        wait_entry_not_equal_command_ec="$?"
    fi

    if [ "${wait_entry_equal_command_ec}" -eq "0" ] && [ "${wait_entry_not_equal_command_ec}" -eq "0" ]; then
        wait_entry_final_ec="0"
    else
        wait_entry_final_ec="1"
    fi

    if [ "$wait_entry_final_ec" -eq "$expected_ec" ]; then
        log -deb "unit_lib:wait_ovsdb_entry - $wait_entry_equal_command - Success"
        # shellcheck disable=SC2086
        ${OVSH} s "$ovsdb_table" $conditions_string || log -err "unit_lib:wait_ovsdb_entry: Failed to select entry: ${OVSH} s $ovsdb_table $conditions_string"
        return 0
    else
        log -deb "unit_lib:wait_ovsdb_entry - FAIL: Table $ovsdb_table"
        ${OVSH} s "$ovsdb_table" || log -err "unit_lib:wait_ovsdb_entry: Failed to print table: ${OVSH} s $ovsdb_table"
        log -deb "unit_lib:wait_ovsdb_entry - FAIL: $wait_entry_equal_command"
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function waits for expected return value from provided function call.
#   Responses can be: return values, empty, notempty.
#
#   return value (exit code): waits for the provided command to return the
#               expected exit code. Useful for commands that fail for some
#               time and succeed after some time, like "ping" or "grep"
#
#   "empty": has several meanings, it succeeds if the result of a command
#            (usually "ovsh") is blank (no echo), or the field value is
#            "unset", so it matches '["set",[]]' or '["map",[]]'
#
#   "notempty": the inverse of the above: if the command is an "ovsh" command,
#               any non-empty value means success (not blank or
#               not '["set",[]]' or not '["map",[]]').
#
# INPUT PARAMETER(S):
#   $1  value to wait for (int, required)
#   $2  function call (string, required)
#   $3  wait timeout in seconds (int, optional)
# RETURNS:
#   0   On success.
#   1   Function did not return expected value within timeout.
# USAGE EXAMPLE(S):
#   wait_for_function_response 0 "check_number_of_radios 3"
#   wait_for_function_response 1 "check_dhcp_from_dnsmasq_conf wifi0 10.10.10.16 10.10.10.32"
###############################################################################
wait_for_function_response()
{
    NARGS_MIN=2
    NARGS_MAX=3
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "unit_lib:wait_for_function_response requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    wait_for_value="$1"
    function_to_wait_for="$2"
    wait_time=${3:-$DEFAULT_WAIT_TIME}
    func_exec_time=0
    is_get_ovsdb_entry_value=1
    local retval=1

    if [ "$wait_for_value" = 'empty' ]; then
        log -deb "unit_lib:wait_for_function_response - Waiting for function $function_to_wait_for empty response"
    elif [ "$wait_for_value" = 'notempty' ]; then
        log -deb "unit_lib:wait_for_function_response - Waiting for function $function_to_wait_for not empty response"
        echo "$function_to_wait_for" | grep -q "get_ovsdb_entry_value" && is_get_ovsdb_entry_value=0
    else
        log -deb "unit_lib:wait_for_function_response - Waiting for function $function_to_wait_for exit code $wait_for_value"
    fi

    while [ $func_exec_time -le $wait_time ]; do
        log -deb "unit_lib:wait_for_function_response - Executing: $function_to_wait_for"
        func_exec_time=$((func_exec_time+1))

        if [ "$wait_for_value" = 'empty' ] || [ "$wait_for_value" = 'notempty' ]; then
            res=$(eval "$function_to_wait_for" || echo 1)
            if [ -n "$res" ]; then
                if [ "$is_get_ovsdb_entry_value" -eq 0 ]; then
                    if [ "$res" = '["set",[]]' ] || [ "$res" = '["map",[]]' ]; then
                        function_res='empty'
                    else
                        function_res='notempty'
                    fi
                else
                    function_res='notempty'
                fi
            else
                function_res='empty'
            fi
        else
            eval "$function_to_wait_for" && function_res=0 || function_res=1
        fi

        log -deb "unit_lib:wait_for_function_response - Function response/code is $function_res"

        if [ "$function_res" = "$wait_for_value" ]; then
            retval=0
            break
        fi

        sleep 1
    done

    if [ $retval = 1 ]; then
        log -deb "unit_lib:wait_for_function_response - Function $function_to_wait_for timed out"
    fi
    return $retval
}

###############################################################################
# DESCRIPTION:
#   Function waits for expected output from provided function call,
#   but not its exit code.
#   Raises an exception if times out.
#   Supported expected values are "empty", "notempty" or custom.
# INPUT PARAMETER(S):
#   (optional) $1 = -of : If first arguments is equal to -of (one-of)
#     Script will wait for one of values given in the wait for output value
#       Example:
#         wait_for_function_output "value1 value2 value3" "echo value3" 30 1
#         Script will wait for one of values (split with space!) value1, value2 or value3
#   $1  wait for output value (int, required)
#   $2  function call, function returning value (string, required)
#   $3  retry count, number of iterations to stop checks
#                    (int, optional, default=DEFAULT_WAIT_TIME)
#   $4  retry_sleep, time in seconds between checks (int, optional, default=1)
# RETURNS:
#   0   On success.
#   1   On fail.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   wait_for_function_output "notempty" <function_to_wait_for> 30 1
###############################################################################
wait_for_function_output()
{
    NARGS_MIN=2
    NARGS_MAX=5
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "unit_lib:wait_for_function_output requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    local one_of_values="false"
    if [ "${1}" == "-of" ]; then
        one_of_values="true"
        shift
    fi
    local wait_for_value=${1}
    local function_to_wait_for=${2}
    local retry_count=${3:-$DEFAULT_WAIT_TIME}
    local retry_sleep=${4:-1}
    local fn_exec_cnt=0
    local is_get_ovsdb_entry_value=0

    [ $(echo "$function_to_wait_for" | grep -wF "get_ovsdb_entry_value") ] &&
        is_get_ovsdb_entry_value=1

    log -deb "unit_lib:wait_for_function_output - Executing $function_to_wait_for, waiting for $wait_for_value response"
    while [ $fn_exec_cnt -le $retry_count ]; do
        fn_exec_cnt=$(( $fn_exec_cnt + 1 ))

        res=$($function_to_wait_for)
        if [ "$wait_for_value" = 'notempty' ]; then
            if [ $is_get_ovsdb_entry_value ]; then
                [ -n "$res" ] && [ "$res" != '["set",[]]' ] && [ "$res" != '["map",[]]' ] && return 0
            else
                [ -n "$res" ] &&
                    break
            fi
        elif [ "$wait_for_value" = 'empty' ]; then
            if [ $is_get_ovsdb_entry_value ]; then
                [ -z "$res" ] || [ "$res" = '["set",[]]' ] || [ "$res" = '["map",[]]' ] && return 0
            else
                [ -z "$res" ] &&
                    break
            fi
        else
            if [ "${one_of_values}" == "true" ]; then
                for wait_value in ${wait_for_value}; do
                    [ "$res" == "$wait_value" ] && return 0
                done
            else
                [ "$res" == "$wait_for_value" ] && return 0
            fi
        fi
        log -deb "unit_lib:wait_for_function_output - Function retry ${fn_exec_cnt} output: ${res}"
        sleep "${retry_sleep}"
    done

    if [ $fn_exec_cnt -gt "$retry_count" ]; then
        raise "FAIL: Function $function_to_wait_for timed out" -l "unit_lib:wait_for_function_output"
        return 1
    else
        return 0
    fi
}

###############################################################################
# DESCRIPTION:
#   Function waits for expected exit code, not stdout/stderr output.
#   Raises an exception if timeouts.
# INPUT PARAMETER(S):
#   $1  expected exit code (int, required)
#   $2  function call, function returning value (string, required)
#   $3  retry count, number of iterations to stop checks
#                    (int, optional, default=DEFAULT_WAIT_TIME)
#   $4  retry sleep, time in seconds between checks (int, optional, default=1)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   wait_for_function_exit_code 0 <function_to_wait_for> 30 1
###############################################################################
wait_for_function_exit_code()
{
    NARGS_MIN=2
    NARGS_MAX=4
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "unit_lib:wait_for_functin_exit_code requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    local exp_ec=$1
    local function_to_wait_for=$2
    local retry_count=${3:-$DEFAULT_WAIT_TIME}
    local retry_sleep=${4:-1}
    local fn_exec_cnt=1

    log -deb "unit_lib:wait_for_functin_exit_code - Executing $function_to_wait_for, waiting for exit code ${exp_ec}"
    res=$($function_to_wait_for)
    local act_ec=$?
    while [ ${act_ec} -ne "${exp_ec}" ]; do
        log -deb "unit_lib:wait_for_functin_exit_code - Retry ${fn_exec_cnt}, exit code: ${act_ec}, expecting: ${exp_ec}"
        if [ ${fn_exec_cnt} -ge "${retry_count}" ]; then
            raise "FAIL: Function ${function_to_wait_for} timed out" -l "unit_lib:wait_for_functin_exit_code"
        fi
        sleep "${retry_sleep}"
        res=$($function_to_wait_for)
        act_ec=$?
        fn_exec_cnt=$(( $fn_exec_cnt + 1 ))
    done

    log -deb "unit_lib:wait_for_functin_exit_code - Exit code: ${act_ec} equal to expected: ${exp_ec}"

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function returns state of the ethernet interface provided in parameter.
#   Uses and requires ifconfig tool to be installed on device.
#   Provide adequate function in overrides otherwise.
# INPUT PARAMETER(S):
#   $1  Ethernet interface name (string, required)
# RETURNS:
#   0   If ethernet interface state is UP, non zero otherwise.
# USAGE EXAMPLE(S):
#   check_eth_interface_state_is_up eth0
###############################################################################
check_eth_interface_state_is_up()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:check_eth_interface_state_is_up requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1

    ifconfig "$if_name" 2>/dev/null | grep Metric | grep UP
    return $?
}

###############################################################################
# DESCRIPTION:
#   Function returns state of the wireless interface provided in parameter.
#   Uses and requires ifconfig tool to be installed on device.
#   Provide adequate function in overrides otherwise.
# INPUT PARAMETER(S):
#   $1  VIF interface name (string, required)
# RETURNS:
#   0   If VIF interface state is up, non zero otherwise.
# USAGE EXAMPLE(S):
#   check_vif_interface_state_is_up home-ap-24
###############################################################################
check_vif_interface_state_is_up()
{
    check_eth_interface_state_is_up $@
}

###############################################################################
# DESCRIPTION:
#   Function drops interface.
#   Uses and requires ifconfig tool to be installed on device.
#   Provide adequate function in overrides otherwise.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
# RETURNS:
#   0   If interface was dropped, non zero otherwise.
# USAGE EXAMPLE(S):
#   set_interface_down eth0
###############################################################################
set_interface_down()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:set_interface_down requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1

    ifconfig "$if_name" down
    return $?
}

###############################################################################
# DESCRIPTION:
#   Function brings up interface
#   Uses and requires ifconfig tool to be installed on device.
#   Provide adequate function in overrides otherwise.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
# RETURNS:
#   0   If interface was brought up, non zero otherwise.
# USAGE EXAMPLE(S):
#   set_interface_up eth0
###############################################################################
set_interface_up()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:set_interface_up requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1

    ifconfig "$if_name" up
    return $?
}

###############################################################################
# DESCRIPTION:
#   Function returns IP address of interface provided in parameter.
#   Uses and requires ifconfig tool to be insalled on device.
#   Provide adequate function in overrides otherwise.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
# RETURNS:
#   IP address of an interface
# USAGE EXAMPLE(S):
#   check_interface_ip_address_set_on_system eth0
###############################################################################
check_interface_ip_address_set_on_system()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:check_interface_ip_address_set_on_system requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1

    ifconfig "$if_name" | tr -s ' :' '@' | grep -e '^@inet@' | cut -d '@' -f 4
}

###############################################################################
# DESCRIPTION:
#   Function returns IP address of leaf device based on LEAF MAC address.
#   Uses /tmp/dhcp.leases file as default for acquirement of leased LEAF IP address.
#   Provide adequate function in overrides otherwise.
# INPUT PARAMETER(S):
#   $1  leaf MAC address (string, required)
# RETURNS:
#   IP address of associated leaf device
# USAGE EXAMPLE(S):
#   get_associated_leaf_ip ff:ff:ff:ff:ff:ff
###############################################################################
get_associated_leaf_ip()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:get_associated_leaf_ip requires ${NARGS} input argument(s), $# given" -arg

    cat /tmp/dhcp.leases | grep "${1}" | awk '{print $3}'
}
###############################################################################
# DESCRIPTION:
#   Function returns process path of udhcpc on the device.
#   Function returns kconfig CONFIG_OSN_UDHCPC_PATH value as default
# ECHOES:
#   udhcpc path
# USAGE EXAMPLE(S):
#   get_udhcpc_path
###############################################################################
get_udhcpc_path()
{
    path=$(get_kconfig_option_value "CONFIG_OSN_UDHCPC_PATH")
    echo "${path//\"/}"
}
###############################################################################
# DESCRIPTION:
#   Function checks and restores management access to device.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   None.
# USAGE EXAMPLE(S):
#   check_restore_management_access
###############################################################################
check_restore_management_access()
{
    log "unit_lib:check_restore_management_access - Checking and restoring needed management access"
    if [ -z "${MGMT_IFACE}" ]; then
        log -deb "unit_lib:check_restore_management_access - MGMT_IFACE is not set. Nothing to do."
        return 0
    fi
    udhcpc_path=$(get_udhcpc_path)
    if [ -z "${udhcpc_path}" ]; then
        log -deb "unit_lib:check_restore_management_access - udhcpc path is not set. Nothing to do."
        return 0
    fi
    check_eth_interface_state_is_up "${MGMT_IFACE}"
    if [ "$?" = 0 ]; then
        log -deb "unit_lib:check_restore_management_access - Interface ${MGMT_IFACE} is UP"
    else
        log -deb "unit_lib:check_restore_management_access - Interface ${MGMT_IFACE} is DOWN, bringing it UP"
        wait_for_function_response 0 "ifconfig ${MGMT_IFACE} up" "${MGMT_IFACE_UP_TIMEOUT}" &&
            log -deb "unit_lib:check_restore_management_access - Interface ${MGMT_IFACE} brought UP" ||
            log -err "FAIL: Could not bring up interface ${MGMT_IFACE}" -l "unit_lib:check_restore_management_access" -ds
    fi

    check_eth_interface_state_is_up "${MGMT_IFACE}"
    if [ "$?" = 0 ]; then
        log -deb "unit_lib:check_restore_management_access - Interface ${MGMT_IFACE} is UP"
    else
        log -deb "unit_lib:check_restore_management_access - Interface ${MGMT_IFACE} is DOWN, bringing it UP"
        ifconfig "${MGMT_IFACE}" up &&
            log -deb "unit_lib:check_restore_management_access - Interface ${MGMT_IFACE} brought UP" ||
            log -deb "unit_lib:check_restore_management_access - Failed to bring up interface ${MGMT_IFACE}, checking udhcpc"
    fi

    eth_04_address=$(check_interface_ip_address_set_on_system "${MGMT_IFACE}")
    if [ -z "$eth_04_address" ]; then
        log -deb "unit_lib:check_restore_management_access - Interface ${MGMT_IFACE} has no address, setting udhcpc"
        log -deb "unit_lib:check_restore_management_access - Running force address renew for ${MGMT_IFACE}"
        check_counter=0
        while [ ${check_counter} -lt 3 ]; do
            check_counter=$(($check_counter + 1))
            ifconfig "${MGMT_IFACE}" up &&
                log -deb "unit_lib:check_restore_management_access - Interface ${MGMT_IFACE} brought UP" ||
                log -err "unit_lib:check_restore_management_access - Failed to bring up interface ${MGMT_IFACE}, checking udhcpc"
            log -deb "unit_lib:check_restore_management_access - Killing old ${MGMT_IFACE} udhcpc pids"
            dhcpcd_pids=$(pgrep -f "${udhcpc_path} .* ${MGMT_IFACE}")
            # shellcheck disable=SC2086
            kill $dhcpcd_pids &&
                log -deb "unit_lib:check_restore_management_access - ${MGMT_IFACE} udhcpc pids killed" ||
                log -err "unit_lib:check_restore_management_access - No ${MGMT_IFACE} udhcpc pid to kill"
            log -deb "unit_lib:check_restore_management_access - Starting udhcpc on '${MGMT_IFACE}'"
            ${udhcpc_path} -f -S -i "${MGMT_IFACE}" -C -o -O subnet &>/dev/null &
            log -deb "unit_lib:check_restore_management_access - Waiting for ${MGMT_IFACE} address"
            wait_for_function_response notempty "check_interface_ip_address_set_on_system ${MGMT_IFACE}" "${MGMT_CONN_TIMEOUT}"
            if [ "$?" == "0" ]; then
                check_counter=5
                log -deb "unit_lib:check_restore_management_access - ${MGMT_IFACE} $(check_interface_ip_address_set_on_system ${MGMT_IFACE}) address valid"
                break
            else
                log -err "unit_lib:check_restore_management_access - Failed to set ${MGMT_IFACE} address, repeating ${check_counter}"
            fi
        done
    else
        log -deb "unit_lib:check_restore_management_access - Interface ${MGMT_IFACE} address is '$eth_04_address'"
    fi
}

###############################################################################
# DESCRIPTION:
#   Function prints all tables provided as parameter.
#   Useful for debugging and logging to stdout.
# INPUT PARAMETER(S):
#   $@  ovsdb table name(s) (required)
# RETURNS:
#   0   Always.
# USAGE EXAMPLE(S):
#   print_tables Manager
#   print_tables Wifi_Route_State
#   print_tables Connection_Manager_Uplink
###############################################################################
print_tables()
{
    NARGS_MIN=1
    [ $# -ge ${NARGS_MIN} ] ||
        raise "unit_lib:print_tables requires at least ${NARGS_MIN} input argument(s), $# given" -arg

    for table in "$@"
    do
        log -deb "unit_lib:print_tables - OVSDB table - $table:"
        ${OVSH} s "$table"
    done

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function prints a line used as a separator in allure report.
# INPUT PARAMETER(S):
#   None
# RETURNS:
#   None
# USAGE EXAMPLE(S):
#   fut_info_dump_line
###############################################################################
fut_info_dump_line()
{
    echo "************* FUT-INFO-DUMP: $(basename $0) *************"
}

####################### OVSDB SECTION - STOP ##################################

####################### OVS SECTION - START ###################################

###############################################################################
# DESCRIPTION:
#   Function adds bridge to ovs, sets its HW address and optionally sets MTU.
#   Function uses ovs-vsctl command, different from native Linux bridge.
#   Raises an exception if
#       - bridge cannot be added,
#       - HW address cannot be set,
#       - MTU cannot be set.
# INPUT PARAMETER(S):
#   $1  Bridge name (string, required)
#   $2  HW address of bridge (string, required)
#   $3  MTU (int, optional)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   add_ovs_bridge br-home ab.34.cd.78.90.ef
###############################################################################
add_ovs_bridge()
{
    NARGS_MIN=2
    NARGS_MAX=3
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "unit_lib:add_ovs_bridge requires ${NARGS_MIN}-${NARGS_MAX} input arguments" -arg
    bridge_name=$1
    hwaddr=$2
    bridge_mtu=$3

    if [ -z "${bridge_name}" ]; then
        raise "FAIL: First input argument 'bridge_name' is empty" -l "unit_lib:add_ovs_bridge" -arg
    fi
    log -deb "unit_lib:add_ovs_bridge - Add bridge '${bridge_name}'"
    ovs-vsctl br-exists "${bridge_name}"
    if [ $? = 2 ]; then
        ovs-vsctl add-br "${bridge_name}" &&
            log -deb "unit_lib:add_ovs_bridge - ovs-vsctl add-br ${bridge_name} - Success" ||
            raise "FAIL: Could not add bridge '${bridge_name}' to ovs-vsctl" -l "unit_lib:add_ovs_bridge" -ds
    else
        log -deb "unit_lib:add_ovs_bridge - Bridge '${bridge_name}' already exists"
    fi

    # Set hwaddr if provided
    if [ -z "${hwaddr}" ]; then
        return 0
    else
        log -deb "unit_lib:add_ovs_bridge - Set bridge hwaddr to '${hwaddr}'"
        ovs-vsctl set bridge "${bridge_name}" other-config:hwaddr="${hwaddr}" &&
            log -deb "unit_lib:add_ovs_bridge - Set bridge hwaddr - Success" ||
            raise "FAIL: Could not set hwaddr to bridge '${bridge_name}'" -l "unit_lib:add_ovs_bridge" -ds
    fi

    # Set mtu if provided
    if [ -z "${bridge_mtu}" ]; then
        return 0
    else
        log -deb "unit_lib:add_ovs_bridge - Set bridge mtu ${bridge_mtu}"
        ovs-vsctl set int "${bridge_name}" mtu_request="${bridge_mtu}" &&
            log -deb "unit_lib:add_ovs_bridge - Set bridge MTU - Success" ||
            raise "FAIL: Could not set MTU to bridge '${bridge_name}'" -l "unit_lib:add_ovs_bridge" -ds
    fi

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function adds bridge to ovs bridge and adds interface to port on bridge.
#   Function uses ovs-vsctl command, different from native Linux bridge.
#   Procedure:
#       - check if bridge already exists. If not add bridge.
#       - check if interface name is provided. If provided add interface
#         to port on existing bridge
#       - set HW address and MTU if bridge is br-home
#   Raises an exception if
#       - bridge cannot be added,
#       - cannot get HW address.
# INPUT PARAMETER(S):
#   $1  Bridge name (string, required)
#   $2  Interface name (string, required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   add_interface_to_bridge br-lan eth0
###############################################################################
add_interface_to_bridge()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:add_interface_to_bridge requires ${NARGS} input argument(s), $# given" -arg
    br_name=$1
    br_if_name=$2

    log "unit_lib:add_interface_to_bridge - Adding $br_name - $br_if_name"

    ovs-vsctl br-exists "$br_name"
    if [ "$?" = 2 ]; then
        ovs-vsctl add-br "$br_name" &&
            log -deb "unit_lib:add_interface_to_bridge - ovs-vsctl add-br $br_name - Success" ||
            raise "FAIL: ovs-vsctl add-br $br_name" -l "unit_lib:add_interface_to_bridge" -ds
    else
        log -deb "unit_lib:add_interface_to_bridge - Bridge '$br_name' already exists"
        return 0
    fi

    br_mac=$(get_radio_mac_from_system "$br_if_name") &&
        log -deb "unit_lib:add_interface_to_bridge - get_radio_mac_from_system $br_if_name - Success" ||
        raise "FAIL: Could not get interface $br_if_name MAC address" -l "unit_lib:add_interface_to_bridge" -ds

    ovs-vsctl set bridge "$br_name" other-config:hwaddr="$br_mac" &&
        log -deb "unit_lib:add_interface_to_bridge - ovs-vsctl set bridge $br_name other-config:hwaddr=$br_mac - Success" ||
        raise "FAIL: Could not set to bridge $br_name other-config:hwaddr=$br_mac to ovs-vsctl" -l "unit_lib:add_interface_to_bridge" -ds

    ovs-vsctl set int "$br_name" mtu_request=1500 &&
        log -deb "unit_lib:add_interface_to_bridge - ovs-vsctl set int $br_name mtu_request=1500 - Success" ||
        raise "FAIL: ovs-vsctl set int $br_name mtu_request=1500 - Could not set to bridge '$br_name'" -l "unit_lib:add_interface_to_bridge" -ds
}

###############################################################################
# DESCRIPTION:
#   Function adds port with provided name to ovs bridge.
#   Function uses ovs-vsctl command, different from native Linux bridge.
#   Procedure:
#       - check if ovs bridge exists
#       - check if port with provided name already exists on bridge
#       - if port does not exist add port
#   Raises an exception if bridge does not exists, port already in bridge ...
#   Raises an exception if
#       - bridge does not exist,
#       - port cannot be added.
# INPUT PARAMETER(S):
#   $1  Bridge name (string, required)
#   $2  Port name (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   add_bridge_port br-home patch-h2w
###############################################################################
add_bridge_port()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:add_bridge_port requires ${NARGS} input argument(s), $# given" -arg
    bridge_name=$1
    port_name=$2

    log "unit_lib:add_bridge_port - Adding port '${port_name}' to bridge '${bridge_name}'"
    ovs-vsctl br-exists "${bridge_name}"
    if [ $? = 2 ]; then
        raise "FAIL: Bridge '${bridge_name}' does not exist" -l "unit_lib:add_bridge_port" -ds
    fi
    ovs-vsctl list-ports "${bridge_name}" | grep -wF "${port_name}"
    if [ $? = 0 ]; then
        log -deb "unit_lib:add_bridge_port - Port '${port_name}' already in bridge '${bridge_name}'"
        return 0
    else
        ovs-vsctl add-port "${bridge_name}" "${port_name}" &&
            log -deb "unit_lib:add_bridge_port - ovs-vsctl add-port ${bridge_name} ${port_name} - Success" ||
            raise "FAIL: Could not add port '${port_name}' to bridge '${bridge_name}'" -l unit_lib:add_bridge_port -ds
    fi
}

###############################################################################
# DESCRIPTION:
#   Function removes port with provided name from ovs bridge.
#   Function uses ovs-vsctl command, different from native Linux bridge.
#   Procedure:
#       - check if ovs bridge exists
#       - check if port with provided name exists on bridge
#       - if port exist removed port
#   Raises an exception if bridge does not exist
#   Raises an exception if
#       - bridge does not exist,
#       - port cannot be removed.
# INPUT PARAMETER(S):
#   $1  Bridge name (string, required)
#   $2  Port name (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   remove_bridge_port br-home patch-h2w
###############################################################################
remove_bridge_port()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:remove_bridge_port requires ${NARGS} input argument(s), $# given" -arg
    bridge_name=$1
    port_name=$2

    log "unit_lib:remove_bridge_port - Removing port '${port_name}' from bridge '${bridge_name}'"
    ovs-vsctl br-exists "${bridge_name}"
    if [ $? = 2 ]; then
        raise "FAIL: Bridge '${bridge_name}' does not exist" -l "unit_lib:remove_bridge_port" -ds
    fi
    ovs-vsctl list-ports "${bridge_name}" || true
    ovs-vsctl list-ports "${bridge_name}" | grep -wF "${port_name}"
    if [ $? = 0 ]; then
        log -deb "unit_lib:remove_bridge_port - Port '${port_name}' exists in bridge '${bridge_name}', removing."
        ovs-vsctl del-port "${bridge_name}" "${port_name}" &&
            log -deb "unit_lib:remove_bridge_port - ovs-vsctl del-port ${bridge_name} ${port_name} - Success" ||
            raise "FAIL: Could not remove port '${port_name}' from bridge '${bridge_name}'" -l unit_lib:remove_bridge_port -ds
    else
        log -deb "unit_lib:remove_bridge_port - Port '${port_name}' does not exist in bridge '${bridge_name}', nothing to do."
    fi
    return 0
}

###############################################################################
# DESCRIPTION:
#   Function removes bridge from ovs.
#   Function uses ovs-vsctl command, different from native Linux bridge.
#   Raises an exception if bridge cannot be deleted.
# INPUT PARAMETER(S):
#   $1  Bridge name (string, required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   remove_bridge_interface br-lan
###############################################################################
remove_bridge_interface()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:remove_bridge_interface requires ${NARGS} input argument(s), $# given" -arg
    br_name=$1

    ovs-vsctl del-br "$br_name" &&
        log -deb "unit_lib:remove_bridge_interface - ovs-vsctl del-br $br_name - Success" ||
        raise "FAIL: Could not remove bridge '$br_name' from ovs-vsctl" -l "unit_lib:remove_bridge_interface" -ds
}

###############################################################################
# DESCRIPTION:
#   Function removes port from bridge in ovs switch.
#   Function uses ovs-vsctl command, different from native Linux bridge.
#   Raises an exception if port cannot be deleted.
# INPUT PARAMETER(S):
#   $1  Bridge name (string, required)
#   $2  Port name (string, required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   remove_port_from_bridge br-lan br-lan.tdns
#   remove_port_from_bridge br-lan br-lan.thttp
###############################################################################
remove_port_from_bridge()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:remove_port_from_bridge requires ${NARGS} input argument(s)" -arg
    br_name=$1
    port_name=$2

    res=$(check_if_port_in_bridge "$port_name" "$br_name")
    if [ "$res" = 0 ]; then
        log -deb "unit_lib:remove_port_from_bridge - Port $port_name exists in bridge $br_name - Removing..."
        ovs-vsctl del-port "$br_name" "$port_name" &&
            log -deb "unit_lib:remove_port_from_bridge - ovs-vsctl del-port $br_name $port_name - Success" ||
            raise "Failed: ovs-vsctl del-port $br_name $port_name" -l "unit_lib:remove_port_from_bridge" -ds
    else
        log -wrn "unit_lib:remove_port_from_bridge - Port '$port_name' does not exist in bridge $br_name"
    fi
}

###############################################################################
# DESCRIPTION:
#   Function sets interface to patch port.
#   Function uses ovs-vsctl command, different from native Linux bridge.
#   Raises an exception if patch cannot be set.
# INPUT PARAMETER(S):
#   $1  interface name (string, required, not used)
#   $2  patch name (string, required)
#   $3  peer name (string, required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   set_interface_patch patch-h2w patch-w2h patch-h2w
#   set_interface_patch patch-w2h patch-h2w patch-w2h
###############################################################################
set_interface_patch()
{
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:set_interface_patch requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1
    patch=$2
    peer=$3

    ovs-vsctl set interface "$patch" type=patch &&
        log -deb "unit_lib:set_interface_patch - ovs-vsctl set interface '$patch' type=patch - Success" ||
        raise "FAIL: Could not set interface patch: ovs-vsctl set interface '$patch' type=patch" -l "unit_lib:set_interface_patch" -ds

    ovs-vsctl set interface "$patch" options:peer="$peer" &&
        log -deb "unit_lib:set_interface_patch - ovs-vsctl set interface '$patch' options:peer=$peer - Success" ||
        raise "FAIL: Could not set interface patch peer: ovs-vsctl set interface '$patch' options:peer=$peer" -l "unit_lib:set_interface_patch" -ds
}

###############################################################################
# DESCRIPTION:
#   Function sets interface option.
#   Function uses ovs-vsctl command, different from native Linux bridge.
#   Raises an exception on failure.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
#   $2  Option (string, required)
#   $3  Value (string, required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   set_ovs_vsctl_interface_option br-home.tdns type internal
#   set_ovs_vsctl_interface_option br-home.tdns ofport_request 3001
###############################################################################
set_ovs_vsctl_interface_option()
{
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:set_ovs_vsctl_interface_option requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1
    option=$2
    value=$3

    ovs-vsctl set interface "${if_name}" "${option}"="${value}" &&
        log -deb "unit_lib:set_ovs_vsctl_interface_option - ovs-vsctl set interface ${if_name} ${option}=${value} - Success" ||
        raise "FAIL: Could not set interface option: set interface ${if_name} ${option}=${value}" -l "unit_lib:set_ovs_vsctl_interface_option" -ds
}

###############################################################################
# DESCRIPTION:
#   Function checks if port is in bridge.
#   Function uses ovs-vsctl command, different from native Linux bridge.
# INPUT PARAMETER(S):
#   $1  Port name (string, required)
#   $2  Bridge name (string, required)
# RETURNS:
#   0   Port in bridge.
#   1   Port is not in bridge.
# USAGE EXAMPLE(S):
#   check_if_port_in_bridge eth0 br-lan
###############################################################################
check_if_port_in_bridge()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:check_if_port_in_bridge requires ${NARGS} input argument(s), $# given" -arg
    port_name=$1
    br_name=$2

    ovs-vsctl list-ports "$br_name" | grep "$port_name"
    if [ "$?" = 0 ]; then
        log -deb "unit_lib:check_if_port_in_bridge - Port '$port_name' exists on bridge '$br_name'"
        return 0
    else
        log -deb "unit_lib:check_if_port_in_bridge - Port '$port_name' does not exist on bridge '$br_name'"
        return 1
    fi
}

####################### OVS SECTION - STOP ####################################

####################### FUT CLOUD SECTION - START #############################

###############################################################################
# DESCRIPTION:
#   Function connects device to simulated FUT cloud.
#   Procedure:
#       - test if certificate file in provided folder exists
#       - update SSL table with certificate location and file name
#       - remove redirector address so it will not interfere
#       - set inactivity probe to 30s
#       - set manager address to FUT cloud
#       - set target in Manager table (it should be resolved by CM)
#       - wait for cloud state to become ACTIVE
#   Raises an exception on fail.
# INPUT PARAMETER(S):
#   $1  Cloud IP (string, optional, defaults to 192.168.200.1)
#   $2  Port (int, optional, defaults to 65000)
#   $3  Certificates folder (string, optional, defaults to $FUT_TOPDIR/utility/files)
#   $4  Certificate file (string, optional, defaults to fut_ca.pem)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   connect_to_fut_cloud
#   connect_to_fut_cloud 192.168.200.1 65000 fut-base/utility/files fut_ca.pem
###############################################################################
connect_to_fut_cloud()
{
    target="192.168.200.1"
    port=65000
    cert_dir="$FUT_TOPDIR/shell/tools/device/files"
    ca_fname="fut_ca.pem"
    inactivity_probe=30000

    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -t)
                target="${1}"
                shift
                ;;
            -p)
                port="${1}"
                shift
                ;;
            -cd)
                cert_dir="${1}"
                shift
                ;;
            -ca)
                ca_fname="${1}"
                shift
                ;;
            -ip)
                inactivity_probe="${1}000"
                shift
                ;;
            *)
              ;;
        esac
    done

    log -deb "unit_lib:connect_to_fut_cloud - Configure certificates, check if file exists"
    test -f "$cert_dir/$ca_fname" ||
        raise "FAIL: File $cert_dir/$ca_fname not found" -l "unit_lib:connect_to_fut_cloud" -ds

    update_ovsdb_entry SSL -u ca_cert "$cert_dir/$ca_fname"
        log -deb "unit_lib:connect_to_fut_cloud - SSL ca_cert set to $cert_dir/$ca_fname - Success" ||
        raise "FAIL: SSL ca_cert not set to $cert_dir/$ca_fname" -l "unit_lib:connect_to_fut_cloud" -ds

    # Remove redirector, to not interfere with the flow
    update_ovsdb_entry AWLAN_Node -u redirector_addr ''
        log -deb "unit_lib:connect_to_fut_cloud - AWLAN_Node redirector_addr set to '' - Success" ||
        raise "FAIL: AWLAN_Node::redirector_addr not set to ''" -l "unit_lib:connect_to_fut_cloud" -ds

    # Remove manager_addr, to not interfere with the flow
    update_ovsdb_entry AWLAN_Node -u manager_addr ''
        log -deb "unit_lib:connect_to_fut_cloud - AWLAN_Node manager_addr set to '' - Success" ||
        raise "FAIL: AWLAN_Node::manager_addr not set to ''" -l "unit_lib:connect_to_fut_cloud" -ds

    # Inactivity probe sets the timing of keepalive packets
    update_ovsdb_entry Manager -u inactivity_probe $inactivity_probe &&
        log -deb "unit_lib:connect_to_fut_cloud - Manager inactivity_probe set to $inactivity_probe - Success" ||
        raise "FAIL: Manager::inactivity_probe not set to $inactivity_probe" -l "unit_lib:connect_to_fut_cloud" -ds

    # Minimize AWLAN_Node::min_backoff timer (8s is ovsdb-server retry timeout)
    update_ovsdb_entry AWLAN_Node -u min_backoff "8" &&
        log -deb "unit_lib:connect_to_fut_cloud - AWLAN_Node min_backof set to 8 - Success" ||
        raise "FAIL: AWLAN_Node::min_backoff not set to 8" -l "unit_lib:connect_to_fut_cloud" -ds

    # Minimize AWLAN_Node::max_backoff timer
    update_ovsdb_entry AWLAN_Node -u max_backoff "9" &&
        log -deb "unit_lib:connect_to_fut_cloud - AWLAN_Node max_backof set to 9 - Success" ||
        raise "FAIL: AWLAN_Node::max_backoff not set to 9" -l "unit_lib:connect_to_fut_cloud" -ds

    # Clear Manager::target before starting
    update_ovsdb_entry Manager -u target ''
        log -deb "unit_lib:connect_to_fut_cloud - Manager target set to '' - Success" ||
        raise "FAIL: Manager::target not set to ''" -l "unit_lib:connect_to_fut_cloud" -ds

    # Wait for CM to settle
    sleep 2

    update_ovsdb_entry AWLAN_Node -u redirector_addr "ssl:$target:$port" &&
        log -deb "unit_lib:connect_to_fut_cloud - AWLAN_Node redirector_addr set to ssl:$target:$port - Success" ||
        raise "FAIL: AWLAN_Node::redirector_addr not set to ssl:$target:$port" -l "unit_lib:connect_to_fut_cloud" -ds

    log -deb "unit_lib:connect_to_fut_cloud - Waiting for FUT cloud status to go to ACTIVE - Success"
    wait_cloud_state ACTIVE &&
        log -deb "unit_lib:connect_to_fut_cloud - Manager::status is set to ACTIVE. Connected to FUT cloud. - Success" ||
        raise "FAIL: Manager::status is not ACTIVE. Not connected to FUT cloud." -l "unit_lib:connect_to_fut_cloud" -ds
}

###############################################################################
# DESCRIPTION:
#   Function echoes locationId from AWLAN_Node mqtt_headers field
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   get_location_id
###############################################################################
get_location_id()
{
    ${OVSH} s AWLAN_Node mqtt_headers |
        awk -F'"' '{for (i=1;i<NF;i++) {if ($(i)=="locationId"){print $(i+2)}}}'
}

###############################################################################
# DESCRIPTION:
#   Function echoes nodeId from AWLAN_Node mqtt_headers field
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   get_node_id
###############################################################################
get_node_id()
{
    ${OVSH} s AWLAN_Node mqtt_headers |
        awk -F'"' '{for (i=1;i<NF;i++) {if ($(i)=="nodeId"){print $(i+2)}}}'
}

###############################################################################
# DESCRIPTION:
#   Function waits for Cloud status in Manager table to become
#   as provided in parameter.
#   Cloud status can be one of:
#       ACTIVE          device is connected to the Cloud.
#       BACKOFF         device could not connect to the Cloud, will retry.
#       CONNECTING      connecting to the Cloud in progress.
#       DISCONNECTED    device is disconnected from the Cloud.
#   Raises an exception on fail.
# INPUT PARAMETER(S):
#   $1  Desired Cloud state (string, required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   wait_cloud_state ACTIVE
###############################################################################
wait_cloud_state()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:wait_cloud_state requires ${NARGS} input argument(s), $# given" -arg
    wait_for_cloud_status=$1

    log "unit_lib:wait_cloud_state - Waiting for the FUT cloud status $wait_for_cloud_status"
    wait_for_function_response 0 "${OVSH} s Manager status -r | grep \"$wait_for_cloud_status\"" &&
        log -deb "unit_lib:wait_cloud_state - FUT cloud status is $wait_for_cloud_status" ||
        raise "FAIL: FUT cloud status is not $wait_for_cloud_status" -l "unit_lib:wait_cloud_state" -ow
}

###############################################################################
# DESCRIPTION:
#   Function waits for Cloud status in Manager table not to become
#   as provided in parameter.
#   Cloud status can be one of:
#       ACTIVE          device is connected to Cloud.
#       BACKOFF         device could not connect to Cloud, will retry.
#       CONNECTING      connecting to Cloud in progress.
#       DISCONNECTED    device is disconnected from Cloud.
#   Raises an exception on fail.
# INPUT PARAMETER(S):
#   $1  un-desired cloud state (string, required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   wait_cloud_state_not ACTIVE
###############################################################################
wait_cloud_state_not()
{
    local NARGS=1
    [ $# -lt ${NARGS} ] &&
        raise "unit_lib:wait_cloud_state_not requires ${NARGS} input argument(s), $# given" -arg
    wait_for_cloud_state_not=${1}
    wait_for_cloud_state_not_timeout=${2:-60}

    log "unit_lib:wait_cloud_state_not - Waiting for cloud state not to be $wait_for_cloud_state_not"
    wait_for_function_response 0 "${OVSH} s Manager status -r | grep \"$wait_for_cloud_state_not\"" "${wait_for_cloud_state_not_timeout}" &&
        raise "FAIL: Manager::status is $wait_for_cloud_state_not" -l "unit_lib:wait_cloud_state_not" -ow ||
        log -deb "unit_lib:wait_cloud_state_not - Cloud state is not $wait_for_cloud_state_not"
}

####################### FUT CLOUD SECTION - STOP ##############################

####################### FUT CMD SECTION - START ###############################

###############################################################################
# DESCRIPTION:
#   Function checks kconfig value from ${OPENSYNC_ROOTDIR}/etc/kconfig
#   if it matches given value.
#   Raises an exception if kconfig field is missing from given path.
# INPUT PARAMETER(S):
#   $1  kconfig option name (string, required)
#   $2  kconfig option value to check (string, required)
# RETURNS:
#   0   value matches to the one in kconfig path
#   1   value does not match to the one in kconfig path
# USAGE EXAMPLE(S):
#   check_kconfig_option "CONFIG_PM_ENABLE_LED" "y"
###############################################################################
check_kconfig_option()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:check_kconfig_option requires ${NARGS} input argument(s), $# given" -arg
    kconfig_option_name=${1}
    kconfig_option_value=${2}

    kconfig_path="${OPENSYNC_ROOTDIR}/etc/kconfig"
    if ! [ -f "${kconfig_path}" ]; then
        raise "kconfig file is not present on ${kconfig_path}" -l "unit_lib:check_kconfig_option" -ds
    fi
    cat "${kconfig_path}" | grep "${kconfig_option_name}=${kconfig_option_value}"
    return $?
}

###############################################################################
# DESCRIPTION:
#   Function echoes kconfig value from ${OPENSYNC_ROOTDIR}/etc/kconfig which matches value name
#   Raises an exception if kconfig field is missing from given path.
# INPUT PARAMETER(S):
#   $1  kconfig option name (string, required)
# RETURNS:
#   None.
#   See description
# USAGE EXAMPLE(S):
#   get_kconfig_option_value "CONFIG_PM_ENABLE_LED" <- return y or n
###############################################################################
get_kconfig_option_value()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:get_kconfig_option_value requires ${NARGS} input argument(s), $# given" -arg
    kconfig_option_name=${1}

    kconfig_path="${OPENSYNC_ROOTDIR}/etc/kconfig"
    if ! [ -f "${kconfig_path}" ]; then
        raise "kconfig file is not present on ${kconfig_path}" -l "unit_lib:get_kconfig_option_value" -ds
    fi
    cat "${kconfig_path}" | grep "${kconfig_option_name}" |  cut -d "=" -f2
}

###############################################################################
# DESCRIPTION:
#   Function ensures that "dir_path" is writable.
#   Common usage is to ensure TARGET_PATH_LOG_STATE can be updated.
#   Requires the path "/tmp" to be writable, executable, tmpfs
# INPUT PARAMETER(S):
#   $1  dir_path: absolute path to dir (string, required)
# RETURNS:
#   Last exit status.
# USAGE EXAMPLE(S):
#   set_dir_to_writable "/etc"
###############################################################################
set_dir_to_writable()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "unit_lib:set_dir_to_writable: requires ${NARGS} input argument(s), $# given" -arg
    [ -n "${1}" ] || raise "Input argument empty" -l "unit_lib:set_dir_to_writable"  -arg
    [ -d "${1}" ] || raise "Input argument '${1}' is not a directory" -l "unit_lib:set_dir_to_writable"  -arg
    dir_path="${1}"
    subst_dir=${dir_path//\//_}

    if touch ${dir_path}/.test_write 2>/dev/null; then
        rm -f ${dir_path}/.test_write
    else
        mkdir -p /tmp/${subst_dir}-ro
        mkdir -p /tmp/${subst_dir}-rw
        mount --bind ${dir_path} /tmp/${subst_dir}-ro
        ln -sf /tmp/${subst_dir}-ro/* /tmp/${subst_dir}-rw/
        mount --bind /tmp/${subst_dir}-rw ${dir_path}
    fi
}

####################### FUT CMD SECTION - STOP ################################

###############################################################################
# DESCRIPTION:
#   Function triggers cloud reboot by creating Wifi_Test_Config table
#   and inserting reboot command to table.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   Always
# USAGE EXAMPLE(S):
#   trigger_cloud_reboot
###############################################################################
trigger_cloud_reboot()
{
    params='["map",[["arg","5"],["path","/usr/plume/bin/delayed-reboot"]]]'
    insert_ovsdb_entry Wifi_Test_Config -i params "$params" -i test_id reboot

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function creates tap interface on bridge with selected Openflow port.
#   Raises an exception if not in the path.
# INPUT PARAMETER(S):
#   $1  Bridge name (string, required)
#   $2  Interface name (string, required)
#   $3  Open flow port (string, required)
# RETURNS:
#   0   On success.
# USAGE EXAMPLE(S):
#   add_tap_interface br-home br-home.tdns 3001
#   add_tap_interface br-home br-home.tx 401
###############################################################################
add_tap_interface()
{
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "fsm_lib:add_tap_interface requires ${NARGS} input arguments, $# given" -arg
    bridge=$1
    intf=$2
    ofport=$3
    log -deb "fsm_lib:add_tap_interface - Generating tap interface '${intf}' on bridge '${bridge}'"
    ovs-vsctl add-port "${bridge}" "${intf}"  \
        -- set interface "${intf}"  type=internal \
        -- set interface "${intf}"  ofport_request="${ofport}"
}
####################### FUT CMD SECTION - STOP ################################
