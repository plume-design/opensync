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
# Collect certs info
#
. "$LOGPULL_LIB"

collect_certs()
{
    PRIV_CERT="$CONFIG_TARGET_PATH_CERT/$CONFIG_TARGET_PATH_PRIV_CERT"
    CERT_CA="$CONFIG_TARGET_PATH_CERT/$CONFIG_TARGET_PATH_CERT_CA"
    OPENSYNC_CAFILE="$CONFIG_TARGET_PATH_OPENSYNC_CERTS/$CONFIG_TARGET_OPENSYNC_CAFILE"

    # This command shows all certificates in case a file contains multiple
    collect_cmd openssl storeutl -noout -text -certs "$PRIV_CERT"
    collect_cmd openssl storeutl -noout -text -certs "$CERT_CA"
    collect_cmd openssl storeutl -noout -text -certs "$OPENSYNC_CAFILE"
}

# Only collect certs info if openssl is available
if ! openssl help >/dev/null 2>&1; then
    echo "openssl not found, skipping certs collection"
    exit 1
fi
collect_certs
