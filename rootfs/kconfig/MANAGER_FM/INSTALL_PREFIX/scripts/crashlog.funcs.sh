#!/bin/false
# {# jinja-parse #}
INSTALL_PREFIX={{INSTALL_PREFIX}}

source ${INSTALL_PREFIX}/scripts/opensync_functions.sh

include log
include lm

once CRASHLOG_FUNCS || return

CRASHLOG_DIR="/sys/fs/pstore"
PSTORE_PROCESSED_FILE="/var/run/pstore_processed"

crashlog_mount()
{
    [ -e "$CRASHLOG_DIR" ] || return 1

    # Skip mounting if pstore has already been mounted
    grep -q "$CRASHLOG_DIR" /proc/mounts && {
        log_info "PSTORE already mounted"
        return 0
    }

    # Persistent messages store is empty, we can use to mount the pstore filesystem itself
    mount -t pstore pstore "$CRASHLOG_DIR"
}

crashlog_store()
{
    # Change current folder to CRASHLOG_DIR first, so we don't create absolute filenames
    (cd "$CRASHLOG_DIR" && lm_crash_store ramoops * ) || return 1
}

#
# Check pstore and save crashlogs to flash
#
crashlog_pstore()
{
    # Process pstore files only once per boot
    [ -e "$PSTORE_PROCESSED_FILE" ]  && {
        log_info "PSTORE already processed"
        return 0
    }

    crashlog_mount || {
        log_err "PSTORE filesystem not detected. Ramoops data wont be saved."
        return 1
    }

    # Copy ramoops log to keep continuation of logs
    for i in $CRASHLOG_DIR/pmsg-ramoops*; do cat $i | grep -e '^LOG' > /dev/pmsg0; done

    # Count number of files on pstore that are not console and pmsg logs
    NFILES=$(find "$CRASHLOG_DIR" -type f -a \! -name 'console-*' -a \! -name 'pmsg-*' | wc -l)

    # Do nothing if there are no files
    [ $NFILES -le 0 ] && return 0

    log_info "PSTORE has usable files. Creating new crashlog..."
    crashlog_store

    # Create the marker file to indicate processing is done
    touch "$PSTORE_PROCESSED_FILE"
}

crashlog_wlan_ramdump()
{
    cd /lib/firmware \
        && ls /lib/firmware/WLAN_FW_*.BIN.* \
        && lm_crash_store wlan-ramdump /lib/firmware/WLAN_FW_*.BIN.* \
        && rm -f /lib/firmware/WLAN_FW_*.BIN.*
}
