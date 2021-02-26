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
#   Function returns filename of the script manipulating OpenSync managers.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   Path to managers script.
# USAGE EXAMPLE(S):
#   get_managers_script
###############################################################################
get_managers_script()
{
    echo "/etc/init.d/opensync"
}

###############################################################################
# DESCRIPTION:
#   Function returns filename of the script manipulating openvswitch.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   Path to openvswitch script.
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
#   Command to list processes.
# USAGE EXAMPLE(S):
#   get_process_cmd
###############################################################################
get_process_cmd()
{
    echo "ps -w"
}

###############################################################################
# DESCRIPTION:
#   Function gets MAC address of a provided interface.
#   Function supports ':' delimiter only.
# INPUT PARAMETER(S):
#   $1 interface name (required)
# RETURNS:
#   HW address of interface.
# USAGE EXAMPLE(S):
#   mac_get eth0
###############################################################################
mac_get()
{
    fn_name="unit_lib:mac_get"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1

    # Match 2 alfanum chars and a : five times plus additional 2 alfanum chars
    ifconfig "$if_name" | grep -o -E '([A-F0-9]{2}:){5}[A-F0-9]{2}'
}

###############################################################################
# DESCRIPTION:
#   Function returns MAC of radio interface from Wifi_Radio_State table.
#   Using condition string interface can be selected by name, channel,
#   freqency band etc. See USAGE EXAMPLE(S).
# INPUT PARAMETER(S):
#   $1 condition string (required)
# RETURNS:
#   Radio interface MAC address.
# USAGE EXAMPLES(S):
#   get_radio_mac_from_ovsdb "freq_band==5GL"
#   get_radio_mac_from_ovsdb "if_name==wifi1"
#   get_radio_mac_from_ovsdb "channel==44"
###############################################################################
get_radio_mac_from_ovsdb()
{
    fn_name="unit_lib:get_radio_mac_from_ovsdb"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    local where_clause=$1

    # No logging, this function echoes the requested value to caller!
    ${OVSH} s Wifi_Radio_State -w ${where_clause} mac -r
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
#   $1 interface name (required)
#   $2 option to select if IP address is requested (optional)
# RETURNS:
#   0   on success
#   See DESCRIPTION.
# USAGE EXAMPLES(S):
#   start_udhcpc eth0 true
#   start_udhcpc eth0 false
###############################################################################
start_udhcpc()
{
    fn_name="unit_lib:start_udhcpc"
    NARGS_MIN=1
    NARGS_MAX=2
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "${fn_name} requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    if_name=$1
    should_get_address=${2:-false}

    log -deb "$fn_name - Starting udhcpc on $if_name"

    ps_out=$(pgrep "/sbin/udhcpc.*$if_name")
    if [ $? -eq 0 ]; then
        # shellcheck disable=2086
        kill $ps_out && log -deb "$fn_name - Old udhcpc pid killed for $if_name"
    fi

    /sbin/udhcpc -i "$if_name" -f -p /var/run/udhcpc-"$if_name".pid -s ${OPENSYNC_ROOTDIR}/bin/udhcpc.sh -t 60 -T 1 -S --no-default-options &>/dev/null &

    if [ "$should_get_address" = "true" ]; then
        wait_for_function_response 'notempty' "interface_ip_address $if_name" &&
            log "$fn_name - DHCPC provided address to $if_name" ||
            raise "FAIL: DHCPC didn't provide address to $if_name" -l "$fn_name" -ds
    fi

    return 0
}

####################### UTILITY SECTION - STOP ################################


####################### PROCESS SECTION - START ###############################

###############################################################################
# DESCRIPTION:
#   Function echoes PID of provided process.
# INPUT PARAMETER(S):
#   $1 process name (required)
# ECHOES:
#   PID value.
# USAGE EXAMPLE(S):
#   get_pid "healthcheck"
###############################################################################
get_pid()
{
    fn_name="unit_lib:get_pid"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    process_name=$1

    # Match parameter string, but exclude lines containing 'grep'.
    PID=$($(get_process_cmd) | grep -e "$process_name" | grep -v 'grep' | awk '{ print $1 }')
    echo "$PID"
}

###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
# RETURNS:
#   0
#   1
# USAGE EXAMPLE(S):
#   check_pid_file alive \"/var/run/udhcpc-$if_name.pid\"
#   check_pid_file dead \"/var/run/udhcpc-$if_name.pid\"
###############################################################################
check_pid_file()
{
    fn_name="unit_lib:check_pid_file"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    type=$1
    file=$2

    if [ "$type" = "dead" ] && [ ! -f "$file" ]; then
        log -deb "$fn_name - Process $file is dead"
        return 0
    elif [ "$type" = "alive" ] && [ -f "$file" ]; then
        log -deb "$fn_name - Process $file is alive"
        return 0
    elif [ "$type" = "dead" ] && [ -f "$file" ]; then
        log -deb "$fn_name - Process is alive"
        return 1
    else
        log -deb "$fn_name - Process is dead"
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function checks if udhcps service (DHCP client) on provided interface is
#   running. It does so by checking existance of PID of DHCP client service.
# INPUT PARAMETER(S):
#   $1  interface name (required)
# RETURNS:
#   0   PID found, udhcpc service is running
#   1   PID not found, udhcpc service is not running
# USAGE EXAMPLE(S):
#   check_pid_udhcp br-wan
###############################################################################
check_pid_udhcp()
{
    local fn_name="unit_lib:check_pid_udhcp"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    local if_name="${1}"

    PID=$($(get_process_cmd) | grep -e udhcpc | grep -e "${if_name}" | grep -v 'grep' | awk '{ print $1 }')
    if [ -z "$PID" ]; then
        log -deb "${fn_name} - DHCP client not running on ${if_name}"
        return 1
    else
        log -deb "${fn_name} - DHCP client running on ${if_name}, PID=${PID}"
        return 0
    fi
}

###############################################################################
# DESCRIPTION:
#   Function kills process of provided name.
#   Required binary/tool pidof installed on system.
# INPUT PARAMETER(S):
#   $1 process name (required)
# RETURNS:
#   None.
# USAGE EXAMPLE(S):
#   killall_process_by_name <process name>
###############################################################################
killall_process_by_name()
{
    fn_name="unit_lib:killall_process_by_name"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
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
                log -deb "${fn_name} - killed process:${P} with signal:${S}"
            else
                log -deb "${fn_name} - killall_process_by_name - could not kill process:${P}"
            fi
        done
    fi
}

####################### PROCESS SECTION - STOP ################################


####################### SETUP SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function initializes device for use in FUT.
#   It disables watchdog and stops all managers.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   Last exit status.
# USAGE EXAMPLE(S):
#   device_init
###############################################################################
device_init()
{
    disable_watchdog
    stop_managers
    return $?
}

####################### SETUP SECTION - STOP ##################################


####################### WATCHDOG SECTION - START ##############################

###############################################################################
# DESCRIPTION:
#   Function disables watchdog.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   Always.
# NOTE:
#   This is a stub function. Provide function for each device in overrides.
# USAGE EXAMPLE(S):
#   disable_watchdog
###############################################################################
disable_watchdog()
{
    local fn_name="unit_lib:disable_watchdog"
    log -deb "$fn_name - Disabling watchdog."
    log -deb "$fn_name - This is a stub function. Override implementation needed for each model."
    return 0
}

####################### WATCHDOG SECTION - STOP ###############################


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
    fn_name="unit_lib:start_openswitch"

    log -deb "$fn_name - Starting Open vSwitch"
    # shellcheck disable=SC2034,2091
    ovs_run=$($(get_process_cmd)  | grep -v "grep" | grep "ovs-vswitchd")
    if [ "$?" -eq 0 ]; then
        log -deb "$fn_name - Open vSwitch already running"
        return 0
    fi

    OPENVSWITCH_SCRIPT=$(get_openvswitch_script)
    ${OPENVSWITCH_SCRIPT} start ||
        raise "FAIL: Issue during Open vSwitch start" -l "$fn_name" -ds

    wait_for_function_response 0 "pidof ovs-vswitchd" &&
        log -deb "$fn_name - ovs-vswitchd running" ||
        raise "FAIL: Could not start ovs-vswitchd" -l "$fn_name" -ds

    wait_for_function_response 0 "pidof ovsdb-server" &&
        log -deb "$fn_name - ovsdb-server running" ||
        raise "FAIL: Could not start ovsdb-server" -l "$fn_name" -ds

    sleep 1
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
    fn_name="unit_lib:start_managers"

    log -deb "$fn_name Starting OpenSync managers"
    MANAGER_SCRIPT=$(get_managers_script)
    ret=$($MANAGER_SCRIPT start)
    # Make sure to define return value on success or failure
    if [ $? -ne 1 ]; then
        raise "FAIL: Issue during OpenSync manager start" -l "$fn_name" -ds
    else
        log "$fn_name - OpenSync managers started"
    fi

    # Check dm slave PID
    # shellcheck disable=2091
    PID=$($(get_process_cmd) | grep -e "${OPENSYNC_ROOTDIR}/bin/dm" | grep -v 'grep' | grep -v slave | awk '{ print $1 }')
    if [ -z "$PID" ]; then
        raise "FAIL: Issue during manager start, dm slave not running" -l "$fn_name" -ds
    else
        log "$fn_name - dm slave PID = $PID"
    fi

    # Check dm master PID
    # shellcheck disable=2091
    PID=$($(get_process_cmd) | grep -e "${OPENSYNC_ROOTDIR}/bin/dm" | grep -v 'grep' | grep -v master | awk '{ print $1 }')
    if [ -z "$PID" ]; then
        raise "FAIL: Issue during manager start, dm master not running" -l "$fn_name" -ds
    else
        log "$fn_name - dm master PID = $PID"
    fi

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function re-starts all OpenSync managers.
#   Executes managers script with restart option.
# INPUT PARAMETER(S):
#   None.
# USAGE EXAMPLE(S):
#   restart_managers
# RETURNS:
#   Last exit status.
###############################################################################
restart_managers()
{
    fn_name="unit_lib:restart_managers"
    log -deb "$fn_name - Restarting OpenSync managers"
    MANAGER_SCRIPT=$(get_managers_script)
    # shellcheck disable=2034
    ret=$($MANAGER_SCRIPT restart)
    ec=$?
    log -deb "$fn_name - manager restart exit code ${ec}"
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
    fn_name="unit_lib:stop_managers"
    log -deb "$fn_name - Stopping OpenSync managers"
    MANAGER_SCRIPT=$(get_managers_script)
    $MANAGER_SCRIPT stop ||
        raise "FAIL: Issue during OpenSync manager stop" -l "$fn_name" -ds
}

###############################################################################
# DESCRIPTION:
#   Function starts a single specific OpenSync manager.
#   Raises an exception if managers script is not executable or if it
#   does not exist.
# INPUT PARAMETER(S):
#   $1  manager name (required)
#   $2  option used with start manager (optional)
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
    fn_name="unit_lib:start_specific_manager"
    NARGS_MIN=1
    NARGS_MAX=2
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "${fn_name} requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    manager="${OPENSYNC_ROOTDIR}/bin/$1"
    option=$2

    # Check if executable
    if [ ! -x "$manager" ]; then
        log -deb "$fn_name - Manager $manager does not exist or is not executable"
        return 1
    fi

    # Start manager
    # shellcheck disable=SC2018,SC2019
    log -deb "$fn_name - Starting $manager $option" | tr a-z A-Z
    $manager "$option" >/dev/null 2>&1 &
    sleep 1
}

###############################################################################
# DESCRIPTION:
#   Function sets log severity for manager by managing AW_Debug table.
#   Possible log severity levels are INFO, TRACE, DEBUG...
#   Raises an exception if log severity is not succesfully set.
# INPUT PARAMETER(S):
#   $1 manager name (required)
#   $2 log severity (required)
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   set_manager_log NM TRACE
#   set_manager_log WM TRACE
###############################################################################
set_manager_log()
{
    fn_name="unit_lib:set_manager_log"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    manager_name=$1
    severity=$2

    log -deb "$fn_name - Adding $manager_name to AW_Debug with severity $severity"
    insert_ovsdb_entry AW_Debug -i name "$manager_name" -i log_severity "$severity" ||
        raise "FAIL: Could not insert to table AW_Debug::log_severity" -l "$fn_name" -oe
}

###############################################################################
# DESCRIPTION:
#   Function runs manager setup if any of the managers in provided
#   parameters crashed. Checks existance of PID for provided managers.
#   If not found run setup for the crashed manager.
# INPUT PARAMETER(S):
#   $@  crashed managers
# RETURNS:
#   0   On success.
#   1   Manager is not executable.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   run_setup_if_crashed wm
#   run_setup_if_crashed nm cm
###############################################################################
run_setup_if_crashed()
{
    fn_name="unit_lib:run_setup_if_crashed"
    for manager in "$@"
    do
        manager_pid_file="${OPENSYNC_ROOTDIR}/bin/$manager"
        pid_of_manager=$(get_pid "$manager_pid_file")
        if [ -z "$pid_of_manager" ]; then
            log -deb "$fn_name - Manager $manager crashed. Executing module environment setup"
            eval "${manager}_setup_test_environment" &&
                log -deb "$fn_name - Test environment for $manager success" ||
                raise "FAIL: Test environment for $manager failed" -l "$fn_name" -tc
        fi
    done
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
#   cm_disable_fatal_state
###############################################################################
cm_disable_fatal_state()
{
    fn_name="unit_lib:cm_disable_fatal_state"
    log -deb "$fn_name - Disabling CM manager restart procedure"
    if [ ! -d /opt/tb ]; then
        mkdir -p /opt/tb/
    fi
    # Create cm-disable-fatal file in /opt/tb/
    touch /opt/tb/cm-disable-fatal
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
#   cm_enable_fatal_state
###############################################################################
cm_enable_fatal_state()
{
    fn_name="unit_lib:cm_enable_fatal_state"
    log -deb "$fn_name - Enabling CM manager restart procedure"
    # Delete cm-disable-fatal file in /opt/tb/
    rm -f /opt/tb/cm-disable-fatal
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
#   -raw (raw)  output value is echoed without formatting
#   -r (raw)    output value is echoed without formatting
#
# INPUT PARAMETER(S):
#   $1  ovsdb table (required)
#   $2  ovsdb field in ovsdb table (required)
#   $3  option, supported options: -w, -raw (optional, see DESCRIPTION)
#   $4  ovsdb field in ovsdb table (optional, see DESCRIPTION)
#   $5  ovsdb field value (optional, see DESCRIPTION)
#   ...
# ECHOES:
#   Echoes field value.
# USAGE EXAMPLE(S):
#   get_ovsdb_entry_value AWLAN_Node firmware_version
#   get_ovsdb_entry_value Manager target -raw
###############################################################################
get_ovsdb_entry_value()
{
    fn_name="unit_lib:get_ovsdb_entry_value"
    ovsdb_table=$1
    ovsdb_field=$2
    shift 2
    conditions_string=""
    raw=false

    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -w)
                conditions_string="$conditions_string -w $1==$2"
                shift 2
                ;;
            -raw)
                raw=true
                shift
                ;;
            -r)
                raw=true
                shift
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "$fn_name" -arg
                ;;
        esac
    done

    # shellcheck disable=SC2086
    raw_field_value=$(${OVSH} s "$ovsdb_table" $conditions_string "$ovsdb_field" -r) ||
        return 1

    echo "$raw_field_value" | grep -q '"uuid"'
    uuid_check_res="$?"
    if [ "$raw" == false ] && [ "$uuid_check_res" == 0 ]; then
        value=$(echo "$raw_field_value" | cut -d ',' -f 2 | cut -d '"' -f 2)
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
#   $1  ovsdb table (required)
#   $2  option, supported options: -w (optional, see DESCRIPTION)
#   $3  ovsdb field in ovsdb table (optional, see DESCRIPTION)
#   $4  ovsdb field value (optional, see DESCRIPTION)
#   ...
# RETURNS:
#   0   Value is as expected.
#   1   Value is not as expected.
# USAGE EXAMPLE(S):
#   check_ovsdb_entry AWLAN_Node -w model <model>
###############################################################################
check_ovsdb_entry()
{
    fn_name="unit_lib:check_ovsdb_entry"
    ovsdb_table=$1
    shift 1
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
                raise "FAIL: Wrong option provided: $option" -l "$fn_name" -arg
                ;;
        esac
    done

    check_cmd="${OVSH} s $ovsdb_table $conditions_string"

    log "$fn_name - Checking if entry exists \n$check_cmd"
    eval "$check_cmd" && return 0 || return 1
}

###############################################################################
# DESCRIPTION:
#   Function deletes all entries in ovsdb table.
#   Raises an exception if table emtries cannot be deleted.
# INPUT PARAMETER(S):
#   $1  ovsdb table (required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   empty_ovsdb_table AW_Debug
#   empty_ovsdb_table Wifi_Stats_Config
###############################################################################
empty_ovsdb_table()
{
    fn_name="unit_lib:empty_ovsdb_table"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    ovsdb_table=$1

    log -deb "$fn_name - Clearing $ovsdb_table table"
    ${OVSH} d "$ovsdb_table" ||
        raise "FAIL: Could not delete table $ovsdb_table" -l "$fn_name" -oe
}

###############################################################################
# DESCRIPTION:
#   Function waits for ovsdb table removal within given timeout.
#   Some ovsdb tables take time to be emptied. This function is used for
#   such tables and would raise exception that table was not emptied
#   only after given time.
# INPUT PARAMETER(S):
#   $1  ovsdb table (required)
#   $2  wait timeout in seconds (optional, defaults to DEFAULT_WAIT_TIME)
# RETURNS:
#   0   On success.
#   1   On fail.
# USAGE EXAMPLE(S):
#    wait_for_empty_ovsdb_table Wifi_VIF_State 60
###############################################################################
wait_for_empty_ovsdb_table()
{
    fn_name="unit_lib:wait_for_empty_ovsdb_table"
    NARGS_MIN=1
    NARGS_MAX=2
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "${fn_name} requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    ovsdb_table=$1
    empty_timeout=${2:-$DEFAULT_WAIT_TIME}

    log -deb "$fn_name - Waiting for table $ovsdb_table deletion"
    wait_time=0
    while [ $wait_time -le $empty_timeout ]; do
        wait_time=$((wait_time+1))

        log -deb "$fn_name - Select $ovsdb_table try $wait_time"
        table_select=$(${OVSH} s "$ovsdb_table") || true

        if [ -z "$table_select" ]; then
            log -deb "$fn_name - Table $ovsdb_table is empty!"
            break
        fi

        sleep 1
    done

    if [ $wait_time -gt "$empty_timeout" ]; then
        raise "FAIL: Could not delete table $ovsdb_table" -l "$fn_name" -oe
        return 1
    else
        log -deb "$fn_name - Table $ovsdb_table successfully deleted!"
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
#   $1  ovsdb table (required)
#   $2  option, supported options: -w (optional, see DESCRIPTION)
#   $3  ovsdb field in ovsdb table (optional, see DESCRIPTION)
#   $4  ovsdb field value (optional, see DESCRIPTION)
# RETURNS:
#   0   On success.
#   1   On fail.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   wait_ovsdb_entry_remove Wifi_VIF_Config -w vif_radio_idx 2 -w ssid custom_ssid
###############################################################################
wait_ovsdb_entry_remove()
{
    fn_name="unit_lib:wait_ovsdb_entry_remove"
    ovsdb_table=$1
    shift
    conditions_string=""
    info_string="$fn_name - Waiting for entry removal:\n"

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
                raise "FAIL: Wrong option provided: $option" -l "$fn_name" -arg
                ;;
        esac
    done

    log -deb "$info_string"
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
        raise "FAIL: Could not remove entry from $ovsdb_table" -l "$fn_name" -ow
        return 1
    else
        log -deb "$fn_name - Entry deleted"
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
#   $1  ovsdb table (required)
#   $2  option, supported options: -m, -w, -u, -force
#   $3  ovsdb field in ovsdb table (-w option); update method (-m option)
#   $4  ovsdb field value (-w option)
# RETURNS:
#   0   On success.
# USAGE EXAMPLE(S):
#   update_ovsdb_entry Wifi_Inet_Config -w if_name br-home -u ip_assign_scheme static
###############################################################################
update_ovsdb_entry()
{
    fn_name="unit_lib:update_ovsdb_entry"
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
                update_string="$update_string $1$update_method$2"
                shift 2
                update_method=":="
                ;;
            -force)
                force_insert=0
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "$fn_name" -arg
                ;;
        esac
    done

    entry_command="${OVSH} u $ovsdb_table $conditions_string $update_string"
    log -deb "$fn_name - Executing update command\n$entry_command"

    ${entry_command}
    # shellcheck disable=SC2181
    if [ "$?" -eq 0 ]; then
        log -deb "$fn_name - Entry updated"
        log -deb "${OVSH} s $ovsdb_table $conditions_string"
        # shellcheck disable=SC2086
        ${OVSH} s "$ovsdb_table" $conditions_string ||
            log -deb "$fn_name - Failed to print entry"
    else
        ${OVSH} s "$ovsdb_table" || log -deb "$fn_name - Failed to print table $ovsdb_table"

        if [ $force_insert -eq 0 ]; then
            log -deb "$fn_name - Force entry, not failing!"
        else
            raise "FAIL: Could not update entry in $ovsdb_table" -l "$fn_name" -oe
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
#   $1  ovsdb table (required)
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
    fn_name="unit_lib:remove_ovsdb_entry"
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
                raise "FAIL: Wrong option provided: $option" -l "$fn_name" -arg
                ;;
        esac
    done

    remove_command="${OVSH} d $ovsdb_table $conditions_string"
    log -deb "$fn_name - $remove_command"
    ${remove_command}
    if [ "$?" -eq 0 ]; then
        log -deb "$fn_name - Entry removed"
    else
        print_tables "$ovsdb_table" ||
            log -deb "$fn_name - Failed to print table $ovsdb_table"
        raise  "FAIL: Could not remove entry from $ovsdb_table" -l "$fn_name" -oe
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
#   $1  ovsdb table
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
    local fn_name="unit_lib:insert_ovsdb_entry"
    local ovsdb_table=$1
    shift
    local insert_string=""
    local conditions_string=""

    while [ -n "${1}" ]; do
        option=${1}
        shift
        case "$option" in
            -i)
                insert_string="${insert_string} "${1}":="${2}
                shift 2
                ;;
            -w)
                conditions_string="$conditions_string -w $1==$2"
                shift 2
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "$fn_name" -arg
                ;;
        esac
    done

    entry_command="${OVSH} i $ovsdb_table $insert_string $conditions_string"
    log -deb "$fn_name - Executing ${entry_command}"
    ${entry_command}
    if [ $? -eq 0 ]; then
        log -deb "$fn_name - Success: entry inserted to $ovsdb_table"
        ${OVSH} s "$ovsdb_table"
        return 0
    else
        ${OVSH} s "$ovsdb_table"
        raise  "FAIL: Could not insert entry to $ovsdb_table" -l "$fn_name" -oe
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
#   $1  ovsdb table (required)
#   $2  option, supported options: -w, -wn, -is, -is_not, -ec
#   $3  ovsdb field in ovsdb table, exit code
#   $4  ovsdb field value
# RETURNS:
#   0   On success.
#   1   Value is not as required within timeout.
# USAGE EXAMPLE(S):
#   wait_ovsdb_entry Manager -is is_connected true
#   wait_ovsdb_entry Wifi_Inet_State -w if_name br-wan -is NAT true
#   wait_ovsdb_entry Wifi_Radio_State -w if_name wifi0 \
#   -is channel 1 -is ht_mode HT20 -t 60
###############################################################################
wait_ovsdb_entry()
{
    fn_name="unit_lib:wait_ovsdb_entry"
    ovsdb_table=$1
    shift
    conditions_string=""
    where_is_string=""
    expected_ec=0
    ovsh_cmd=${OVSH}

    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -w)
                conditions_string="$conditions_string -w $1==$2"
                shift 2
                ;;
            -wn)
                conditions_string="$conditions_string -w $1!=$2"
                shift 2
                ;;
            -is)
                where_is_string="$where_is_string $1:=$2"
                shift 2
                ;;
            -is_not)
                where_is_string="$where_is_string -n $1:=$2"
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
                raise "FAIL: Wrong option provided: $option" -l "$fn_name" -arg
                ;;
        esac
    done

    wait_entry_command="$ovsh_cmd wait $ovsdb_table $conditions_string $where_is_string"
    wait_time=0

    log -deb "$fn_name - Waiting for entry: \n$wait_entry_command"

    ${wait_entry_command} 2>/dev/null
    actual_ec="$?"
    if [ "$actual_ec" -eq "$expected_ec" ]; then
        log -deb "$fn_name - SUCCESS: $wait_entry_command"
        # shellcheck disable=SC2086
        ${OVSH} s "$ovsdb_table" $conditions_string
        return 0
    # ovsh exit code 255 = ovsh wait timed-out
    elif [ "$expected_ec" -eq "1" ] && [ "$actual_ec" -eq "255" ]; then
        log -deb "$fn_name - SUCCESS: $wait_entry_command"
        # shellcheck disable=SC2086
        ${OVSH} s "$ovsdb_table" $conditions_string
        return 0
    else
        log -deb "$fn_name - FAIL: Table $ovsdb_table"
        ${OVSH} s "$ovsdb_table" || true
        log -deb "$fn_name - FAIL: $wait_entry_command"
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
#   $1  value to wait for (required)
#   $2  function call (required)
#   $3  wait timeout in seconds (optional)
# RETURNS:
#   0   On success.
#   1   Function did not return expected value within timeout.
# USAGE EXAMPLE(S):
#   wait_for_function_response 0 "check_number_of_radios 3"
#   wait_for_function_response 1 "wait_for_dnsmasq wifi0 10.10.10.16 10.10.10.32"
###############################################################################
wait_for_function_response()
{
    fn_name="unit_lib:wait_for_function_response"
    NARGS_MIN=2
    NARGS_MAX=3
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "${fn_name} requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    wait_for_value="$1"
    function_to_wait_for="$2"
    wait_time=${3:-$DEFAULT_WAIT_TIME}
    func_exec_time=0
    is_get_ovsdb_entry_value=1
    local retval=1

    if [ "$wait_for_value" = 'empty' ]; then
        log -deb "$fn_name - Waiting for function $function_to_wait_for empty response"
    elif [ "$wait_for_value" = 'notempty' ]; then
        log -deb "$fn_name - Waiting for function $function_to_wait_for not empty response"
        echo "$function_to_wait_for" | grep -q "get_ovsdb_entry_value" && is_get_ovsdb_entry_value=0
    else
        log -deb "$fn_name - Waiting for function $function_to_wait_for exit code $wait_for_value"
    fi

    while [ $func_exec_time -le $wait_time ]; do
        log -deb "$fn_name - Executing: $function_to_wait_for"
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

        log -deb "$fn_name - Function response/code is $function_res"

        if [ "$function_res" = "$wait_for_value" ]; then
            retval=0
            break
        fi

        sleep 1
    done

    if [ $retval = 1 ]; then
        log -deb "$fn_name - Function $function_to_wait_for timed out"
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
#   $1  wait for output value (required)
#   $2  function call, string with the function to wait for output (required)
#   $3  retry count, number of iterations to stop checks
#                    (optional, default=DEFAULT_WAIT_TIME)
#   $4  retry_sleep, time in seconds between checks (optional, default=1)
# RETURNS:
#   0   On success.
#   1   On fail.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   wait_for_function_output "notempty" <function_to_wait_for> 30 1
###############################################################################
wait_for_function_output()
{
    local fn_name="unit_lib:wait_for_function_output"
    NARGS_MIN=2
    NARGS_MAX=4
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "${fn_name} requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    local wait_for_value=$1
    local function_to_wait_for=$2
    local retry_count=${3:-$DEFAULT_WAIT_TIME}
    local retry_sleep=${4:-1}
    local fn_exec_cnt=0
    local is_get_ovsdb_entry_value=0

    [ $(echo "$function_to_wait_for" | grep -qwF "get_ovsdb_entry_value") ] &&
        is_get_ovsdb_entry_value=1

    log -deb "$fn_name - Executing $function_to_wait_for, waiting for $wait_for_value response"
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
            [ "$res" = "$wait_for_value" ] && return 0
        fi

        log -deb "$fn_name - Function retry ${fn_exec_cnt} output: ${res}"

        sleep "${retry_sleep}"
    done

    if [ $fn_exec_cnt -gt "$retry_count" ]; then
        raise "FAIL: Function $function_to_wait_for timed out" -l "${fn_name}"
        return 1
    else
        return 0
    fi
}

###############################################################################
# DESCRIPTION:
#   Function waits for expected exit code, not stdout/stderr output.
#   Raises an exception if times out.
# INPUT PARAMETER(S):
#   $1  expected exit code (required)
#   $2  function call, string with the function to wait for output (required)
#   $3  retry count, number of iterations to stop checks
#                    (optional, default=DEFAULT_WAIT_TIME)
#   $4 retry sleep, time in seconds between checks (optional, default=1)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   wait_for_function_exitcode 0 <function_to_wait_for> 30 1
###############################################################################
wait_for_function_exitcode()
{
    local fn_name="unit_lib:wait_for_function_exitcode"
    NARGS_MIN=2
    NARGS_MAX=4
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "${fn_name} requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    local exp_ec=$1
    local function_to_wait_for=$2
    local retry_count=${3:-$DEFAULT_WAIT_TIME}
    local retry_sleep=${4:-1}
    local fn_exec_cnt=1

    log -deb "${fn_name} - Executing $function_to_wait_for, waiting for exit code ${exp_ec}"
    res=$($function_to_wait_for)
    local act_ec=$?
    while [ ${act_ec} -ne "${exp_ec}" ]; do
        log -deb "${fn_name} - Retry ${fn_exec_cnt}, exit code: ${act_ec}, expecting: ${exp_ec}"
        if [ ${fn_exec_cnt} -ge "${retry_count}" ]; then
            raise "FAIL: Function ${function_to_wait_for} timed out" -l "${fn_name}"
        fi
        sleep "${retry_sleep}"
        res=$($function_to_wait_for)
        act_ec=$?
        fn_exec_cnt=$(( $fn_exec_cnt + 1 ))
    done

    log -deb "${fn_name} - Exit code: ${act_ec} equal to expected: ${exp_ec}"

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function returns state of the interface provided in parameter.
#
#   Uses and required ifconfig tool to be installed on device.
#   Provide adequate function in overrides otherwise.
#
# INPUT PARAMETER(S):
#   $1  interface name (required)
# RETURNS:
#   0   if interface state is UP, non zero otherwise.
# USAGE EXAMPLE(S):
#   interface_is_up eth0
###############################################################################
interface_is_up()
{
    fn_name="unit_lib:interface_is_up"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1

    ifconfig "$if_name" 2>/dev/null | grep Metric | grep -q UP
    return $?
}

###############################################################################
# DESCRIPTION:
#   Function drops interface
#
#   Uses and required ifconfig tool to be installed on device.
#   Provide adequate function in overrides otherwise.
#
# INPUT PARAMETER(S):
#   $1  interface name (required)
# RETURNS:
#   0   if interface was dropped, non zero otherwise.
# USAGE EXAMPLE(S):
#   interface_bring_down eth0
###############################################################################
interface_bring_down()
{
    fn_name="unit_lib:interface_bring_down"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1
    ifconfig "$if_name" down
    return $?
}

###############################################################################
# DESCRIPTION:
#   Function brings up interface
#
#   Uses and required ifconfig tool to be installed on device.
#   Provide adequate function in overrides otherwise.
#
# INPUT PARAMETER(S):
#   $1  interface name (required)
# RETURNS:
#   0   if interface was bringed up, non zero otherwise.
# USAGE EXAMPLE(S):
#   interface_bring_up eth0
###############################################################################
interface_bring_up()
{
    fn_name="unit_lib:interface_bring_up"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1
    ifconfig "$if_name" up
    return $?
}

###############################################################################
# DESCRIPTION:
#   Function returns IP address of interface provided in parameter.
#
#   Uses and required ifconfig tool to be insalled on device.
#   Provide adequate function in overrides otherwise.
#
# INPUT PARAMETER(S):
#   $1  interface name (required)
# RETURNS:
#   IP address of an interface
# USAGE EXAMPLE(S):
#   interface_ip_address eth0
###############################################################################
interface_ip_address()
{
    fn_name="unit_lib:interface_ip_address"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1

    ifconfig "$if_name" | tr -s ' :' '@' | grep -e '^@inet@' | cut -d '@' -f 4
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
# NOTE:
#   Awaits removal.
###############################################################################
check_restore_management_access()
{
    fn_name="unit_lib:check_restore_management_access"
    log -deb "$fn_name - Checking and restoring needed management access"
    mng_iface=${MGMT_IFACE:-eth0}
    interface_is_up "${mng_iface}"
    if [ "$?" = 0 ]; then
        log -deb "$fn_name - Interface ${mng_iface} is UP"
    else
        log -deb "$fn_name - Interface ${mng_iface} is DOWN, bringing it UP"
        wait_for_function_response 0 "ifconfig ${mng_iface} up" "${MGMT_IFACE_UP_TIMEOUT}" &&
            log -deb "$fn_name - Interface ${mng_iface} brought UP" ||
            log -err "FAIL: Could not bring up interface ${mng_iface}" -l "$fn_name" -ds
    fi

    interface_is_up "${mng_iface}.4"
    if [ "$?" = 0 ]; then
        log -deb "$fn_name - Interface ${mng_iface}.4 is UP"
    else
        log -deb "$fn_name - Interface ${mng_iface}.4 is DOWN, bringing it UP"
        ifconfig "${mng_iface}.4" up &&
            log -deb "$fn_name - Interface ${mng_iface}.4 brought UP" ||
            log -deb "$fn_name - Failed to bring up interface ${mng_iface}.4, checking udhcpc"
    fi

    eth_04_address=$(interface_ip_address "${mng_iface}.4")
    if [ -z "$eth_04_address" ]; then
        log -deb "$fn_name - Interface ${mng_iface}.4 has no address, setting udhcpc"
        log -deb "$fn_name - Running force address renew for ${mng_iface}.4"
        no_address=1
        while [ "$no_address" = 1 ]; do
            log -deb "$fn_name - Killing old ${mng_iface}.4 udhcpc pids"
            dhcpcd_pids=$(pgrep -f "/sbin/udhcpc .* ${mng_iface}.4")
            # shellcheck disable=SC2086
            kill $dhcpcd_pids &&
                log -deb "$fn_name - ${mng_iface}.4 udhcpc pids killed" ||
                log -deb "$fn_name - No ${mng_iface}.4 udhcpc pid to kill"
            log -deb "$fn_name - Starting udhcpc on ${mng_iface}.4"
            /sbin/udhcpc -f -S -i "${mng_iface}.4" -C -o -O subnet &>/dev/null &
            log -deb "$fn_name - Waiting for ${mng_iface}.4 address"
            wait_for_function_response notempty "interface_ip_address ${mng_iface}.4" "${MGMT_CONN_TIMEOUT}" &&
                log -deb "$fn_name - ${mng_iface}.4 address valid" && break ||
                log -deb "$fn_name - Failed to set ${mng_iface}.4 address, repeating"
        done
    else
        log -deb "$fn_name - Interface ${mng_iface}.4 address is $eth_04_address"
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
    fn_name="unit_lib:print_tables"
    NARGS_MIN=1
    [ $# -ge ${NARGS_MIN} ] ||
        raise "${fn_name} requires at least ${NARGS_MIN} input argument(s), $# given" -arg

    for table in "$@"
    do
        log -deb "$fn_name - OVSDB table - $table:"
        ${OVSH} s "$table"
    done

    return 0
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
#   $1  bridge name (required)
#   $2  HW address of bridge (required)
#   $3  MTU (optional)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   add_ovs_bridge br-home ab.34.cd.78.90.ef
###############################################################################
add_ovs_bridge()
{
    fn_name="unit_lib:add_ovs_bridge"
    NARGS_MIN=2
    NARGS_MAX=3
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "${fn_name} requires ${NARGS_MIN}-${NARGS_MAX} input arguments" -arg
    bridge_name=$1
    hwaddr=$2
    bridge_mtu=$3

    if [ -z "${bridge_name}" ]; then
        raise "FAIL: First input argument 'bridge_name' is empty" -l "${fn_name}" -arg
    fi
    log -deb "${fn_name} - Add bridge ${bridge_name}"
    ovs-vsctl br-exists "${bridge_name}"
    if [ $? = 2 ]; then
        ovs-vsctl add-br "${bridge_name}" &&
            log -deb "${fn_name} - Success: ovs-vsctl add-br ${bridge_name}" ||
            raise "FAIL: Could not add bridge ${bridge_name} to ovs-vsctl" -l "${fn_name}" -ds
    else
        log -deb "${fn_name} - Bridge ${bridge_name} already exists"
    fi

    # Set hwaddr if provided
    if [ -z "${hwaddr}" ]; then
        return 0
    else
        log -deb "${fn_name} - Set bridge hwaddr ${hwaddr}"
        ovs-vsctl set bridge "${bridge_name}" other-config:hwaddr="${hwaddr}" &&
            log -deb "${fn_name} - Success: set bridge hwaddr" ||
            raise "FAIL: Could not set hwaddr to bridge ${bridge_name}" -l "${fn_name}" -ds
    fi

    # Set mtu if provided
    if [ -z "${bridge_mtu}" ]; then
        return 0
    else
        log -deb "${fn_name} - Set bridge mtu ${bridge_mtu}"
        ovs-vsctl set int "${bridge_name}" mtu_request="${bridge_mtu}" &&
            log -deb "${fn_name} - Success: set bridge mtu" ||
            raise "FAIL: Could not set mtu to bridge ${bridge_name}" -l "${fn_name}" -ds
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
#   $1  bridge name (required)
#   $2  interface name (required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   add_bridge_interface br-wan eth0
###############################################################################
add_bridge_interface()
{
    fn_name="unit_lib:add_bridge_interface"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    br_name=$1
    br_if_name=$2

    log -deb "$fn_name - Adding $br_name - $br_if_name"

    ovs-vsctl br-exists "$br_name"
    if [ "$?" = 2 ]; then
        ovs-vsctl add-br "$br_name" &&
            log -deb "$fn_name - Success: ovs-vsctl add-br $br_name" ||
            raise "Failed: ovs-vsctl add-br $br_name" -l "$fn_name" -ds
    else
        log -deb "$fn_name - Bridge $br_name already exists"
        return 0
    fi

    log -deb "$fn_name - Linking $br_name - $br_if_name - $br_mac"
    mac_if=$(mac_get "$br_if_name") &&
        log -deb "$fn_name - Success: mac_get $br_if_name" ||
        raise "FAIL: Could not get interface $br_if_name MAC address" -l "$fn_name" -ds

    if [ "$br_name" = "br-home" ]; then
        br_mac=$(printf "%02X:%s" $(( 0x${mac_if%%:*} | 0x2 )) "${mac_if#*:}")
    else
        br_mac=$mac_if
    fi

    ovs-vsctl set bridge "$br_name" other-config:hwaddr="$br_mac" &&
        log -deb "$fn_name - Success: ovs-vsctl set bridge $br_name other-config:hwaddr=$br_mac" ||
        raise "FAIL: Could not set to bridge $br_name other-config:hwaddr=$br_mac to ovs-vsctl" -l "$fn_name" -ds

    ovs-vsctl set int "$br_name" mtu_request=1500 &&
        log -deb "$fn_name - Success: ovs-vsctl set int $br_name mtu_request=1500" ||
        raise "FAIL: Could not set to bridge int $br_name mtu_request=1500" -l "$fn_name" -ds
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
#   $1  bridge name (required)
#   $2  port name (required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   add_bridge_port br-home patch-h2w
###############################################################################
add_bridge_port()
{
    fn_name="unit_lib:add_bridge_port"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    bridge_name=$1
    port_name=$2

    log -deb "$fn_name - Adding bridge port ${port_name} to ${bridge_name}"
    ovs-vsctl br-exists "${bridge_name}"
    if [ $? = 2 ]; then
        raise "FAIL: Bridge ${bridge_name} does not exist" -l "${fn_name}" -ds
    fi
    ovs-vsctl list-ports "${bridge_name}" | grep -qwF "${port_name}"
    if [ $? = 0 ]; then
        log -deb "$fn_name - Port ${port_name} already in bridge ${bridge_name}"
        return 0
    else
        ovs-vsctl add-port "${bridge_name}" "${port_name}" &&
            log -deb "$fn_name - Success: ovs-vsctl add-port ${bridge_name} ${port_name}" ||
            raise "FAIL: Could not add bridge port ${port_name} to bridge ${bridge_name}" -l $fn_name -ds
    fi
}

###############################################################################
# DESCRIPTION:
#   Function removes bridge from ovs.
#   Function uses ovs-vsctl command, different from native Linux bridge.
#   Raises an exception if bridge cannot be deleted.
# INPUT PARAMETER(S):
#   $1  bridge name (required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   remove_bridge_interface br-wan
###############################################################################
remove_bridge_interface()
{
    fn_name="unit_lib:remove_bridge_interface"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    br_name=$1

    ovs-vsctl del-br "$br_name" &&
        log -deb "$fn_name - Success: ovs-vsctl del-br $br_name" ||
        raise "FAIL: Could not remove bridge $br_name from ovs-vsctl" -l "$fn_name" -ds
}

###############################################################################
# DESCRIPTION:
#   Function removes port from bridge in ovs switch.
#   Function uses ovs-vsctl command, different from native Linux bridge.
#   Raises an exception if port cannot be deleted.
# INPUT PARAMETER(S):
#   $1  bridge name (required)
#   $2  port name (required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   remove_port_from_bridge br-wan br-wan.tdns
#   remove_port_from_bridge br-wan br-wan.thttp
###############################################################################
remove_port_from_bridge()
{
    fn_name="unit_lib:remove_port_from_bridge"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s)" -arg
    br_name=$1
    port_name=$2

    ovs-vsctl del-port "$br_name" "$port_name" &&
        log -deb "$fn_name - Success: ovs-vsctl del-port $br_name $port_name" ||
        raise "Failed: ovs-vsctl del-port $br_name $port_name" -l "$fn_name" -ds
}

###############################################################################
# DESCRIPTION:
#   Functions sets interface to patch port.
#   Function uses ovs-vsctl command, different from native Linux bridge.
#   Raises an exception if patch cannot be set.
# INPUT PARAMETER(S):
#   $1  interface name (not used)
#   $2  patch name (required)
#   $3  peer name (required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   set_interface_patch patch-h2w patch-w2h patch-h2w
#   set_interface_patch patch-w2h patch-h2w patch-w2h
###############################################################################
set_interface_patch()
{
    fn_name="unit_lib:set_interface_patch"
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1
    patch=$2
    peer=$3

    ovs-vsctl set interface "$patch" type=patch &&
        log -deb "$fn_name - Success: ovs-vsctl set interface $patch type=patch" ||
        raise "FAIL: Could not set interface patch: ovs-vsctl set interface $patch type=patch" -l "$fn_name" -ds

    ovs-vsctl set interface "$patch" options:peer="$peer" &&
        log -deb "$fn_name - Success: ovs-vsctl set interface $patch options:peer=$peer" ||
        raise "FAIL: Could not set interface patch peer: ovs-vsctl set interface $patch options:peer=$peer" -l "$fn_name" -ds
}

###############################################################################
# DESCRIPTION:
#   Functions sets interface option.
#   Function uses ovs-vsctl command, different from native Linux bridge.
#   Raises an exception on failure.
# INPUT PARAMETER(S):
#   $1  if_name (required)
#   $2  option (required)
#   $3  value (required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   set_ovs_vsctl_interface_option br-home.tdns type internal
#   set_ovs_vsctl_interface_option br-home.tdns ofport_request 3001
###############################################################################
set_ovs_vsctl_interface_option()
{
    fn_name="unit_lib:set_ovs_vsctl_interface_option"
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1
    option=$2
    value=$3

    ovs-vsctl set interface "${if_name}" "${option}"="${value}" &&
        log -deb "$fn_name - Success: ovs-vsctl set interface ${if_name} ${option}=${value}" ||
        raise "FAIL: Could not set interface option: set interface ${if_name} ${option}=${value}" -l "$fn_name" -ds
}

###############################################################################
# DESCRIPTION:
#   Function checks if port is in bridge.
#   Function uses ovs-vsctl command, different from native Linux bridge.
# INPUT PARAMETER(S):
#   $1  port name (required)
#   $2  bridge name (required)
# RETURNS:
#   0   Port in bridge.
#   1   Port is not in bridge.
# USAGE EXAMPLE(S):
#   check_if_port_in_bridge eth0 br-wan
###############################################################################
check_if_port_in_bridge()
{
    fn_name="unit_lib:check_if_port_in_bridge"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    port_name=$1
    br_name=$2

    ovs-vsctl list-ports "$br_name" | grep -q "$port_name"
    if [ "$?" = 0 ]; then
        log -deb "$fn_name - Port $port_name exists on bridge $br_name"
        return 0
    else
        log -deb "$fn_name - Port $port_name does not exist on bridge $br_name"
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
#   $1  cloud IP (optional, defaults to 192.168.200.1)
#   $2  port (optional, defaults to 443)
#   $3  certificates folder (optional, defaults to $FUT_TOPDIR/utility/files)
#   $4  certificate file (optional, defaults to fut_ca.pem)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   connect_to_fut_cloud
#   connect_to_fut_cloud 192.168.200.1 65000 fut-base/utility/files fut_ca.pem
###############################################################################
connect_to_fut_cloud()
{
    fn_name="unit_lib:connect_to_fut_cloud"
    target=${1:-"192.168.200.1"}
    port=${2:-"65000"}
    cert_dir=${3:-"$FUT_TOPDIR/shell/tools/device/files"}
    ca_fname=${4:-"fut_ca.pem"}
    inactivity_probe=30000

    log -deb "$fn_name - Configure certificates, check if file exists"
    test -f "$cert_dir/$ca_fname" ||
        raise "FAIL: File $cert_dir/$ca_fname not found" -l "$fn_name" -ds

    update_ovsdb_entry SSL -u ca_cert "$cert_dir/$ca_fname"
        log -deb "$fn_name - SSL ca_cert set to $cert_dir/$ca_fname" ||
        raise "FAIL: SSL ca_cert not set to $cert_dir/$ca_fname" -l "$fn_name" -ds

    # Remove redirector, to not interfere with the flow
    update_ovsdb_entry AWLAN_Node -u redirector_addr ''
        log -deb "$fn_name - AWLAN_Node redirector_addr set to ''" ||
        raise "FAIL: AWLAN_Node::redirector_addr not set to ''" -l "$fn_name" -ds

    # Remove manager_addr, to not interfere with the flow
    update_ovsdb_entry AWLAN_Node -u manager_addr ''
        log -deb "$fn_name - AWLAN_Node manager_addr set to ''" ||
        raise "FAIL: AWLAN_Node::manager_addr not set to ''" -l "$fn_name" -ds

    # Inactivity probe sets the timing of keepalive packets
    update_ovsdb_entry Manager -u inactivity_probe $inactivity_probe &&
        log -deb "$fn_name - Manager inactivity_probe set to $inactivity_probe" ||
        raise "FAIL: Manager::inactivity_probe not set to $inactivity_probe" -l "$fn_name" -ds

    # Minimize AWLAN_Node::min_backoff timer (8s is ovsdb-server retry timeout)
    update_ovsdb_entry AWLAN_Node -u min_backoff "8" &&
        log -deb "$fn_name - AWLAN_Node min_backof set to 8" ||
        raise "FAIL: AWLAN_Node::min_backoff not set to 8" -l "$fn_name" -ds

    # Minimize AWLAN_Node::max_backoff timer
    update_ovsdb_entry AWLAN_Node -u max_backoff "9" &&
        log -deb "$fn_name - AWLAN_Node max_backof set to 9" ||
        raise "FAIL: AWLAN_Node::max_backoff not set to 9" -l "$fn_name" -ds

    # Clear Manager::target before starting
    update_ovsdb_entry Manager -u target ''
        log -deb "$fn_name - Manager target set to ''" ||
        raise "FAIL: Manager::target not set to ''" -l "$fn_name" -ds

    # Wait for CM to settle
    sleep 2

    # AWLAN_Node::manager_addr is the controller address, provided by redirector
    update_ovsdb_entry AWLAN_Node -u manager_addr "ssl:$target:$port" &&
        log -deb "$fn_name - AWLAN_Node manager_addr set to ssl:$target:$port" ||
        raise "FAIL: AWLAN_Node::manager_addr not set to ssl:$target:$port" -l "$fn_name" -ds

    log -deb "$fn_name - Waiting for FUT cloud status to go to ACTIVE"
    wait_cloud_state ACTIVE &&
        log -deb "$fn_name - Manager::status is set to ACTIVE. Connected to FUT cloud." ||
        raise "FAIL: Manager::status is not ACTIVE. Not connected to FUT cloud." -l "$fn_name" -ds
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
#   Cloud statuses are:
#       ACTIVE          device is connected to the Cloud.
#       BACKOFF         device could not connect to the Cloud, will retry.
#       CONNECTING      connecting to the Cloud in progress.
#       DISCONNECTED    device is disconnected from the Cloud.
#   Raises an exception on fail.
# INPUT PARAMETER(S):
#   $1  desired Cloud state (required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   wait_cloud_state ACTIVE
###############################################################################
wait_cloud_state()
{
    fn_name="unit_lib:wait_cloud_state"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wait_for_cloud_status=$1

    log -deb "$fn_name - Waiting for the FUT cloud status $wait_for_cloud_status"
    wait_for_function_response 0 "${OVSH} s Manager status -r | grep -q \"$wait_for_cloud_status\"" &&
        log -deb "$fn_name - FUT cloud status is $wait_for_cloud_status" ||
        raise "FAIL: FUT cloud status is not $wait_for_cloud_status" -l "$fn_name" -ow
    print_tables Manager
}

####################### FUT CLOUD SECTION - STOP ##############################

####################### FUT CMD SECTION - START ###############################

###############################################################################
# DESCRIPTION:
#   Function checks kconfig value from ${OPENSYNC_ROOTDIR}/etc/kconfig
#       if it matches given value
#   Raises an exception if kconfig field is missing from given path.
# INPUT PARAMETER(S):
#   $1  kconfig option name (required)
#   $2  kconfig option value to check (required)
# RETURNS:
#   0 - value matches to the one in kconfig path
#   1 - value does not match to the one in kconfig path
# USAGE EXAMPLE(S):
#   check_kconfig_option "CONFIG_PM_ENABLE_LED" "y"
###############################################################################
check_kconfig_option()
{
    fn_name="unit_lib:check_kconfig_option"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    kconfig_option_name=${1}
    kconfig_option_value=${2}

    kconfig_path="${OPENSYNC_ROOTDIR}/etc/kconfig"
    if ! [ -f "${kconfig_path}" ]; then
        raise "kconfig file is not present on ${kconfig_path}" -l "unit_lib:check_kconfig_option" -ds
    fi
    cat "${kconfig_path}" | grep -q "${kconfig_option_name}=${kconfig_option_value}"
    return $?
}

###############################################################################
# DESCRIPTION:
#   Function echoes kconfig value from ${OPENSYNC_ROOTDIR}/etc/kconfig which matches value name
#   Raises an exception if kconfig field is missing from given path.
# INPUT PARAMETER(S):
#   $1  kconfig option name (required)
# RETURNS:
#   None.
#   See description
# USAGE EXAMPLE(S):
#   get_kconfig_option_value "CONFIG_PM_ENABLE_LED" <- return y or n
###############################################################################
get_kconfig_option_value()
{
    fn_name="unit_lib:check_kconfig_option"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    kconfig_option_name=${1}

    kconfig_path="${OPENSYNC_ROOTDIR}/etc/kconfig"
    if ! [ -f "${kconfig_path}" ]; then
        raise "kconfig file is not present on ${kconfig_path}" -l "unit_lib:check_kconfig_option" -ds
    fi
    cat "${kconfig_path}" | grep "${kconfig_option_name}" |  cut -d "=" -f2
}

####################### FUT CMD SECTION - STOP ################################
