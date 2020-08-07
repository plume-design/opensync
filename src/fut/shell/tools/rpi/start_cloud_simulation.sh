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
fut_topdir="$(realpath "$current_dir"/../..)"
source "$fut_topdir/lib/rpi_lib.sh"

help()
{
cat << EOF
Usage: ${MY_NAME} [--help|-h] [--stop|-s]

OPTIONS:
  --help|-h : this help message
  --stop|-s : stop the service instead of starting it

Script configures and (re)starts haproxy that acts as simulated cloud.
EOF
}

ARGS=""
# parse command line arguments
while [[ "${1}" == -* ]]; do
    option="${1}"
    shift
    case "${option}" in
        -h|--help)
            help
            exit 0
            ;;
        -s|--stop)
            stop_cloud_simulation
            exit 0
            ;;
        -r|--restart)
            stop_cloud_simulation
            ;;
        -*)
            help
            exit 0
            ;;
    esac
done

start_cloud_simulation
