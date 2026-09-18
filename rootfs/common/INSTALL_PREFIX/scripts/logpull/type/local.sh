#!/bin/sh
#
# Support for --local option
#
. "$LOGPULL_LIB"

usage_or_run "$1" "--local" "creates local logpull archive"

logpull_local()
{
    # Print archive path
    logi "archive name: $LOGPULL_ARCHIVE"
}

logpull_local "$@"
