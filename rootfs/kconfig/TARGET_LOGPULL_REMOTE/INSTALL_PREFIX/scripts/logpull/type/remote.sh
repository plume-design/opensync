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
    if [ $4 = "--ipv4" ]; then
        LOGPULL_REMOTE_IPV4_FALLBACK="--ipv4"
    fi

    (cd /tmp/logpull && mv $LOGPULL_ARCHIVE $LOGPULL_REMOTE_TOKEN)

    OPENSYNC_CAFILE_FULL_PATH="$CONFIG_TARGET_PATH_OPENSYNC_CERTS/$CONFIG_TARGET_OPENSYNC_CAFILE"

    # Upload archive to remote location
    logi "remote type:  $LOGPULL_REMOTE_TYPE"
    logi "remote token: $LOGPULL_REMOTE_TOKEN"
    logi "remote url:   $LOGPULL_REMOTE_URL"
    [ $LOGPULL_REMOTE_IPV4_FALLBACK = "--ipv4" ] && is_ipv4_fallback="true" || is_ipv4_fallback="false"
    logi "ipv4 fallback: $is_ipv4_fallback"

    upload_result=0
    case "$LOGPULL_REMOTE_TYPE" in
        lm | lm-awlan)
            (cd /tmp/logpull && curl $LOGPULL_REMOTE_IPV4_FALLBACK \
                --verbose --fail --silent --show-error \
                --cacert $OPENSYNC_CAFILE_FULL_PATH \
                --form filename=@$LOGPULL_REMOTE_TOKEN \
                $LOGPULL_REMOTE_URL > /tmp/logpull/curl-$$.log 2>&1)
            ;;
        lm-s3-presigned)
            (cd /tmp/logpull && curl $LOGPULL_REMOTE_IPV4_FALLBACK \
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
        upload_result=1
        loge "uploading failed"
    fi
    return $upload_result
}

logpull_remote_cleanup()
{
    logi "cleaning up /tmp/logpull"
    (cd /tmp/logpull && rm *)
}

logpull_remote "$@"
if [ $? -eq 1 ]; then
    logpull_remote "$@" --ipv4
fi
logpull_remote_cleanup
