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
# Collect archived syslog messages and crashes
#
. "$LOGPULL_LIB"

# Depending on which logging option is selected, rotated messages may be found in
# either RAM, flash or in both. It is safe to always collect files from both since
# even if logging to RAM and flash is enabled they must be the same in both.

# Collect syslog messages
collect_dir ${CONFIG_FM_LOG_FLASH_ARCHIVE_PATH}/${CONFIG_FM_LOG_ARCHIVE_SUBDIRECTORY}
collect_dir ${CONFIG_FM_LOG_RAM_ARCHIVE_PATH}/${CONFIG_FM_LOG_ARCHIVE_SUBDIRECTORY}

# Move crash files (deleted after requested logpull)
collect_and_delete_dir_files ${CONFIG_FM_LOG_FLASH_ARCHIVE_PATH}/${CONFIG_FM_CRASH_LOG_DIR}
collect_and_delete_dir_files ${CONFIG_FM_LOG_RAM_ARCHIVE_PATH}/${CONFIG_FM_CRASH_LOG_DIR}

# Special log files of managers or any core files present in /var/log/tmp
# are gathered here (deleted after requested logpull)
collect_and_delete_dir_files ${CONFIG_TARGET_PATH_LOG_TRIGGER}


collect_ramoops_log()
{
    # Collect pmsg files
    mkdir -p $LOGPULL_TMP_DIR/pmsg-ramoops
    find /sys/fs/pstore/ -name 'pmsg-ramoops*' -exec cat {} \; | sed -n -e 's/^LOG \(.*\)/\1/p' > $LOGPULL_TMP_DIR/pmsg-ramoops/pmsg-ramoops-0
}

collect_ramoops_log
