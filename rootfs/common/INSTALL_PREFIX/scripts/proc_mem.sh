#!/bin/sh

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

