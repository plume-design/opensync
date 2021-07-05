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


# options:
# -e   : extended stats
# -t   : print also totals
# -s   : sum totals from stdin

AWK_SUM='{if($1>0){a+=$2;b+=$3;c+=$4;d+=$5;}print}END{print "total",a,b,c,d,"total"}'
AWK_PSS='BEGIN{s=0}/Pss/{s+=$2}END{print s}'
AWK_VMM='/VmRSS/{rss=$2}/VmHWM/{hwm=$2}/VmSize/{vm=$2}END{print rss,hwm,vm;exit vm==0}'
HEADER="PID PSS RSS HWM VM name"
if [ "$1" = -e ]; then
AWK_VMM='/VmRSS/{rss=$2}/VmHWM/{hwm=$2}/VmSize/{vm=$2}
/VmData/{data=$2}/VmStk/{stk=$2}/VmExe/{exe=$2}/VmLib/{lib=$2}
END{print rss,hwm,vm,data,stk,exe,lib;exit vm==0}'
HEADER="PID PSS RSS HWM VM DATA STACK EXE LIB name"
shift
fi


proc_mem_sum()
{
    awk "$AWK_SUM"
}

proc_mem()
{
    echo "$HEADER"
    pgrep . -a | cut -d' ' -f1-2 | while read pid name; do
        vmm=$(awk "$AWK_VMM" /proc/$pid/status 2>/dev/null) || continue
        pss=$(awk "$AWK_PSS" /proc/$pid/smaps 2>/dev/null) || continue
        echo $pid $pss $vmm $name
    done
}

case "$1" in
    -s) proc_mem_sum ;;
    -t) proc_mem | proc_mem_sum ;;
    *) proc_mem ;;
esac

