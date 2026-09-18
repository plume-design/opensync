#!/bin/sh
#
# Collect crash core dump files
#
. "$LOGPULL_LIB"


collect_core_dump()
{
    # Collect all core dump files
    mkdir -p $LOGPULL_TMP_DIR/Core
    find "$CONFIG_CORE_DUMP_FILES_SAVE_PATH"/ -name '*core.gz' -exec mv '{}' $LOGPULL_TMP_DIR/Core/ ';'
}

collect_core_dump
