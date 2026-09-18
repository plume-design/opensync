#!/bin/sh
# {# jinja-parse #}

#
# This script can be used as a wrapper for saving core files. It takes 4 arguments:
#       $1 - executable name (%e)
#       $2 - PID (%p)
#       $3 - timestamp (%t)
#       $4 - signal that caused the crash (%s)
#
# The script can be enabled with the following command:
#
# echo '|{{INSTALL_PREFIX}}/bin/save_core.sh %e %p %t %s' > /proc/sys/kernel/core_pattern
# ulimit -c unlimited
#

log()
{
        logger -s -t savecore -- "$@"
}

die()
{
        log FATAL: "$@"
        exit 1
}

[ -z "$1" -o -z "$2" -o -z "$3" -o -z "$4" ] && die "Invalid number of arguments"

mkdir -p {{ CONFIG_CORE_DUMP_FILES_SAVE_PATH }} || die "Unable to create savecore directory: {{ CONFIG_CORE_DUMP_FILES_SAVE_PATH }}"

CORE_FILE="{{ CONFIG_CORE_DUMP_FILES_SAVE_PATH }}/$1.core.gz"

log "Process $1 crashed (PID: $2) due to signal $4. Saving core file to: $CORE_FILE"

cat | gzip -9 > "$CORE_FILE.tmp" || die "Error saving core file: $CORE_FILE.tmp"
mv "$CORE_FILE.tmp" "$CORE_FILE" || die "Error moving core file: $CORE_FILE"
