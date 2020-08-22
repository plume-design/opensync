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
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source "${FUT_TOPDIR}/shell/config/default_shell.sh"
fi
source "${FUT_TOPDIR}/shell/lib/base_lib.sh"

############################################ INFORMATION SECTION - START ###############################################
#
#   Common library of shared functions, used globally
#
############################################ INFORMATION SECTION - STOP ################################################
lib_name="unit_lib"

############################################ UTILITY SECTION - START ###################################################

# Returns the filename of the script manipulating OpenSync managers
get_managers_script()
{
    echo "/etc/init.d/opensync"
}

get_process_cmd()
{
    echo "ps -w"
}

# Get the MAC address of an interface
mac_get()
{
    ifconfig "$1" | grep -o -E '([A-F0-9]{2}:){5}[A-F0-9]{2}'
}

get_radio_mac_from_ovsdb()
{
    # Examples of use:
    #   get_radio_mac_from_ovsdb "freq_band==5GL"
    #   get_radio_mac_from_ovsdb "if_name==wifi1"
    #   get_radio_mac_from_ovsdb "channel==44"
    local where_clause=$1
    # No logging, this function echoes the requested value to caller!
    ${OVSH} s Wifi_Radio_State -w ${where_clause} mac -r
    return $?
}

tail_logs_for_match()
{
    match=$(timeout -t "$DEFAULT_WAIT_TIME" -s SIGKILL tail -n150 -f /var/log/messages |  tr -s ' ' | { sed "/$match_pattern_for_log_inspecting/ q"; } | tail -1 | grep "$match_pattern_for_log_inspecting")
    echo $match
}

start_udhcpc()
{
    fn_name="unit_lib:start_udhcpc"
    if_name=$1
    should_get_address=${2:-false}

    log -deb "$fn_name - Starting udhcpc on $if_name"

    ps_out=$(pgrep "/sbin/udhcpc.*$if_name")
    if [ $? -eq 0 ]; then
        kill $ps_out && log -deb "$fn_name - Old udhcpc pid killed for $if_name"
    fi

    /sbin/udhcpc -i "$if_name" -f -p /var/run/udhcpc-"$if_name".pid -s ${OPENSYNC_ROOTDIR}/bin/udhcpc.sh -t 60 -T 1 -S --no-default-options &>/dev/null &

    if [ "$should_get_address" = "true" ]; then
        wait_for_function_response 'notempty' "interface_ip_address $if_name" &&
            log "$fn_name - DHCPC provided address to $if_name" ||
            raise "DHCPC didn't provide address to $if_name" -l "$fn_name" -ds
    fi

    return 0
}

############################################ UTILITY SECTION - STOP ####################################################


############################################ PROCESS SECTION - START ###################################################

# deprecated in favor of "get_pids", which provides all pids matching expression
get_pid()
{
    PID=$($(get_process_cmd) | grep -e "$1" | grep -v 'grep' | awk '{ print $1 }')
    echo "$PID"
}

get_pids()
{
    PID="$($(get_process_cmd) | grep -e ${1} | grep -v 'grep' | awk '{ print $1 }')"
    echo "${PID}"
}

get_pid_mem()
{
    PID_MEM=$($(get_process_cmd) | grep -e "$1" | grep -v 'grep' | awk '{ printf $3 }')
    echo "$PID_MEM"
}

check_pid_file()
{
    fn_name="unit_lib:check_pid_file"
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

check_pid_udhcp()
{
    local fn_name="unit_lib:check_pid_udhcp"
    local if_name="${1}"
    PID=$($(get_process_cmd) | grep -e udhcpc | grep -e ${if_name} | grep -v 'grep' | awk '{ print $1 }')
    if [ -z "$PID" ]; then
        log -deb "${fn_name} - DHCP client not running on ${if_name}"
        return 1
    else
        log -deb "${fn_name} - DHCP client running on ${if_name}, PID=${PID}"
        return 0
    fi
}

# requires binary "pidof"
killall_process_by_name ()
{
    local PROCESS_PID="$(pidof ${1})"
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
                log -deb "lib/unit_lib: killall_process_by_name - killed process:${P} with signal:${S}"
            else
                log -deb "lib/unit_lib: killall_process_by_name - could not kill process:${P}"
            fi
        done
    fi
}

############################################ PROCESS SECTION - STOP ####################################################


############################################ SETUP SECTION - START ##################################################

device_init()
{
    disable_watchdog
    stop_managers
    stop_healthcheck
    return $?
}

############################################ SETUP SECTION - START ##################################################


############################################ WATCHDOG SECTION - START ##################################################

disable_watchdog()
{
    local fn_name="unit_lib:disable_watchdog"
    log -deb "$fn_name - Disabling watchdog."
    log -deb "$fn_name - This is a stub function. Override implementation needed for each model."
    return 0
}

############################################ WATCHDOG SECTION - STOP ###################################################


############################################ OpenSwitch SECTION - START ################################################

start_openswitch()
{
    fn_name="unit_lib:start_openswitch"
    log -deb "$fn_name - Starting Open vSwitch"

    ovs_run=$($(get_process_cmd)  | grep -v "grep" | grep "ovs-vswitchd")

    if [ "$?" -eq 0 ]; then
        log -deb "$fn_name - Open vSwitch already running"
        return 0
    fi

    /etc/init.d/openvswitch start || raise "Issue during Open vSwitch start" -l "$fn_name" -ds

    wait_for_function_response 0 "pidof ovs-vswitchd" &&
        log -deb "$fn_name - ovs-vswitchd running" ||
        raise "Couldn't start ovs-vswitchd" -l "$fn_name" -ds

    wait_for_function_response 0 "pidof ovsdb-server" &&
        log -deb "$fn_name - ovsdb-server running" ||
        raise "Couldn't start ovsdb-server" -l "$fn_name" -ds

    sleep 1
}

stop_healthcheck()
{
    fn_name="unit_lib:stop_healthcheck"
    if [ -n "$(get_pid "healthcheck")" ]; then
        log -deb "$fn_name - Disabling healthcheck."
        /etc/init.d/healthcheck stop || true

        log -deb "$fn_name - Check if healthcheck is disabled"
        wait_for_function_response 1 "$(get_process_cmd) | grep -e 'healthcheck' | grep -v 'grep'"
        if [ "$?" -ne 0 ]; then
            log -deb "$fn_name - Healthcheck is NOT disabled ! PID: $(get_pid "healthcheck")"
            return 1
        else
            log -deb "$fn_name - Healthcheck is disabled."
        fi
    else
        log -deb "$fn_name - Healthcheck is already disabled."
    fi
    return 0
}

############################################ OpenSwitch SECTION - STOP #################################################


############################################ RESOURCE SECTION - START ##################################################

############################################ RESOURCE SECTION - STOP ###################################################


############################################ MANAGERS SECTION - START ##################################################

start_managers()
{
    fn_name="unit_lib:start_managers"
    log -deb "$fn_name Starting OpenSync managers"
    MANAGER_SCRIPT=$(get_managers_script)
    ret=$($MANAGER_SCRIPT start)
    # Make sure to define return value on success or failure
    if [ $? -ne 1 ]; then
        raise "Issue during OpenSync manager start" -l "$fn_name" -ds
    else
        log "$fn_name OpenSync managers started"
    fi

    # Check dm slave PID
    PID=$($(get_process_cmd) | grep -e ${OPENSYNC_ROOTDIR}/bin/dm | grep -v 'grep' | grep -v slave | awk '{ print $1 }')
    if [ -z "$PID" ]; then
        raise "Issue during manager start, dm slave not running" -l "$fn_name" -ds
    else
        log "$fn_name dm slave PID = $PID"
    fi

    # Check dm master PID
    PID=$($(get_process_cmd) | grep -e ${OPENSYNC_ROOTDIR}/bin/dm | grep -v 'grep' | grep -v master | awk '{ print $1 }')
    if [ -z "$PID" ]; then
        raise "Issue during manager start, dm master not running" -l "$fn_name" -ds
    else
        log "$fn_name dm master PID = $PID"
    fi

    return 0
}

restart_managers()
{
    fn_name="unit_lib:restart_managers"
    log -deb "$fn_name - Restarting OpenSync managers"
    MANAGER_SCRIPT=$(get_managers_script)
    ret=$($MANAGER_SCRIPT restart)
    ec=$?
    log -deb "$fn_name - manager restart exit code ${ec}"
    return $ec
}

stop_managers()
{
    fn_name="unit_lib:stop_managers"
    log -deb "$fn_name - Stopping OpenSync managers"
    MANAGER_SCRIPT=$(get_managers_script)
    $MANAGER_SCRIPT stop ||
        raise "Issue during OpenSync manager stop" -l "$fn_name" -ds
}

disable_managers()
{
    fn_name="unit_lib:disable_managers"
    log -deb "$fn_name - Stopping OpenSync managers"
    MANAGER_SCRIPT=$(get_managers_script)
    $MANAGER_SCRIPT stop ||
        raise "Issue during OpenSync manager stop" -l "$fn_name" -ds

    sleep 1
    PID=$(pidof dm) && raise "dm not running" -l "$fn_name" -ds
}

start_specific_manager()
{
    fn_name="unit_lib:start_specific_manager"
    option=$2

    manager="${OPENSYNC_ROOTDIR}/bin/$1"

    # Check if executable
    if [ ! -x "$manager" ]; then
        log -deb "$fn_name - Manager $manager does not exist or is not executable"
        return 1
    fi

    # Start manager
    log -deb "$fn_name - Starting $manager $option" | tr a-z A-Z
    $manager $option >/dev/null 2>&1 &
    sleep 1
}

restart_specific_manager()
{
    fn_name="unit_lib:restart_specific_manager"
    # Sanitize input
    if [ $# -eq 1 ]; then
        manager="${OPENSYNC_ROOTDIR}/bin/$1"
    else
        log -deb "$fn_name - Provide exactly one input argument"
        return 1
    fi

     # Check if executable
    if [ ! -x "$manager" ]; then
        log -deb "$fn_name - Manager $manager does not exist or is not executable"
        return 1
    fi

    # Start manager
    log -deb "$fn_name - Starting $manager" | tr a-z A-Z
    killall "$1"
    sleep 1
    $manager >/dev/null 2>&1 &
    sleep 1
}

set_manager_log()
{
    fn_name="unit_lib:set_manager_log"
    log -deb "$fn_name - Adding $1 to AW_Debug with severity $2"
    insert_ovsdb_entry AW_Debug -i name "$1" -i log_severity "$2" ||
        raise "{AW_Debug -> insert}" -l "$fn_name" -oe
}

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
                raise "Test environment for $manager failed" -l "$fn_name" -tc
        fi
    done
}

# Prevent CM fatal state: prevent restarting managers
cm_disable_fatal_state()
{
    fn_name="unit_lib:cm_disable_fatal_state"
    log -deb "$fn_name - Disabling CM manager restart procedure"
    if [ ! -d /opt/tb ]; then
        mkdir -p /opt/tb/
    fi
    touch /opt/tb/cm-disable-fatal
}

cm_enable_fatal_state()
{
    fn_name="unit_lib:cm_enable_fatal_state"
    log -deb "$fn_name - Enabling CM manager restart procedure"
    rm -f /opt/tb/cm-disable-fatal
}

############################################ MANAGERS SECTION - STOP ###################################################


############################################ OVSDB SECTION - START #####################################################

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
        esac
    done

    raw_field_value=$(${OVSH} s "$ovsdb_table" $conditions_string "$ovsdb_field" -r) || return 1

    echo "$raw_field_value" | grep -q '"uuid"'
    if [ "$raw" = "false" ] && [ "$?" -eq 0 ]; then
        value=$(echo "$raw_field_value" | cut -d ',' -f 2 | cut -d '"' -f 2)
    else
        value="$raw_field_value"
    fi

    echo "$value"
}

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
        esac
    done

    check_cmd="${OVSH} s $ovsdb_table $conditions_string"

    log "$fn_name - Checking if entry exists \n$check_cmd"
    eval "$check_cmd" && return 0 || return 1
}

empty_ovsdb_table()
{
    fn_name="unit_lib:empty_ovsdb_table"
    log -deb "$fn_name - Clearing $1 table"
    ${OVSH} d "$1" || raise "{$1 -> delete}" -l "$fn_name" -oe
}

wait_for_empty_ovsdb_table()
{
    fn_name="unit_lib:wait_for_empty_ovsdb_table"
    ovsdb_table=$1
    empty_timeout=${2:-$DEFAULT_WAIT_TIME}
    wait_time=0

    log -deb "$fn_name - Waiting for table $1 deletion"

    while true ; do
        log -deb "$fn_name - Select $ovsdb_table try $wait_time"
        table_select=$(${OVSH} s "$ovsdb_table") || true

        if [ -z "$table_select" ]; then
            log -deb "$fn_name - Table $ovsdb_table is empty!"
            break
        fi

        if [ $wait_time -gt $empty_timeout ]; then
            raise "{$1 -> delete}" -l "$fn_name" -oe
        fi

        wait_time=$((wait_time+1))
        sleep 1
    done

    log -deb "$fn_name - Table $ovsdb_table successfully deleted!"
}

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
        esac
    done

    log -deb "$info_string"

    select_entry_command="$ovsdb_table $conditions_string"
    wait_time=0

    while true ; do
        entry_select=$(${OVSH} s $select_entry_command) || true

        if [ -z "$entry_select" ]; then
            log -deb "$fn_name - Entry deleted"
            break
        fi

        if [ $wait_time -gt $DEFAULT_WAIT_TIME ]; then
            $select_entry_command
            raise "{$ovsdb_table -> entry_remove}" -l "$fn_name" -ow
        fi

        wait_time=$((wait_time+1))
        sleep 1
    done

    return 0
}

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
        esac
    done

    entry_command="${OVSH} u $ovsdb_table $conditions_string $update_string"
    log -deb "$fn_name - Executing update command\n$entry_command"

    $($entry_command) || $(return 1)

    if [ "$?" -eq 0 ]; then
        log -deb "$fn_name - Entry updated"
        log -deb "${OVSH} s $ovsdb_table $conditions_string"
        ${OVSH} s "$ovsdb_table" $conditions_string || log -deb "$fn_name - Failed to print entry"
    else
        ${OVSH} s "$ovsdb_table" || log -deb "$fn_name - Failed to print table $ovsdb_table"

        if [ $force_insert -eq 0 ]; then
            log -deb "$fn_name - Force entry, not failing!"
        else
            raise "{$ovsdb_table -> update}" -l "$fn_name" -oe
        fi
    fi

    return 0
}

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
        esac
    done

    remove_command="${OVSH} d $ovsdb_table $conditions_string $update_string"
    log -deb "$fn_name - $remove_command"

    $($remove_command) || $(return 1)

    if [ "$?" -eq 0 ]; then
        log -deb "$fn_name Entry removed"
    else
        print_tables "$ovsdb_table" ||
            log -deb "$fn_name - Failed to print table $ovsdb_table"
        raise "{$ovsdb_table -> entry_remove}" -l "$fn_name" -oe
    fi

    return 0
}

insert_ovsdb_entry()
{
    fn_name="unit_lib:insert_ovsdb_entry"
    ovsdb_table=$1
    shift
    conditions_string=""
    insert_string=""
    insert_method=":="

    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -m)
                insert_method="$1"
                shift
                ;;
            -i)
                insert_string="$insert_string $1$insert_method$2"
                shift 2
                insert_method=":="
                ;;
        esac
    done

    entry_command="${OVSH} i $ovsdb_table $insert_string"

    log -deb "$fn_name - Executing insert command\n$entry_command"

    $($entry_command) || $(return 1)

    if [ "$?" -eq 0 ]; then
        log -deb "$fn_name - Entry inserted"
        ${OVSH} s "$ovsdb_table"
    else
        ${OVSH} s "$ovsdb_table"
        raise "{$ovsdb_table -> insert}" -l "$fn_name" -oe
    fi

    return 0
}

insert_ovsdb_entry2()
{
    local fn_name="unit_lib:insert_ovsdb_entry2"
    local ovsdb_table=$1
    shift
    local insert_string=""

    while [ -n "${1}" ]; do
        option=${1}
        shift
        case "$option" in
            -i)
                insert_string="${insert_string} "${1}":="${2}
                shift 2
                ;;
        esac
    done

    entry_command="${OVSH} i $ovsdb_table "$insert_string
    log -deb "$fn_name - Executing ${entry_command}"
    ${entry_command}
    if [ $? -eq 0 ]; then
        log -deb "$fn_name - Success: entry inserted"
        ${OVSH} s "$ovsdb_table"
        return 0
    else
        ${OVSH} s "$ovsdb_table"
        raise "Failure: entry not inserted" -l "$fn_name" -oe
    fi
}

wait_ovsdb_entry()
{
    fn_name="unit_lib:wait_ovsdb_entry"
    ovsdb_table=$1
    shift
    conditions_string=""
    where_is_string=""
    exit_code=0
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
                exit_code=1
                ;;
            -f)
                ovsh_cmd=${OVSH_FAST}
                ;;
            -s)
                ovsh_cmd=${OVSH_SLOW}
        esac
    done

    wait_entry_command="$ovsh_cmd wait $ovsdb_table $conditions_string $where_is_string"
    wait_time=0

    log -deb "$fn_name - Waiting for entry: \n$wait_entry_command"

    $($wait_entry_command) || $(return 1)

    if [ "$?" -eq "$exit_code" ]; then
        log -deb "$fn_name - SUCCESS: $wait_entry_command"
        ${OVSH} s "$ovsdb_table" $conditions_string
        return 0
    else
        log -deb "$fn_name - FAIL: Table $ovsdb_table"
        ${OVSH} s "$ovsdb_table" || true
        log -deb "$fn_name - FAIL: $wait_entry_command"
        return 1
    fi
}

wait_for_function_response()
{
    fn_name="unit_lib:wait_for_function_response"
    wait_for_value="$1"
    function_to_wait_for="$2"
    wait_time=${3:-$DEFAULT_WAIT_TIME}
    return_val=1
    fuc_exec_time=0
    is_get_ovsdb_entry_value=1

    if [ "$wait_for_value" = 'empty' ]; then
        log -deb "$fn_name - Waiting for function $function_to_wait_for empty response"
    elif [ "$wait_for_value" = 'notempty' ]; then
        log -deb "$fn_name - Waiting for function $function_to_wait_for not empty response"
        echo "$function_to_wait_for" | grep -q "get_ovsdb_entry_value" && is_get_ovsdb_entry_value=0
    else
        log -deb "$fn_name - Waiting for function $function_to_wait_for exit code $wait_for_value"
    fi

    while true ; do
        log -deb "$fn_name - Executing: $function_to_wait_for"
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
            return_val=0
            break
        fi

        if [ $fuc_exec_time -gt $wait_time ]; then
            log -deb "$fn_name - Function $function_to_wait_for timed out"
            break
        fi

        fuc_exec_time=$((fuc_exec_time+1))
        sleep 1
    done

    return "$return_val"
}

wait_for_function_output()
{
    # Waits for expected output of function, not exit code
    # Possible input arguments:
    #   wait_for_value: "empty", "notempty", custom (required)
    #   function_to_wait_for: string with the function to wait for (required)
    #   retry_count: after how many iterations we stop checking? (optional, default=30)
    #   retry_sleep: time in seconds between function checks (optional, default=1)
    local fn_name="unit_lib:wait_for_function_output"
    local wait_for_value=$1
    local function_to_wait_for=$2
    local retry_count=${3:-$DEFAULT_WAIT_TIME}
    local retry_sleep=${4:-1}
    local fn_exec_cnt=0
    local is_get_ovsdb_entry_value=0
    [ $(echo "$function_to_wait_for" | grep -qwF "get_ovsdb_entry_value") ] && is_get_ovsdb_entry_value=1

    log -deb "$fn_name - Executing $function_to_wait_for, waiting for $wait_for_value response"
    while true ; do
        res=$($function_to_wait_for)
        if [ "$wait_for_value" = 'notempty' ]; then
            if [ $is_get_ovsdb_entry_value ]; then
                [ -n "$res" -a "$res" != '["set",[]]' -a "$res" != '["map",[]]' ] && return 0
            else
                [ -n "$res" ] && return 0
            fi
        elif [ "$wait_for_value" = 'empty' ]; then
            if [ $is_get_ovsdb_entry_value ]; then
                [ -z "$res" -o "$res" = '["set",[]]' -o "$res" = '["map",[]]' ] && return 0
            else
                [ -z "$res" ] && return 0
            fi
        else
            [ "$res" = "$wait_for_value" ] && return 0
        fi

        fn_exec_cnt=$(( $fn_exec_cnt + 1 ))
        log -deb "$fn_name - Function retry ${fn_exec_cnt} output: ${res}"
        if [ $fn_exec_cnt -ge $retry_count ]; then
            raise "Function $function_to_wait_for timed out" -l "${fn_name}"
        fi
        sleep ${retry_sleep}
    done
    return 1
}

wait_for_function_exitcode()
{
    # Waits for expected exit code, not stdout/stderr
    # Possible input arguments:
    #   exp_ec: expected exit code (required)
    #   function_to_wait_for: string with the function to wait for (required)
    #   retry_count: after how many iterations we stop checking? (optional, default=30)
    #   retry_sleep: time in seconds between function checks (optional, default=1)
    local fn_name="unit_lib:wait_for_function_exitcode"
    local exp_ec=$1
    local function_to_wait_for=$2
    local retry_count=${3:-$DEFAULT_WAIT_TIME}
    local retry_sleep=${4:-1}
    local fn_exec_cnt=1

    log -deb "${fn_name} - Executing $function_to_wait_for, waiting for exit  code ${exp_ec}"
    res=$($function_to_wait_for)
    local act_ec=$?
    while [ ${act_ec} -ne ${exp_ec} ]; do
        log -deb "${fn_name} - Retry ${fn_exec_cnt}, exit code: ${act_ec}, expecting: ${exp_ec}"
        if [ ${fn_exec_cnt} -ge ${retry_count} ]; then
            raise "Function ${function_to_wait_for} timed out" -l "${fn_name}"
        fi
        sleep ${retry_sleep}
        res=$($function_to_wait_for)
        act_ec=$?
        fn_exec_cnt=$(( $fn_exec_cnt + 1 ))
    done
    log -deb "${fn_name} - Exit code: ${act_ec} equal to expected: ${exp_ec}"
    return 0
}

monitor_ovsdb_field_for_changes()
{
    fn_name="unit_lib:monitor_ovsdb_field_for_changes"
    ovsdb_table=$1
    shift
    info_string="$fn_name - Monitoring table $ovsdb_table Column -"
    conditions_string=""
    field_to_monitor=""
    timer=0

    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -w)
                conditions_string="$conditions_string -w $1==$2"
                shift 2
                ;;
            -m)
                field_to_monitor="$1"
                info_string="$info_string $1"
                shift 1
                ;;
        esac
    done

    log -deb "$info_string"

    echo "${OVSH} s $ovsdb_table $conditions_string $field_to_monitor -r"
    field_to_monitor_value=$(${OVSH} s "$ovsdb_table" $conditions_string "$field_to_monitor" -r)

    log -deb "$fn_name - Field $field_to_monitor value is $field_to_monitor_value"

    while [ "$timer" -lt "$DEFAULT_WAIT_TIME" ]; do
        (${OVSH} wait "$ovsdb_table" $conditions_string -n "$field_to_monitor":="$field_to_monitor_value" 2>&1)
        if [ "$?" -eq 0 ]; then
            field_value=$(${OVSH} s "$ovsdb_table" $conditions_string "$field_to_monitor" -r)
            log -deb "$fn_name - Field $field_to_monitor changed from $field_to_monitor_value to $field_value"
            field_to_monitor_value=$field_value
        fi
    done
}

interface_is_up()
{
    ifconfig "$1" 2>/dev/null | grep Metric | grep -q UP
    return $?
}

interface_ip_address()
{
    ifconfig "$1" | tr -s ' :' '@' | grep -e '^@inet@' | cut -d '@' -f 4
}

check_restore_management_access()
{
    fn_name="unit_lib:check_restore_managment_access"
    log -deb "$fn_name - Checking and restoring if needed management access"
    interface_is_up eth0
    if [ "$?" = 0 ]; then
        log -deb "$fn_name - Interface eth0 is UP"
    else
        log -deb "$fn_name - Interface eth0 is DOWN, bringing it UP"
        wait_for_function_response 0 "ifconfig eth0 up" &&
            log -deb "$fn_name - Interface eth0 brought UP" ||
            raise "Failed to bring up interface eth0" -l "$fn_name" -ds
    fi

    interface_is_up eth0.4
    if [ "$?" = 0 ]; then
        log -deb "$fn_name - Interface eth0.4 is UP"
    else
        log -deb "$fn_name - Interface eth0.4 is DOWN, bringing it UP"
        ifconfig eth0.4 up &&
            log -deb "$fn_name - Interface eth0.4 brought UP" ||
            log -deb "$fn_name - Failed to bring up interface eth0.4, checking udhcpc"
    fi

    eth_04_address=$(interface_ip_address eth0.4)
    if [ -z "$eth_04_address" ]; then
        log -deb "$fn_name - Interface eth0.4 has no address, setting udhcpc"
        log -deb "$fn_name - Running force address renew for eth0.4"
        no_address=1
        while [ "$no_address" = 1 ]; do
            log -deb "$fn_name - Killing old eth0.4 udhcpc pids"
            dhcpcd_pids=$(pgrep -f "/sbin/udhcpc .* eth0.4")
            kill $dhcpcd_pids &&
                log -deb "$fn_name - eth0.4 udhcpc pids killed" ||
                log -deb "$fn_name - No eth0.4 udhcpc pid to kill"
            log -deb "$fn_name - Starting udhcpc on eth0.4"
            /sbin/udhcpc -f -S -i eth0.4 -C -o -O subnet &>/dev/null &
            log -deb "$fn_name - Waiting for eth0.4 address"
            wait_for_function_response notempty 'interface_ip_address eth0.4' &&
                log -deb "$fn_name - eth0.4 address valid" && break ||
                log -deb "$fn_name - Failed to set eth0.4 address, repeating"
        done
    else
        log -deb "$fn_name - Interface eth0.4 address is $eth_04_address"
    fi
}


print_tables()
{
    fn_name="unit_lib:print_tables"
    for table in "$@"
    do
        log -deb "$fn_name - OVSDB table - $table"
        ${OVSH} s "$table"
    done
    return 0
}

############################################ OVSDB SECTION - STOP ######################################################

add_ovs_bridge()
{
    fn_name="${lib_name}:add_ovs_bridge"
    [ $# -ge 1 -a $# -le 3 ] || raise "${fn_name} requires 1-3 input arguments" -arg
    bridge_name=$1
    hwaddr=$2
    bridge_mtu=$3

    if [ -z "${bridge_name}" ]; then
        raise "Empty first input argument: bridge_name" -l "${fn_name}" -arg
    fi
    log -deb "${fn_name} - Add bridge ${bridge_name}"
    ovs-vsctl br-exists ${bridge_name}
    if [ $? = 2 ]; then
        ovs-vsctl add-br ${bridge_name} &&
            log -deb "${fn_name} - Success: ovs-vsctl add-br ${bridge_name}" ||
            raise "Failed: ovs-vsctl add-br ${bridge_name}" -l "${fn_name}" -ds
    else
        log -deb "${fn_name} - Bridge ${bridge_name} already exists"
    fi

    # Set hwaddr if provided
    if [ -z "${hwaddr}" ]; then
        return 0
    else
        log -deb "${fn_name} - Set bridge hwaddr ${hwaddr}"
        ovs-vsctl set bridge ${bridge_name} other-config:hwaddr="${hwaddr}" &&
            log -deb "${fn_name} - Success: set bridge hwaddr" ||
            raise "Failed: set bridge hwaddr" -l "${fn_name}" -ds
    fi

    # Set mtu if provided
    if [ -z "${bridge_mtu}" ]; then
        return 0
    else
        log -deb "${fn_name} - Set bridge mtu ${bridge_mtu}"
        ovs-vsctl set int ${bridge_name} mtu_request=${bridge_mtu} &&
            log -deb "${fn_name} - Success: set bridge mtu" ||
            raise "Failed: set bridge mtu" -l "${fn_name}" -ds
    fi

    return 0
}

add_bridge_interface()
{
    fn_name="unit_lib:add_bridge_interface"
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

    # If only bridge is specified, stop here
    if [ -z "$br_if_name" ]; then
        return 0
    fi

    # If also interface is specified, add it to bridge
    log -deb "$fn_name - Linking $br_name - $br_if_name - $br_mac"
    mac_if=$(mac_get "$br_if_name") &&
        log -deb "$fn_name - Success: mac_get $br_if_name" ||
        raise "Failed to get interface $br_if_name MAC address" -l "$fn_name" -ds

    if [ "$br_name" = "br-home" ]; then
        br_mac=$(printf "%02X:%s" $(( 0x${mac_if%%:*} | 0x2 )) "${mac_if#*:}")
    else
        br_mac=$mac_if
    fi

    ovs-vsctl set bridge "$br_name" other-config:hwaddr="$br_mac" &&
        log -deb "$fn_name - Success: ovs-vsctl set bridge $br_name other-config:hwaddr=$br_mac" ||
        raise "Failed: ovs-vsctl set bridge $br_name other-config:hwaddr=$br_mac" -l "$fn_name" -ds

    ovs-vsctl set int "$br_name" mtu_request=1500 &&
        log -deb "$fn_name - Success: ovs-vsctl set int $br_name mtu_request=1500" ||
        raise "Failed: ovs-vsctl set int $br_name mtu_request=1500" -l "$fn_name" -ds
}

add_bridge_port()
{
    fn_name="unit_lib:add_bridge_port"
    [ $# -ne 2 ] && raise "${fn_name} requires 2 input arguments" -arg
    bridge_name=$1
    port_name=$2

    [ -z "${bridge_name}" ] && raise "Empty input argument" -l "${fn_name}" -arg
    log -deb "$fn_name - Adding bridge port ${port_name} to ${bridge_name}"
    ovs-vsctl br-exists ${bridge_name}
    if [ $? = 2 ]; then
        raise "Failed: bridge ${bridge_name} does not exits" -l "${fn_name}" -ds
    fi
    ovs-vsctl list-ports "${bridge_name}" | grep -qwF "${port_name}"
    if [ $? = 0 ]; then
        log -deb "$fn_name - Port ${port_name} already in bridge ${bridge_name}"
        return 0
    else
        ovs-vsctl add-port "${bridge_name}" "${port_name}" &&
            log -deb "$fn_name - Success: ovs-vsctl add-port ${bridge_name} ${port_name}" ||
            raise "Failed: ovs-vsctl add-port ${bridge_name} ${port_name}" -l $fn_name -ds
    fi
}

remove_bridge_interface()
{
    fn_name="unit_lib:remove_bridge_interface"
    br_name=$1

    ovs-vsctl del-br "$br_name" &&
        log -deb "$fn_name - Success: ovs-vsctl del-br $br_name" ||
        raise "Failed: ovs-vsctl del-br $br_name" -l "$fn_name" -ds
}

set_interface_patch()
{
    fn_name="unit_lib:set_interface_patch"
    if_name=$1
    patch=$2
    peer=$3

    ovs-vsctl set interface "$patch" type=patch &&
        log -deb "$fn_name - Success: ovs-vsctl set interface $patch type=patch" ||
        raise "Failed ovs-vsctl set interface $patch type=patch" -l "$fn_name" -ds

    ovs-vsctl set interface "$patch" options:peer="$peer" &&
        log -deb "$fn_name - Success: ovs-vsctl set interface $patch options:peer=$peer" ||
        raise "Failed ovs-vsctl set interface $patch options:peer=$peer" -l "$fn_name" -ds
}

check_if_port_in_bridge()
{
    fn_name="unit_lib:check_if_port_in_bridge"
    if_name=$1
    br_name=$2

    ovs-vsctl list-ports "$br_name" | grep -q "$if_name"

    if [ "$?" = 0 ]; then
        log -deb "$fn_name - Port $if_name exists on $br_name"
        return 0
    else
        log -deb "$fn_name - Port $if_name does not exist on $br_name"
        return 1
    fi
}

######################################### FUT CLOUD SECTION - START ####################################################


connect_to_fut_cloud()
{
    fn_name="unit_lib:connect_to_fut_cloud"
    target=${1:-"192.168.200.1"}
    port=${2:-"443"}
    cert_dir=${3:-"$FUT_TOPDIR/shell/tools/device/files"}
    ca_fname=${4:-"fut_ca.pem"}
    inactivity_probe=30000

    log -deb "$fn_name - Configure certificates, check if file exists"
    test -f "$cert_dir/$ca_fname" ||
        raise "FAILED: file $cert_dir/$ca_fname NOT found" -l "$fn_name" -ds

    update_ovsdb_entry SSL -u ca_cert "$cert_dir/$ca_fname"
        log -deb "$fn_name - SSL ca_cert set to $cert_dir/$ca_fname" ||
        raise "SSL ca_cert NOT set to $cert_dir/$ca_fname" -l "$fn_name" -ds

    # Remove redirector, to not interfere with the flow
    update_ovsdb_entry AWLAN_Node -u redirector_addr ''
        log -deb "$fn_name - AWLAN_Node redirector_addr set to ''" ||
        raise "AWLAN_Node redirector_addr NOT set to ''" -l "$fn_name" -ds

    # Inactivity probe sets the timing of keepalive packets
    update_ovsdb_entry Manager -u inactivity_probe $inactivity_probe &&
        log -deb "$fn_name - Manager inactivity_probe set to $inactivity_probe" ||
        raise "Manager inactivity_probe NOT set to $inactivity_probe" -l "$fn_name" -ds

    # AWLAN_Node::manager_addr is the controller address, provided by redirector
    update_ovsdb_entry AWLAN_Node -u manager_addr "ssl:$target:$port" &&
        log -deb "$fn_name - AWLAN_Node manager_addr set to ssl:$target:$port" ||
        raise "AWLAN_Node manager_addr NOT set to ssl:$target:$port" -l "$fn_name" -ds

    # CM should ideally fill in Manager::target itself
    update_ovsdb_entry Manager -u target "ssl:$target:$port"
        log -deb "$fn_name - Manager target set to ssl:$target:$port" ||
        raise "Manager target NOT set to ssl:$target:$port" -l "$fn_name" -ds

    log -deb "$fn_name - Waiting for cloud status to go to ACTIVE"
    wait_cloud_state ACTIVE &&
        log -deb "$fn_name - Manager cloud status is set to ACTIVE. Connected to FUT cloud." ||
        raise "FAIL: Manager cloud state is NOT ACTIVE. NOT connected to FUT cloud." -l "$fn_name" -ds
}

wait_cloud_state()
{
    wait_for_cloud_state=$1
    fn_name="cm2_lib:wait_cloud_state"
    log -deb "$fn_name - Waiting for cloud state $wait_for_cloud_state"
    wait_for_function_response 0 "${OVSH} s Manager status -r | grep -q \"$wait_for_cloud_state\"" &&
        log -deb "$fn_name - Cloud state is $wait_for_cloud_state" ||
        raise "Manager - {status:=$wait_for_cloud_state}" -l "$fn_name" -ow
    print_tables Manager
}

######################################### FUT CLOUD SECTION - STOP #####################################################


########################################## FUT CMD SECTION - START #####################################################

########################################## FUT CMD SECTION - STOP ######################################################
