
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


ospkg_switch_to_next()
{
    local CURR=$(ospkg_get_current_slot)
    local NEXT=$(ospkg_get_next_slot)
    # if next exists, move prev to current and current to next
    if [ -z "$NEXT" ]; then
        return
    fi
    # if next is builtin, remove current and next links
    # and fallback to builtin
    if [ "$NEXT" = "builtin" ]; then
        ospkg_notice "Switching to next: $NEXT"
        ospkg_rm_slot_link next
        ospkg_rm_slot_link current
        return
    fi
    if ! ospkg_valid_slot_link "$NEXT"; then
        ospkg_error "Invalid next $NEXT"
        ospkg_rm_slot_link next
        return
    fi
    ospkg_reset_state $NEXT
    if [ -n "$CURR" ]; then
        ospkg_set_slot_link prev $CURR
    fi
    ospkg_set_slot_link current $NEXT
    ospkg_rm_slot_link next
    ospkg_notice "Switching to next: $NEXT"
    echo "$NEXT"
}

ospkg_switch_to_prev()
{
    local CURR=$(ospkg_get_current_slot)
    local PREV=$(ospkg_get_prev_slot)
    local TARGET="$PREV"
    # try to fallback to prev
    if [ -z "$PREV" ]; then
        # no prev, fallback to builtin
        TARGET=builtin
    elif ! ospkg_valid_slot_link "$PREV"; then
        # prev invalid, fallback to builtin
        TARGET=builtin
    elif [ "$PREV" = "$CURR" ]; then
        # prev same as current, fallback to builtin
        TARGET=builtin
    fi
    ospkg_notice "Switching to previous: $TARGET"
    if [ "$TARGET" = builtin ]; then
        # switch to builtin
        ospkg_rm_slot_link prev
        ospkg_rm_slot_link current
        # no output indicates builtin
    else
        # switch to prev
        ospkg_set_slot_link current "$PREV"
        ospkg_rm_slot_link prev
        echo "$PREV"
    fi
}

ospkg_select_active_slot()
{
    # try switching to next if set
    local NEXT=$(ospkg_switch_to_next)
    if [ -n "$NEXT" ]; then
        echo "$NEXT"
        return 0
    fi
    local CURR=$(ospkg_get_current_slot)
    if [ -z "$CURR" ]; then
        # current is not set, use builtin
        return 0
    fi
    # return current if valid
    if ospkg_valid_slot_link "$CURR"; then
        echo "$CURR"
        return
    fi
    ospkg_error "Invalid current $CURR"
    # switch to previous or builtin
    ospkg_switch_to_prev
}

ospkg_revert()
{
    local CURR=$(ospkg_get_current_slot)
    local PREV=$(ospkg_get_prev_slot)
    local NEXT
    if [ -z "$CURR" ]; then
        # if current is builtin (empty) just remove next
        ospkg_rm_slot_link next
        ospkg_rm_slot_link prev
        NEXT="builtin"
    elif [ "$1" = "--builtin" ]; then
        # revert to builtin regardless of what prev was
        NEXT="builtin"
        ospkg_set_slot_link next $NEXT
        ospkg_rm_slot_link prev
    elif [ -n "$PREV" -a "$PREV" != "$CURR" ]; then
        # if prev exists and is not the same as current
        # set next to prev
        NEXT="$PREV"
        ospkg_set_slot_link next $NEXT
    else
        # fallback to builtin
        NEXT="builtin"
        ospkg_set_slot_link next $NEXT
        ospkg_rm_slot_link prev
    fi
    echo "Set next boot to $NEXT"
}

ospkg_set_next()
{
    local FOUND=""
    local PKGID="$1"
    local ID
    local ALL_IDS=$(ospkg_get_all_pkgids)
    for ID in $ALL_IDS; do
        if [ "$PKGID" = "$ID" ]; then
            FOUND=true
        fi
    done
    if [ "$FOUND" != "true" ]; then
        ospkg_error "Not a valid pkgid: $PKGID"
        return
    fi
    local PKGDIR=$(ospkg_get_pkg_dir "$PKGID")
    if ! ospkg_validate_pkg_dir "$PKGDIR"; then
        ospkg_error "Not a valid install in $PKGDIR"
    fi
    local CURR=$(ospkg_get_current_slot)
    local PREV=$(ospkg_get_prev_slot)
    local NEXT="$PKGID"
    if [ "$CURR" = "$NEXT" ]; then
        ospkg_rm_slot_link next
    elif [ -z "$CURR" -a "$NEXT" = "builtin" ]; then
        ospkg_rm_slot_link next
    elif [ -z "$CURR" ]; then
        ospkg_set_slot_link next $NEXT
    else
        ospkg_set_slot_link next $NEXT
        ospkg_set_slot_link prev $CURR
    fi
    echo "Set next boot to $NEXT"
}


