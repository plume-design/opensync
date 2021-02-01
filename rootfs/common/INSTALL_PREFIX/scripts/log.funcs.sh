
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

[ -z "$LOG_MODULE" ] && export LOG_MODULE=MAIN
[ -z "$LOG_PROCESS" ] && export LOG_PROCESS="$0"

__log()
{
    lvl="$1"; shift
    mod="$1"; shift
    msg="$*"

    let se_len=16
    spaces="                "

    let scnt="$se_len - (${#lvl} + ${#mod} + 2)"

    # if $msg == -, then we should read the log message from stdin
    if [ "$msg" == "-" ]
    then
        while read _msg
        do
            logger -st "$LOG_PROCESS" "$(printf "<%s>%.*s%s: %s\n" "$lvl" $scnt "$spaces" "$mod" "${_msg}")"
        done
    else
        logger -st "$LOG_PROCESS" "$(printf "<%s>%.*s%s: %s\n" "$lvl" $scnt "$spaces" "$mod" "$msg")"
    fi
}

log_trace()
{
    __log TRACE $LOG_MODULE $*
}

log_debug()
{
    __log DEBUG $LOG_MODULE $*
}

log_info()
{
    __log INFO $LOG_MODULE $*
}

log_notice()
{
    __log NOTICE $LOG_MODULE $*
}

log_warn()
{
    __log WARNING $LOG_MODULE $*
}

log_err()
{
    __log ERR $LOG_MODULE $*
}

log_emerg()
{
    __log EMERG $LOG_MODULE $*
}
