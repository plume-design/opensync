#!/bin/false

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
INSTALL_PREFIX={{INSTALL_PREFIX}}

source ${INSTALL_PREFIX}/scripts/opensync_functions.sh

once LM_FUNCS || return 1

LM_DIR="${INSTALL_PREFIX}/log_archive"
LM_CRASH_DIR="$LM_DIR/crash"

LM_ROTATE_MAX=10

lm_crash_rotate()
{
    # Keep LM_ROTATE_MAX entries maximum
    find "$LM_DIR" -type f -name "crash-$LABEL-*.tar.gz" | sort | head -n "-${LM_ROTATE_MAX}" | xargs -r rm
}

#
# Store filenames passed as argument into a tarball in the crashlog folder
#
lm_crash_store()
{
    LABEL=$1
    shift
    # Create a unique filename
    LAST_N=$( { echo 0; find "${LM_DIR}/crash" -type f -name "crash-$LABEL-*.tar.gz" | grep -oE '[1-9][0-9]*' | sort -n; } | tail -n 1)
    F="$LM_DIR/crash/crash-$LABEL-$(printf "%04d" $((LAST_N + 1))).tar.gz"
    # Store files to the crashlog folder
    log_info "Creating crashlog $F ..."
    tar -czvf "$F" "$@"

    # Remove old entries
    lm_crash_rotate "$LABEL"
}
