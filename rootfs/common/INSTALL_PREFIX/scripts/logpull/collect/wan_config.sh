#!/bin/sh
#
# Collect WAN config info
#
. "$LOGPULL_LIB"

collect_wan_config()
{
    collect_cmd $CONFIG_TARGET_PATH_TOOLS/osps -p get local_config wan

    #collecting the files present pstore
    collect_file $CONFIG_PSFS_PRESERVE_DIR/local_config
}

collect_wan_config
