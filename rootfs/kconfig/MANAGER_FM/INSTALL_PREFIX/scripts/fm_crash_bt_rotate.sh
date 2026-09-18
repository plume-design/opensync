#!/bin/sh
#
# command syntax:
#   fm_crash_bt_rotate.sh  <logs_dir> <crash_file_path>
#
#   <logs_dir>      Directory where crash backtrace logs are stored
#

DIR_CRASH_MESSAGES=$1
CRASH_COUNT_FILE=$2
DEST_DIR=$3
CRASH_FILE_COUNT=$4

ls $DIR_CRASH_MESSAGES | egrep -c 'crashed_*' > $CRASH_COUNT_FILE

while read count; do
    crash_count=$count
done < $CRASH_COUNT_FILE

if [ $crash_count -ge $CRASH_FILE_COUNT ]; then
    cd $DIR_CRASH_MESSAGES

    tar cvzf $DEST_DIR/crash_bt.tar.gz crashed_*

    rm -f crashed_*
fi
