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


THIS_SCRIPT="${0}"
THIS_DIR=$(dirname "$(readlink -f "${THIS_SCRIPT}")")
myname="$(basename "${THIS_SCRIPT}")"
KCONFIG_FILE="${THIS_DIR}/../etc/kconfig"

. "${KCONFIG_FILE}"

INSTALL_PREFIX="${CONFIG_INSTALL_PREFIX}"

###############################################################################
#
# fm_syslog_rotate.sh
#
# Called when a new logfile is created and needs to be archived:
# - The file is tarballed and archived to the archive location.
# - Filenames (and archive names) are composed as a combination of a
#   sequential number and a time stamp ("messages_NNNNN_YYYYmmdd_HHMMSS"),
#   where the second part is compatible with older versions.
# - The first part (sequential number) guarantees proper ordering of the
#   archived files even if date/time is not properly set/synchronized.
#   The sequential part is formatted using (at least) 5 digits by prepending
#   zeros so that the files are sorted properly when using shell commands
#   (e.g. `ll`, or `cat messages_* > messages_all`).
# - Numbering starts at 00001 (and uses 100000 after 99999).
# - Only the last 'n' archives are kept (i.e. log rotation). The archive
#   with the lowest number is deleted before a new one is added.
# - Once the logfile is archived, the original file is deleted.
#
# Syntax: fm_syslog_rotate.sh <filename> <archive_dir> <syslog_subdir> \
#                             <rotate_max_files> {yes|no} <logs_dir>
#
# Parameters are positional:
#     $1    - name of the new file to be archived
#     $2    - path to the directory for the log archive
#     $3    - sub-directory for archiving (appended to $2)
#     $4    - rotation max files (number of files to keep in the archive)
#     $5    - location of $1 (typically '/var/log')
#
# Example: fm_syslog_rotate.sh messages.0 ${INSTALL_PREFIX}/log_archive/ syslog 8 /var/log/
#
###############################################################################

log() {
    echo "[${myname}] $@"
    logger "[${myname}] $@"
}

LOG_FILE=${1:-messages.*}
LOGS_ARCHIVE=${2:-${INSTALL_PREFIX}/log_archive/}
SYSLOG_SUBDIR=${3:-syslog}
ROTATION_MAX_FILES=${4:-8}
LOGS_LOCATION=${5:-/var/log}

SYSLOG_ARCHIVE=${LOGS_ARCHIVE}/${SYSLOG_SUBDIR}/

# We typically get 'messages.0' (and name archives like 'messages_NNNNN.tar.gz'),
# here we deduce the base name (instead of hard-coding it):
PREFIX=${LOG_FILE%%.*}


# This function counts the archived logs, finds the lowest and highest
# postfix number, and then deletes the archived log with the lowest number
# if the maximum number of archived logs has already been reached.
# All findings are available in variables, and can be examined afterwards.
syslog_archive_rotate()
{
    NUMBER_OF_LOG_FILES=0
    LAST_LOG_NUMBER=0
    FIRST_LOG_NUMBER=
    LOG_FILE_TO_BE_DELETED=

    cd ${SYSLOG_ARCHIVE}

    for FILENAME in ${PREFIX}_*.gz; do
        [ -e "${FILENAME}" ] || continue  # poor man's 'shopt -s nullglob'
        #echo "FILENAME = ${FILENAME}"

        PARSED=${FILENAME%.gz}  # left of .gz
        PARSED=${PARSED%.tar}  # left of .tar (or leave as-is if it's not there)
        PARSED=${PARSED#*_}  # right of first _
        PARSED=${PARSED%%_*}  # left of first _ of what remained
        PARSED=$(expr ${PARSED} + 0)  # strips leading zeros and verifies it is a number
        if [ $? -ge 2 ]; then
            log "WARNING: Unexpected archived log file postfix (in '${FILENAME}')"
            # optionally, delete the file
            #rm -fv ${FILENAME}
            continue
        fi
        #echo "Parsed postfix number = ${PARSED}"

        if [ "${PARSED}" -gt "${LAST_LOG_NUMBER}" ]; then
            LAST_LOG_NUMBER=${PARSED}
        fi
        if [ -z "${FIRST_LOG_NUMBER}" ] || [ "${PARSED}" -lt "${FIRST_LOG_NUMBER}" ]; then
            FIRST_LOG_NUMBER=${PARSED}
            LOG_FILE_TO_BE_DELETED=${FILENAME}
        fi

        NUMBER_OF_LOG_FILES=$((NUMBER_OF_LOG_FILES + 1))
    done

    if [ "${NUMBER_OF_LOG_FILES}" -ge "${ROTATION_MAX_FILES}" ]; then
        log "Deleting oldest archive: '${LOG_FILE_TO_BE_DELETED}'"
        rm -fv ${LOG_FILE_TO_BE_DELETED}
        NUMBER_OF_LOG_FILES=$((NUMBER_OF_LOG_FILES - 1))
    fi

    cd - > /dev/null
}


# Main

if [ ! -e "${LOGS_LOCATION}/${LOG_FILE}" ]; then
    log "ERROR: Incorrect notification, log file '${LOGS_LOCATION}/${LOG_FILE}' not found"
    exit 1
fi

syslog_archive_rotate

if [ "${NUMBER_OF_LOG_FILES}" -ge "${ROTATION_MAX_FILES}" ]; then
    # If we're still not below the maximum allowed number, then the 'max' must
    # have changed. Instead of looping, just delete one more and rely on further
    # invocations of the script to gradually reduce the number (two at a time).
    log "WARNING: Maximum number of log files (${NUMBER_OF_LOG_FILES}) still exceeded"
    syslog_archive_rotate
fi

# Prepare the suffix as NNNNN_YYYYmmdd_HHMMSS
LAST_LOG_NUMBER=$((LAST_LOG_NUMBER + 1))
NEXT_SUFFIX=$(printf "%05d" ${LAST_LOG_NUMBER})
NEXT_SUFFIX=${NEXT_SUFFIX}_$(date +'%Y%m%d_%H%M%S')

#echo "NEXT_SUFFIX = ${NEXT_SUFFIX}"

OUT_FILE=${PREFIX}_${NEXT_SUFFIX}

log "Archiving: '${OUT_FILE}'"

# Busybox's tar does not support the '--transform' option, therefore rename the file before archiving
mv ${LOGS_LOCATION}/${LOG_FILE} ${LOGS_LOCATION}/${OUT_FILE}

tar -C ${LOGS_LOCATION} -cvzf ${SYSLOG_ARCHIVE}/${OUT_FILE}.tar.gz ${OUT_FILE}

EXIT_CODE=$?

rm ${LOGS_LOCATION}/${OUT_FILE}


if [ "${EXIT_CODE}" -ne 0 ]; then
    log "Finished with errors."
else
    log "Done."
fi

exit ${EXIT_CODE}
