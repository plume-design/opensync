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


# This check makes sure that we're not closing in on the global max number of allocated file handles.
# This is done by checking the values in the /proc/sys/fs/file-nr.

MIN_FH_DIFFERENCE=500

# count the number of open fds per pid; prints out "<fd count> : <pid> : <process name>"
get_fd_count_for_pid()
{
    for f in /proc/*/fd; do pid="${f#/proc/})"; echo "$(ls -l $f | wc -l) : ${pid%/*} : $(cat ${f%/fd}/comm)"; done
}

# get the number of used/max num of file handles and compute the diff
used_fh=$(cat /proc/sys/fs/file-nr | cut -f1)
max_fh=$(cat /proc/sys/fs/file-nr | cut -f3)
diff_fh=$(($max_fh-$used_fh))

# check if there's at least MIN_FH_DIFFERENCE file handles left free, if not log top 100 consumers and fail the check
if [ $diff_fh -lt $MIN_FH_DIFFERENCE ]; then
    IFS=$'\n'
    log_warn "Running out of file handles. Listing top 100 consumers (<fd count> : <pid> : <process name>)"
    for l in "$(get_fd_count_for_pid | sort -rn | head -100)"; do log_warn "$l\n"; done
    Healthcheck_Fail
fi

Healthcheck_Pass
