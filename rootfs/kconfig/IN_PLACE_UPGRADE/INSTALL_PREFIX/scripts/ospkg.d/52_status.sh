
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


ospkg_status()
{
    local VERBOSE=false
    local ALL_IDS=$(ospkg_get_all_pkgids)
    local ALL_LINKS="current prev next"
    local PKGID
    local PKGDIR
    local VER
    local LINK
    local USED
    if [ "$1" = "-v" ]; then
        VERBOSE=true
        echo
    fi
    # print version for all links
    for LINK in $ALL_LINKS; do
        SLOT=$(ospkg_get_slot "$LINK")
        if [ "$LINK" = current -a -z "$SLOT" ]; then
            SLOT=builtin
        fi
        PKGDIR=$(ospkg_get_pkg_dir "$SLOT")
        VER=$(ospkg_pkgdir_get_short_ver "$PKGDIR")
        NAME="$SLOT"
        if [ -z "$SLOT" ]; then
            NAME=none
        fi
        printf "%-16s %s\n" "$LINK=$NAME:" "$VER"
    done
    # print version for builtin
    PKGDIR=$(ospkg_get_pkg_dir "builtin")
    VER=$(ospkg_pkgdir_get_short_ver "$PKGDIR")
    printf "%-16s %s\n" "builtin:" "$VER"
    # additional details if verbose option
    if [ "$VERBOSE" != "true" ]; then
        return
    fi
    echo
    echo "Install dirs:"
    for PKGID in $ALL_IDS; do
        PKGDIR=$(ospkg_get_pkg_dir $PKGID)
        VER=""
        if [ -d "$PKGDIR" ]; then
            VER=$(ospkg_pkgdir_get_ver $PKGDIR)
        fi
        USED=""
        for LINK in $ALL_LINKS; do
            SLOT=$(ospkg_get_slot "$LINK")
            if [ "$SLOT" = "$PKGID" ]; then
                USED="$USED $LINK"
            fi
        done
        echo "$PKGID:" $USED
        echo "- dir:" "$PKGDIR"
        echo "- ver:" "$VER"
        [ -n "$PKGDIR" ] || continue
        [ "$PKGID" != "builtin" ] || continue
        local bcount=$(ospkg_get_state $PKGID bcount)
        local hc_count=$(ospkg_get_state $PKGID hc_count)
        local hc_success=$(ospkg_get_state $PKGID hc_success)
        echo "- bcount: $bcount hc_count: $hc_count hc_success: $hc_success"
    done
    echo
}

