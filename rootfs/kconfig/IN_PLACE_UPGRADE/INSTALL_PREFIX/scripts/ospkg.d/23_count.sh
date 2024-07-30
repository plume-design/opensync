
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


ospkg_check_boot_count()
{
    local PKGID="$1"
    if [ "$PKGID" = "builtin" ]; then
        return
    fi
    local COUNT=$(ospkg_get_state $PKGID bcount)
    let COUNT=$((COUNT+1))
    ospkg_set_state "$PKGID" bcount "$COUNT"
    if [ "$COUNT" -gt "$OSPKG_MAX_BOOT_COUNT" ]; then
        ospkg_error "Boot count $PKGID $COUNT exceeded $OSPKG_MAX_BOOT_COUNT"
        return
    fi
    true
}

ospkg_boot_complete()
{
    local CURR=$(ospkg_get_current_slot)
    local NAME=$(ospkg_get_current_name)
    ospkg_notice "Boot complete $NAME"
    if [ -n "$CURR" ]; then
        ospkg_set_state "$CURR" bcount 0
    fi
}

ospkg_check_healthcheck_count()
{
    local PKGID="$1"
    if [ "$PKGID" = "builtin" ]; then
        return
    fi
    # if hc_success is marked stop counting hc_count
    local SUCCESS=$(ospkg_get_state $PKGID hc_success)
    if [ "$SUCCESS" = "1" ]; then
        return
    fi
    local COUNT=$(ospkg_get_state $PKGID hc_count)
    let COUNT=$((COUNT+1))
    ospkg_set_state "$PKGID" hc_count "$COUNT"
    if [ "$COUNT" -gt "$OSPKG_MAX_HC_COUNT" ]; then
        ospkg_error "Healthcheck count $PKGID $COUNT exceeded $OSPKG_MAX_HC_COUNT"
        return
    fi
    true
}

ospkg_healthcheck_complete()
{
    local CURR=$(ospkg_get_current_slot)
    local NAME=$(ospkg_get_current_name)
    if [ "$NAME" = "builtin" ]; then
        return
    fi
    local SUCCESS=$(ospkg_get_state "$CURR" hc_success)
    if [ "$SUCCESS" = "1" ]; then
        return
    fi
    ospkg_notice "Healthcheck success complete $NAME"
    ospkg_set_state "$CURR" hc_count 0
    ospkg_set_state "$CURR" hc_success 1
}

