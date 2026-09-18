#!/bin/sh
# {# jinja-parse #}

# Create run dir for dnsmasq
mkdir -p /var/run/dnsmasq

# Create the log trigger directory
mkdir -p "${CONFIG_TARGET_PATH_LOG_TRIGGER}"

{%- if CONFIG_MANAGER_FM %}
FM_SYSLOG_SUBDIR="${CONFIG_FM_LOG_ARCHIVE_SUBDIRECTORY}"
FM_CRASH_LOG_SUBDIR="${CONFIG_FM_CRASH_LOG_DIR}"

# Create FM logging directories in RAM
FM_LOG_DIR_RAM="${CONFIG_FM_LOG_RAM_ARCHIVE_PATH}"
mkdir -p "$FM_LOG_DIR_RAM/$FM_SYSLOG_SUBDIR"
mkdir -p "$FM_LOG_DIR_RAM/$FM_CRASH_LOG_SUBDIR"

# Create FM logging directories in flash
FM_LOG_DIR_FLASH="${CONFIG_FM_LOG_FLASH_ARCHIVE_PATH}"
mkdir -p "$FM_LOG_DIR_FLASH/$FM_SYSLOG_SUBDIR"
mkdir -p "$FM_LOG_DIR_FLASH/$FM_CRASH_LOG_SUBDIR"

# Create a link to the RAM crash directory in flash. The crash files will always
# initially be created in RAM.
if [ ! -e "${CONFIG_OS_BACKTRACE_DUMP_PATH}" ]; then
    ln -s "$FM_LOG_DIR_FLASH/crash" "${CONFIG_OS_BACKTRACE_DUMP_PATH}"
fi
{%- endif %}
