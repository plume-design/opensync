#!/bin/sh
#
# LogPull: Collect system logs, state and current configurations.
#
# This is the main script for logpull feature which is called from OpenSync.
# It uses a helper library logpull.lib.sh which gives us all options
# to collect output of commands, files,...
#
# There are two additional folders:
# - type (what kind of logpull would you like to perform)
# - collect (scripts which collects the system information)
#
# All scripts must be *.sh and have permission +x.
# Here are the general OpenSync scripts and they can be extended by
# platform or vendor scripts with the same paths (rootfs/.../logpull/.../...)
#

LOGPULL_DIR=$(dirname "$(readlink -f "$0")")
LOGPULL_LIB=$LOGPULL_DIR/logpull.lib.sh
LOGPULL_TYPE_DIR=$LOGPULL_DIR/type
LOGPULL_COLLECT_DIR=$LOGPULL_DIR/collect

export LOGPULL_DIR
export LOGPULL_LIB
export LOGPULL_TMP_DIR="/tmp/logpull/logpull-$(date +"%Y%m%d-%H%M%S")"
export LOGPULL_ARCHIVE="/tmp/logpull/logpull-$(date +"%Y%m%d-%H%M%S").tar.gz"

. "$LOGPULL_LIB"

logpull_usage()
{
    echo "Usage:"
    echo "./logpull.sh [options] <logpull_type>"
    echo
    echo "Options:"
    echo " --nopwdmask                          ... skips masking passwords from log files"
    echo 
    echo "Logpull types:"
    for f in $LOGPULL_TYPE_DIR/* ; do
        sh "$f" --usage
    done
    echo
    exit 1
}

exit_handler()
{
    rm -rf "$LOGPULL_TMP_DIR"
}
trap exit_handler EXIT SIGINT SIGTERM

logpull_type()
{
    LOGPULL_TYPE=$(echo ${1:2})
    [ -e "$LOGPULL_TYPE_DIR/$LOGPULL_TYPE.sh" ] || logpull_usage
}

logpull_run()
{
    logi "collecting logpull data ..."

    mkdir -p "$LOGPULL_TMP_DIR"

    # Run collection scripts
    for f in $LOGPULL_COLLECT_DIR/*.sh ; do
        logi "collecting via $f"
        sh "$f"
    done

    # Mask Passwords
    if [ -z "$NO_PWD_MASK" ]; then
        logi "masking passwords from logpull data"

        TARB_EXT=".tar.gz"
        logi "repacking tarballs"
        for f in $(find "$LOGPULL_TMP_DIR" -name "*$TARB_EXT") ; do
            TARB_TMPDIR="$(mktemp -d /tmp/masking-XXXXXX)"
            logi "unpacking $f to $TARB_TMPDIR"
            mkdir -p "$TARB_TMPDIR" >&2 && tar xzf "$f" -C "$TARB_TMPDIR" >&2
            logi "masking tarball"
            find "$TARB_TMPDIR" -type f | xargs $CONFIG_TARGET_PATH_TOOLS/pwdmask -o _MASKED_ -- >&2 ||
            {
                loge "PWD masking failed"
                rm -rf "$TARB_TMPDIR"
                return 1
            }
            logi "repacking $TARB_TMPDIR back to $f"
            tar czf "$f" -C "$TARB_TMPDIR" . >&2 && rm -rf "$TARB_TMPDIR" >&2
        done

        find "$LOGPULL_TMP_DIR" -type f ! -name "$TARB_EXT"| xargs $CONFIG_TARGET_PATH_TOOLS/pwdmask -o _MASKED_ -- >&2 ||
        {
            loge "PWD masking failed"
            return 1
        }
    fi

    # Pack collected information into archive
    logi "packing collected files ..."
    tar cvhzf "$LOGPULL_ARCHIVE" -C "$(dirname $LOGPULL_TMP_DIR)" "$(basename $LOGPULL_TMP_DIR)" >&2

    logi "archive size: $(wc -c $LOGPULL_ARCHIVE | awk '{ print $1 }') B"

    # Run selected logpull type
    shift
    sh "$LOGPULL_TYPE_DIR/$LOGPULL_TYPE.sh" "$@"

    logi "$LOGPULL_TYPE logpull done"
}

# Parse options
if [ "$1" = "--nopwdmask" ]; then
    export NO_PWD_MASK=1
    shift
fi
[ "$#" -lt 1 ] && logpull_usage

# Main
logpull_type "$@"
logpull_run  "$@"
