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

# Create a symlink for `logread` to `logread-osync` to ensure that the
# `logread` command is available in the PATH.
# If the system provides its own `logread` implementation, it is still
# used by default, as it precedes the INSTALL_PREFIX directory in PATH.

LOGREAD_OSYNC_PATH="${INSTALL_PREFIX}/tools/logread-osync"
LOGREAD_PATH="${INSTALL_PREFIX}/tools/logread"

# The `LOGREAD_PATH` is not expected to exist by default, but still
# check for it to avoid unnecessary symlink creation or overwriting.
if [ -x "$LOGREAD_OSYNC_PATH" ] && [ ! -e "$LOGREAD_PATH" ]; then
    ln -s "$LOGREAD_OSYNC_PATH" "$LOGREAD_PATH"
fi
