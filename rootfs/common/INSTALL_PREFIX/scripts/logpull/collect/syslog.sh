#!/bin/sh
#
# Collect syslog
#
. "$LOGPULL_LIB"

collect_syslog()
{
    # TODO - sanitiy script looks for filename messages* in logpull tarball
    #collect_file /var/log/messages
    cp /var/log/messages "$LOGPULL_TMP_DIR/messages_$(date +"%Y%m%d_%H%M%S")"
}

collect_syslog
