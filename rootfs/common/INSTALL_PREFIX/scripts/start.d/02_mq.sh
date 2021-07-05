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


# POSIX message queues system setup. Required params:
# $1 : max number of messages, the queue can store
# $2 : max supported length of single message in bytes
setup_mq()
{
    mqsysdir="/proc/sys/fs/mqueue"
    [ -d $mqsysdir ] || return 0

    # mount mq file-system
    mqfsdir="/dev/mqueue"
    mkdir -p ${mqfsdir}
    mount -t mqueue none ${mqfsdir} > /dev/null 2>&1

    # config queue max capacity if too low
    mcap_max=$1
    if [ $(cat ${mqsysdir}/msg_max) -lt $mcap_max ]; then
        echo $mcap_max > ${mqsysdir}/msg_max
    fi

    # config max msg size if too low
    msize_max=$2
    if [ $(cat ${mqsysdir}/msgsize_max) -lt $msize_max ]; then
        echo $msize_max > ${mqsysdir}/msgsize_max
    fi
}

# setup MQ for max queue capacity (1) & message length (2)
setup_mq 128 8192
