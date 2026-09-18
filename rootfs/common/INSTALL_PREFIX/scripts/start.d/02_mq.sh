#!/bin/sh

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
