#!/bin/sh
#
# Support for --stdout option
#
. "$LOGPULL_LIB"

usage_or_run "$1" "--stdout" "dumps created logpull archive to stdout"

logpull_stdout()
{
    cat "$LOGPULL_ARCHIVE" && rm "$LOGPULL_ARCHIVE"
}

logpull_stdout "$@"
