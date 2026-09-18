#!/bin/sh

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
