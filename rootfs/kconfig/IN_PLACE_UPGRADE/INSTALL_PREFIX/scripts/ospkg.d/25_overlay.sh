
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


ospkg_check_state_integrity()
{
    local PKGDIR="$1"
    local PKGID="$2"
    if [ "$PKGID" = "builtin" ]; then
        # no check required for builtin
        return 0
    fi
    local STATEDIR=$(ospkg_state_path "$PKGID")
    # check that state and data matches
    if ! cmp -s "$STATEDIR/ospkg.info" "$PKGDIR/ospkg.info"; then
        ospkg_error "State vs data mismatch"
        return 1
    fi
    true
}

# try to mount specified package and return list of layers
# it will return PKG:ROOTFS layers
# PKG can be either a builtin extracted dir or a squashfs mountpoint
# ROOTFS can be either / or a rootfs overlay that hides the builtin/fs
ospkg_try_mount_pkg_layer()
{
    local PKGID="$1"
    if [ -z "$PKGID" ]; then
        return 1
    fi
    local PKGDIR=$(ospkg_get_pkg_dir "$PKGID")
    if [ ! -d "$PKGDIR" ]; then
        return 1
    fi
    if ! ospkg_check_state_integrity $PKGDIR $PKGID; then
        return 1
    fi
    if ! ospkg_check_boot_count $PKGID; then
        return 1
    fi
    if ! ospkg_check_healthcheck_count $PKGID; then
        return 1
    fi
    if [ -d "$PKGDIR/fs" ]; then
        # unpacked fs
        if [ "$PKGID" = "builtin" ]; then
            # if builtin, hide the rootfs pkg fs dir
            # to prevent mount error (since kernel 4.19+):
            # overlayfs: overlapping ... path
            # mount ...: : Too many levels of symbolic links
            rm -rf /overlay/r
            mkdir -p /overlay/r/upper
            mkdir -p /overlay/r/work
            # node 0:0 is used by overlayfs to hide (delete) contents
            mkdir -p /overlay/r/upper/$PKGDIR
            mknod /overlay/r/upper/$PKGDIR/fs c 0 0
            mkdir -p /overlay/rootfs
            mount -t overlay overlay:rootfs -o lowerdir=/,upperdir=/overlay/r/upper,workdir=/overlay/r/work /overlay/rootfs
            # create an empty builtin/fs dir
            mkdir -p /overlay/rootfs/$PKGDIR/fs
            echo "$PKGDIR/fs:/overlay/rootfs"
            return
        else
            # return as is
            echo "$PKGDIR/fs:/"
            return
        fi
    fi
    local SQFS="$PKGDIR/fs.sqfs"
    if [ -f "$SQFS" ]; then
        # squashfs, mount it
        ospkg_notice "mount $SQFS $OSPKG_MNT_DIR"
        if mount "$SQFS" "$OSPKG_MNT_DIR"; then
            echo "$OSPKG_MNT_DIR:/"
            return
        fi
    fi
}

# when installing a new package or reverting to an older one
# reset the upper overlay, that is the writable layer
ospkg_reset_overlay_upper()
{
    local OVERLAY_UPPER="$1"
    if [ ! -d "$OVERLAY_UPPER" ]; then
        return
    fi
    ospkg_notice "Resetting upper overlay: $OVERLAY_UPPER"
    rm -rf "$OVERLAY_UPPER"/*
    rm -rf "$OVERLAY_UPPER"/.[^.]*
}

ospkg_is_valid_overlay()
{
    # split by : and check if dirs exist
    local DIR
    local OVERLAYS=${1//:/ }
    local RET=1
    for DIR in $OVERLAYS; do
        if [ ! -d "$DIR" ]; then
            return 1
        fi
        RET=0
    done
    return $RET
}

# select current active package,
# mount the pkg fs
# return the list of layers
ospkg_select_active_mount_layer()
{
    local LAYER=""
    local OVERLAY_UPPER="$1"
    if [ -z "$OVERLAY_UPPER" ]; then
        ospkg_notice "select_active_mount: missing OVERLAY_UPPER"
    fi
    local CURR=$(ospkg_get_current_slot)
    local NEXT=$(ospkg_get_next_slot)
    # if curr or next defined, try mounting it
    if [ -n "$CURR" -o -n "$NEXT" ]; then
        # get current before any changes
        local CURR_NAME_1=$(ospkg_get_current_name)
        # try active slot (current or next)
        LAYER=$(ospkg_try_mount_pkg_layer $(ospkg_select_active_slot))
        if ! ospkg_is_valid_overlay "$LAYER"; then
            # mount failed, try switching to prev
            LAYER=$(ospkg_try_mount_pkg_layer $(ospkg_switch_to_prev))
        fi
        if ! ospkg_is_valid_overlay "$LAYER"; then
            # current and prev failed, fallback to builtin
            ospkg_rm_slot_link current
            ospkg_rm_slot_link prev
        fi
        # get current after eventual changes
        local CURR_NAME_2=$(ospkg_get_current_name)
        # if current changed reset overlay/upper
        if [ "$CURR_NAME_1" != "$CURR_NAME_2" ]; then
            ospkg_reset_overlay_upper "$OVERLAY_UPPER"
        fi
    fi
    if ! ospkg_is_valid_overlay "$LAYER"; then
        # use builtin
        LAYER=$(ospkg_try_mount_pkg_layer "builtin")
    fi
    echo "$LAYER"
}

# return list of layers (directories separated with :)
# this will include the pkg layer and rootfs layer
# used to mount the overlayfs
# OVERLAY_UPPER has to be provided because on
# pkg switch the upper layer will be reset
ospkg_mount_active_layer()
{
    local OVERLAY_UPPER="$1"
    local LAYER=$(ospkg_select_active_mount_layer "$OVERLAY_UPPER")
    if ospkg_is_valid_overlay "$LAYER"; then
        ospkg_notice "Using overlay: $LAYER"
        echo "$LAYER"
    else
        echo "/"
    fi
}

