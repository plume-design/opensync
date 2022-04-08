#!/usr/bin/env bash

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


current_dir=$(dirname "$(realpath "$BASH_SOURCE")")
fut_topdir="$(realpath "$current_dir"/../../..)"

# FUT environment loading
source "${fut_topdir}"/config/default_shell.sh
# Ignore errors for fut_set_env.sh sourcing
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh &> /dev/null
source "$fut_topdir/lib/rpi_lib.sh"

usage()
{
cat << usage_string
tools/server/um/um_create_md5_file.sh [-h] arguments
Description:
    - Creates MD5 file of FW image
Arguments:
    -h  show this help message
    \$1 (um_fw_path) : path to clean FW which to create corrupted copy : (string)(required)
Script usage example:
   ./tools/server/um/um_create_md5_file.sh /tmp/clean_device_fw.img
Result:
    - Creates MD5 file for FW image (example clean_device_fw.img.md5)
usage_string
}
if [ -n "${1}" ]; then
    case "${1}" in
        help | \
        --help | \
        -h)
            usage && exit 1
            ;;
        *)
            ;;
    esac
fi

NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "tools/server/um/um_create_md5_file.sh" -arg
um_fw_path=$1

log "tools/server/um/um_create_md5_file.sh - Creating md5 sum file of file $um_fw_path"
um_create_md5_file "$um_fw_path" &&
    log -deb "tools/server/um/um_create_md5_file.sh - md5 sum file created - Success" ||
    raise "FAIL: Could not create md5 sum file" -l "tools/server/um/um_create_md5_file.sh" -ds
