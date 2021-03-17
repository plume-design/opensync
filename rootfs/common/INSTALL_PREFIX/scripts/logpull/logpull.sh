#!/bin/sh

# Copyright (c) 2015, Plume Design Inc. All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    1. Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#    2. Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#    3. Neither the name of the Plume Design Inc. nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
    echo
    for f in $LOGPULL_TYPE_DIR/* ; do
        sh "$f" --usage
    done
    echo
    exit 1
}

logpull_type()
{
    [ "$#" -lt 1 ] && logpull_usage
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

    # Pack collected information into archive
    logi "packing collected files ..."
    tar cvhzf "$LOGPULL_ARCHIVE" -C "$(dirname $LOGPULL_TMP_DIR)" "$(basename $LOGPULL_TMP_DIR)" >&2
    rm -rf "$LOGPULL_TMP_DIR"

    logi "archive size: $(wc -c $LOGPULL_ARCHIVE | awk '{ print $1 }') B"

    # Run selected logpull type
    shift
    sh "$LOGPULL_TYPE_DIR/$LOGPULL_TYPE.sh" "$@"

    logi "$LOGPULL_TYPE logpull done"
}

##
# Main
#
logpull_type "$@"
logpull_run  "$@"
