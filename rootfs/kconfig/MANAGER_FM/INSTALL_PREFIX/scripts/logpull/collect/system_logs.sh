#!/bin/sh
#
# Collect archived syslog messages and crashes
#
. "$LOGPULL_LIB"

# Depending on which logging option is selected, rotated messages may be found in
# either RAM, flash or in both. It is safe to always collect files from both since
# even if logging to RAM and flash is enabled they must be the same in both.

# Collect syslog messages
collect_dir ${CONFIG_FM_LOG_FLASH_ARCHIVE_PATH}/${CONFIG_FM_LOG_ARCHIVE_SUBDIRECTORY}
collect_dir ${CONFIG_FM_LOG_RAM_ARCHIVE_PATH}/${CONFIG_FM_LOG_ARCHIVE_SUBDIRECTORY}

# Move crash files (deleted after requested logpull)
collect_and_delete_dir_files ${CONFIG_FM_LOG_FLASH_ARCHIVE_PATH}/${CONFIG_FM_CRASH_LOG_DIR}
collect_and_delete_dir_files ${CONFIG_FM_LOG_RAM_ARCHIVE_PATH}/${CONFIG_FM_CRASH_LOG_DIR}

# Special log files of managers or any core files present in /var/log/tmp
# are gathered here (deleted after requested logpull)
collect_and_delete_dir_files ${CONFIG_TARGET_PATH_LOG_TRIGGER}


collect_ramoops_log()
{
    # Collect pmsg files
    mkdir -p $LOGPULL_TMP_DIR/pmsg-ramoops
    find /sys/fs/pstore/ -name 'pmsg-ramoops*' -exec cat {} \; | sed -n -e 's/^LOG \(.*\)/\1/p' > $LOGPULL_TMP_DIR/pmsg-ramoops/pmsg-ramoops-0
}

collect_ramoops_log
