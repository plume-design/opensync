
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


ospkg_abort()
{
    echo "ospkg: ERROR" "$@" >&2
    exit 1
}

ospkg_error()
{
    echo "ospkg: ERROR" "$@" >&2
    false
}

ospkg_notice()
{
    echo "ospkg:" "$@" >&2
    true
}

ospkg_env_check()
{
    [ -n "$OSPKG_DATA_DIR" ] || ospkg_install_abort "Undefined OSPKG_DATA_DIR"
    [ -n "$OSPKG_STATE_DIR" ] || ospkg_install_abort "Undefined OSPKG_STATE_DIR"
}

ospkg_get_slot()
{
    readlink "$OSPKG_STATE_DIR/$1" 2>/dev/null || true
}

ospkg_get_current_slot()
{
    ospkg_get_slot current
}

ospkg_get_next_slot()
{
    ospkg_get_slot next
}

ospkg_get_prev_slot()
{
    ospkg_get_slot prev
}

ospkg_rm_slot_link()
{
    rm -f "$OSPKG_STATE_DIR/$1"
}

ospkg_set_slot_link()
{
    local LINK="$1"
    local TARGET="$2"
    if [ -z "$TARGET" -o -z "$LINK" ]; then
        ospkg_error "Link missing args '$1', '$2'."
        return 1
    fi
    if [ -e "$OSPKG_STATE_DIR/$LINK" -a ! -L "$OSPKG_STATE_DIR/$LINK" ]; then
        ospkg_error "Link $LINK: exist but not a link"
        return 1
    fi
    # if target is "builtin" skip this check
    if [ "$TARGET" != "builtin" -a ! -d "$OSPKG_DATA_DIR/$TARGET" ]; then
        ospkg_error "Link $LINK: no $OSPKG_DATA_DIR/$TARGET"
        return 1
    fi
    mkdir -p "$OSPKG_STATE_DIR"
    ln -sfn "$TARGET" "$OSPKG_STATE_DIR/$LINK"
}

ospkg_get_current_name()
{
    local SLOT=$(ospkg_get_current_slot)
    if [ -z "$SLOT" ]; then
        echo "builtin"
    else
        echo "$SLOT"
    fi
}

ospkg_get_current_dir()
{
    ospkg_get_pkg_dir $(ospkg_get_current_name)
}

ospkg_get_pkg_dir()
{
    local PKGID="$1"
    if [ "$PKGID" = "builtin" ]; then
        echo "$OSPKG_BUILTIN_DIR"
        return
    fi
    if [ -n "$PKGID" ]; then
        local PKGDIR="$OSPKG_DATA_DIR/$PKGID"
        if [ -d "$PKGDIR" ]; then
            echo "$PKGDIR"
        fi
    fi
}

ospkg_get_all_slots()
{
    local LIST=""
    local NUM
    local SLOT
    for NUM in $OSPKG_SLOT_ALL_NUMS; do
        SLOT="$OSPKG_SLOT_PREFIX$NUM"
        LIST="$LIST $SLOT"
    done
    echo $LIST
}

ospkg_get_all_pkgids()
{
    echo "builtin" $(ospkg_get_all_slots)
}


# free any slot except current
# return the new slot name (dir name)
ospkg_alloc_slot()
{
    local CURR=$(ospkg_get_current_slot)
    local SLOT
    for SLOT in $(ospkg_get_all_slots); do
        if [ "$SLOT" = "$CURR" ]; then
            continue
        fi
        ospkg_rm_slot_link next
        ospkg_rm_slot_link prev
        if [ -n "$CURR" ]; then
            ospkg_set_slot_link prev "$CURR"
        fi
        rm -rf "$OSPKG_DATA_DIR/$SLOT"
        mkdir -p "$OSPKG_DATA_DIR/$SLOT"
        ospkg_clean_state "$SLOT"
        echo "$SLOT"
        break
    done
}

ospkg_valid_slot_link()
{
    local SLOT
    for SLOT in $(ospkg_get_all_slots); do
        if [ "$1" = "$SLOT" ]; then
            ospkg_validate_pkg_dir "$OSPKG_DATA_DIR/$1"
            return
        fi
    done
    false
}

ospkg_state_path()
{
    if [ -n "$2" ]; then
        echo "$OSPKG_STATE_DIR/$1/$2"
    else
        echo "$OSPKG_STATE_DIR/$1"
    fi
}

ospkg_get_state()
{
    local F="$OSPKG_STATE_DIR/$1/$2"
    if [ -e "$F" ]; then
        cat "$F"
    fi
}

ospkg_set_state()
{
    mkdir -p "$OSPKG_STATE_DIR/$1"
    echo "$3" > "$OSPKG_STATE_DIR/$1/$2"
}

ospkg_clean_state()
{
    if [ -z "$1" ]; then
        ospkg_error "Clean_state: no arg"
        return 1
    fi
    rm -rf "$OSPKG_STATE_DIR/$1"
    mkdir -p "$OSPKG_STATE_DIR/$1"
}

ospkg_reset_state()
{
    if [ -z "$1" ]; then
        ospkg_error "Reset_state: no arg"
        return 1
    fi
    # reset all counters, keep ospkg.info
    find "$OSPKG_STATE_DIR/$1"/* -name ospkg.info -o -exec rm -rf '{}' ';' 2>/dev/null
    mkdir -p "$OSPKG_STATE_DIR/$1"
}

ospkg_validate_pkg_dir()
{
    local PKG_DIR="$1"
    test -d "$PKG_DIR" || return
    test -f "$PKG_DIR/ospkg.info" || return
    test -f "$PKG_DIR/version.txt" || return
    test -f "$PKG_DIR/fs.sqfs" || test -d "$PKG_DIR/fs" || return
}

ospkg_pkgdir_get_ver()
{
    local PKGDIR="$1"
    if [ -e "$PKGDIR/version.txt" ]; then
        cat "$PKGDIR/version.txt"
    fi
}

ospkg_pkgdir_get_short_ver()
{
    local PKGDIR="$1"
    if [ -e "$PKGDIR/version.txt" ]; then
        cat "$PKGDIR/version.txt" | cut -d ' ' -f 1
    fi
}

ospkg_unqote_prop()
{
    echo "$1" | sed 's/^"//;s/"$//;s/\\"/"/g;s/\\\\/\\/g'
}

ospkg_get_prop()
{
    local PKG_DIR="$1"
    local NAME="$2"
    local PROP=$(sed -n "/^${NAME}=/{s/^[^=]*=//p;q}" "$PKG_DIR/ospkg.info")
    ospkg_unqote_prop "$PROP"
}

