
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

##
# LogPull helpers
#
set -a

KCONFIG_ENV_FILE=$LOGPULL_DIR/../../etc/kconfig
. "$KCONFIG_ENV_FILE"

usage_or_run()
{
    case "$1" in
        --usage | --help)
            printf " %-36s ... %s\n" "$2" "$3"
            exit 0
            ;;
        *)
            ;;
    esac
}

loge()
{
    logger -s -p ERROR -t logpull.sh "$@" >&2
}

logi()
{
    logger -s -p INFO -t logpull.sh "$@" >&2
}

collect_cmd()
{
    # Collects output of the command
    output="$LOGPULL_TMP_DIR/$(echo -n "$@" | tr -C "A-Za-z0-9.-" _)"
    ("$@") > "$output" 2>&1 || true
}

collect_file()
{
    # Collects file
    filename="$(echo "$1" | sed "s,/,_,g")"
    [ -e "$1" ] && cp "$1" $LOGPULL_TMP_DIR/$filename
}

collect_file_safe()
{
    local src=$1
    local dst=$2

    local file_size=$(du -k $src | awk '{ print $1 }')
    local free_memory=$(grep MemFree /proc/meminfo | awk '{ print $2 }')

    # We are leaving at least 10 MB of free memory
    [ $(( $free_memory - $file_size )) -gt 10000 ] && {
        cp $src $dst
        return 0
    }

    return 1
}

collect_dir()
{
    if [ -d "$1" ] && [ -n "$(ls -A $1)" ]; then
        ln -sf "$1"/* $LOGPULL_TMP_DIR/
    fi
}
