
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

DF_CRIT_LOW="15"    # % of filesystem free
DF_WARN_LOW="30"    # % of filesystem free
LOG_MODULE="TMP_DF"

# Check free space on /tmp. If tmp free space is bellow the configured limit,
# the check fails with logging the top offenders.

# Calculate free space on /tmp, express it as %
DF=$(df -k /tmp | tail -n +2 | awk '{printf("%0.0f", $4 / $2 * 100.0)}')

if [ "${DF}" -le ${DF_CRIT_LOW} ]
then
    log_err "/tmp free space below ${DF_CRIT_LOW}% (${DF}% free). Listing top 15 offenders:"
    find /tmp -xdev -type f -exec du -k {} \; | sort -nr | head -n 15 | log_err -
    Healthcheck_Fatal "20_tmp_df.sh"
elif [ "${DF}" -le ${DF_WARN_LOW} ]
then
    log_warn "/tmp is getting low on space (${DF}% free)."
fi

Healthcheck_Pass
