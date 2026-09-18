#!/bin/sh

#Disables Sipalg on start
#Gets enabled or stays disabled after it connects to cloud.

if lsmod | grep -q 'sip'; then
    rmmod nf_nat_sip && rmmod nf_conntrack_sip
fi
