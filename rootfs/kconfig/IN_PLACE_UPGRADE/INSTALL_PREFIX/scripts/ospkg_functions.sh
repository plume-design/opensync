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

OPENSYNC_INSTALL_PREFIX={{CONFIG_INSTALL_PREFIX}}

# ospkg tool can be either in /usr/opensync/tools
# or in /ospkg/tools for boot-time preinit
# derive the dir from the cmd
OSPKG_INSTALL_PREFIX=$(dirname $(dirname $(readlink -f "$0")))
INSTALL_PREFIX="$OSPKG_INSTALL_PREFIX"
SCRIPTS_DIR="$INSTALL_PREFIX/scripts"

. "$SCRIPTS_DIR/opensync_functions.sh"

INSTALL_PREFIX="$OSPKG_INSTALL_PREFIX"
SCRIPTS_DIR="$INSTALL_PREFIX/scripts"

for F in $SCRIPTS_DIR/ospkg.d/[0-9]*.sh; do
    if [ -e "$F" ]; then
        . "$F"
    fi
done

