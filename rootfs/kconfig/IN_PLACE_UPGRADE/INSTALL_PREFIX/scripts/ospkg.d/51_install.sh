
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


ospkg_reboot()
{
    echo "Rebooting..."
    if which preboot >/dev/null; then
        if preboot -Rtype=upgrade -Rreason="ospkg upgrade"; then
            return
        fi
    fi
    reboot
}

ospkg_install_abort()
{
    echo
    if [ -n "$OSPKG_INSTALL_CLEANUP_DIR" -a -d "$OSPKG_INSTALL_CLEANUP_DIR" ]; then
        rm -rf "$OSPKG_INSTALL_CLEANUP_DIR"
    fi
    if [ -n "$1" ]; then
        ospkg_error "$@"
    fi
    ospkg_abort "Install failed"
}

ospkg_install()
{
    local OPT_FORCE_VERSION=false
    local OPT_FORCE_PRECHECK=false
    local OPT_REBOOT=false
    while [ -n "$1" ]; do
        case "$1" in
            --force-version) OPT_FORCE_VERSION=true; ;;
            --force-precheck) OPT_FORCE_PRECHECK=true; ;;
            -r|--reboot) OPT_REBOOT=true; ;;
            -*) ospkg_abort "Unknown option $1" ;;
            *) break; ;;
        esac
        shift
    done
    local PKG_FILE="$1"
    echo "Installing $PKG_FILE"
    if [ ! -e "$PKG_FILE" ]; then
        ospkg_abort "Missing $PKG_FILE"
    fi
    echo -n .
    sync
    local SLOT=$(ospkg_alloc_slot)
    if [ -z "$SLOT" ]; then
        ospkg_install_abort "No pkg slot"
    fi
    local PKGDIR=$(ospkg_get_pkg_dir $SLOT)
    if [ ! -d "$PKGDIR" ]; then
        ospkg_install_abort "No pkg dir $PKGDIR"
    fi
    OSPKG_INSTALL_CLEANUP_DIR="$PKGDIR"
    if ! tar xzf "$PKG_FILE" -C "$PKGDIR"; then
        ospkg_install_abort "Extracting $PKG_FILE"
    fi
    local MSG
    if [ -e "$PKGDIR/md5sum.txt" ]; then
        if ! MSG=$(cd "$PKGDIR" && md5sum -c md5sum.txt 2>&1); then
            echo "$MSG"
            ospkg_install_abort "md5sum failed"
        fi
    fi
    echo -n .
    if ! ospkg_validate_pkg_dir "$PKGDIR"; then
        ospkg_install_abort "Invalid package $PKG_FILE"
    fi
    if ! ospkg_install_check_model_number "$PKGDIR"; then
        ospkg_install_abort
    fi
    if ! ospkg_install_check_current_version "$PKGDIR" "$OPT_FORCE_VERSION"; then
        ospkg_install_abort
    fi
    if ! ospkg_install_check_pre_install_script "$PKGDIR" "$PKG_FILE" "$OPT_FORCE_PRECHECK"; then
        ospkg_install_abort
    fi
    echo -n .
    OSPKG_INSTALL_CLEANUP_DIR=""
    sync
    echo
    # schedule use of package on next reboot
    cp "$PKGDIR/ospkg.info" $(ospkg_state_path "$SLOT" ospkg.info)
    ospkg_set_state "$SLOT" bcount 0
    ospkg_set_slot_link next "$SLOT"
    sync
    echo "Version: $(cat $PKGDIR/version.txt)"
    echo "Installed to: $PKGDIR"
    if [ "$OPT_REBOOT" = "true" ]; then
        ospkg_reboot
    fi
}

