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
# Support for --remote option
#
. "$LOGPULL_LIB"

usage_or_run "$1" "--remote <url> <token> <method>" "uploads created logpull archive to remote url"

logpull_remote()
{
    LOGPULL_REMOTE_URL="$1"
    LOGPULL_REMOTE_TOKEN="$2"
    LOGPULL_REMOTE_TYPE="$3"

    (cd /tmp/logpull && mv $LOGPULL_ARCHIVE $LOGPULL_REMOTE_TOKEN)

    OPENSYNC_CAFILE_FULL_PATH="$CONFIG_TARGET_PATH_CERT/$CONFIG_TARGET_OPENSYNC_CAFILE"

    # Upload archive to remote location
    logi "remote type:  $LOGPULL_REMOTE_TYPE"
    logi "remote token: $LOGPULL_REMOTE_TOKEN"
    logi "remote url:   $LOGPULL_REMOTE_URL"
    case "$LOGPULL_REMOTE_TYPE" in
        lm | lm-awlan)
            (cd /tmp/logpull && curl \
                --verbose --fail --silent --show-error \
                --cacert $OPENSYNC_CAFILE_FULL_PATH \
                --form filename=@$LOGPULL_REMOTE_TOKEN \
                $LOGPULL_REMOTE_URL > /tmp/logpull/curl-$$.log 2>&1)
            ;;
        lm-s3-presigned)
            (cd /tmp/logpull && curl \
                --verbose --fail --silent --show-error \
                --cacert $OPENSYNC_CAFILE_FULL_PATH \
                -X PUT --upload-file $LOGPULL_REMOTE_TOKEN \
                $LOGPULL_REMOTE_URL > /tmp/logpull/curl-$$.log 2>&1)
            ;;
        *)
            loge "invalid logpull remote method"
            rm $LOGPULL_REMOTE_TOKEN
            exit 1
            ;;
    esac

    if [ $? -eq 0 ]; then
        logi "uploading successful"
    else
        while IFS= read -r line; do loge "curl: $line"; done < /tmp/logpull/curl-$$.log
        loge "uploading failed"
    fi

    # Cleanup
    (cd /tmp/logpull && rm $LOGPULL_REMOTE_TOKEN && rm curl-$$.log)
}

logpull_remote "$@"
