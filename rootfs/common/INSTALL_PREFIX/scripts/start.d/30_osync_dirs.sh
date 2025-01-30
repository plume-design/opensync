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

# {# jinja-parse #}

# Create run dir for dnsmasq
mkdir -p /var/run/dnsmasq

# Create the log trigger directory
mkdir -p "${CONFIG_TARGET_PATH_LOG_TRIGGER}"

{%- if CONFIG_MANAGER_FM %}
FM_SYSLOG_SUBDIR="${CONFIG_FM_LOG_ARCHIVE_SUBDIRECTORY}"
FM_CRASH_LOG_SUBDIR="${CONFIG_FM_CRASH_LOG_DIR}"

# Create FM logging directories in RAM
FM_LOG_DIR_RAM="${CONFIG_FM_LOG_RAM_ARCHIVE_PATH}"
mkdir -p "$FM_LOG_DIR_RAM/$FM_SYSLOG_SUBDIR"
mkdir -p "$FM_LOG_DIR_RAM/$FM_CRASH_LOG_SUBDIR"

# Create FM logging directories in flash
FM_LOG_DIR_FLASH="${CONFIG_FM_LOG_FLASH_ARCHIVE_PATH}"
mkdir -p "$FM_LOG_DIR_FLASH/$FM_SYSLOG_SUBDIR"
mkdir -p "$FM_LOG_DIR_FLASH/$FM_CRASH_LOG_SUBDIR"

# Create a link to the RAM crash directory in flash. The crash files will always
# initially be created in RAM.
if [ ! -e "${CONFIG_OS_BACKTRACE_DUMP_PATH}" ]; then
    ln -s "$FM_LOG_DIR_FLASH/crash" "${CONFIG_OS_BACKTRACE_DUMP_PATH}"
fi
{%- endif %}
