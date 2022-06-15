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
source "${fut_topdir}"/lib/unit_lib.sh

usage() {
    cat << usage_string
tools/client/fsm/make_curl_agent_req.sh [-h] arguments
Description:
    - Script makes curl request to url with specified user_agent
Arguments:
    -h  show this help message
    \$1 (namespace_enter_cmd) : Command to enter interface namespace : (string)(required)
    \$2 (user_agent)          : User agent to pass with curl request : (string)(required)
    \$3 (url)                 : URL to make curl request             : (string)(required)
Script usage example:
   ./tools/client/fsm/make_curl_agent_req.sh "custom_user_agent_name" "www.google.com"
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
NARGS=3
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "tools/client/fsm/make_curl_agent_req.sh" -arg

namespace_enter_cmd=$1
user_agent=$2
url=$3

${namespace_enter_cmd} -c "curl -S -s --output /dev/null -A '${user_agent}' '${url}'" || $(exit 1)
if [[ "$?" != 0 ]];then
    raise "Failed to make curl request to ${url} with user_agent ${user_agent}" -l "tools/client/fsm/make_curl_agent_req.sh"
else
    log "tools/client/fsm/make_curl_agent_req.sh: curl request made to ${url} with user_agent ${user_agent}"
fi
