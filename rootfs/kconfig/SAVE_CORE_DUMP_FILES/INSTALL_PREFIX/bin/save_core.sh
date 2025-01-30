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
