
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

# install checks

ospkg_install_check_model_number()
{
    local PKGDIR="$1"
    local MODEL_NUMBER=$(ospkg_get_model_number)
    local SUPPORTED_MODELS=$(ospkg_get_prop "$PKGDIR" "PKG_SUPPORTED_MODELS")
    if [ -z "$MODEL_NUMBER" ]; then
        ospkg_error "Can't get model number"
        return 1
    fi
    if [ -z "$SUPPORTED_MODELS" ]; then
        ospkg_error "Missing supported models"
        return 1
    fi
    local M
    for M in $SUPPORTED_MODELS; do
        if [ "$M" = "$MODEL_NUMBER" ]; then
            # model number matches
            return 0
        fi
    done
    ospkg_error "Model $MODEL_NUMBER not in supported models $SUPPORTED_MODELS"
    return 1
}

ospkg_install_check_current_version()
{
    local PKGDIR="$1"
    local OPT_FORCE="$2"
    local PKG_VER=$(cat $PKGDIR/version.txt)
    if [ -z "$PKG_VER" ]; then
        ospkg_error "Missing version"
        return 1
    fi
    local CURR_DIR=$(ospkg_get_current_dir)
    if cmp -s "$CURR_DIR/version.txt" "$PKGDIR/version.txt"; then
        ospkg_error "Same package version already installed and is current"
        if [ "$OPT_FORCE" = "true" ]; then
            ospkg_notice "Install forced."
        else
            return 1
        fi
    fi
    return 0
}

ospkg_install_check_pre_install_script()
{
    local PKGDIR="$1"
    local PKG_FILE="$2"
    local OPT_FORCE="$3"
    local SCRIPT="$PKGDIR/scripts/pre-install-check"
    if [ ! -e "$SCRIPT" ]; then
        return 0
    fi
    if "$SCRIPT" "$PKGDIR" "$PKG_FILE" "$OSPKG_BUILTIN_DIR"; then
        return 0
    fi
    ospkg_error "Script pre-install-check failed"
    if [ "$OPT_FORCE" = "true" ]; then
        ospkg_notice "Install forced."
        return 0
    fi
    return 1
}

ospkg_check_sdk_commit()
{
    local TMP_PKG_DIR="$1"
    local BUILTIN_DIR="$2"

    local PKG_COMMIT=$(ospkg_get_prop "$TMP_PKG_DIR" "PKG_SDK_COMMIT")
    local DEST_COMMIT=$(ospkg_get_prop "$BUILTIN_DIR" "PKG_SDK_COMMIT")

    if [ -z "$PKG_COMMIT" ]; then
        ospkg_error "Missing SDK_COMMIT in $TMP_PKG_DIR"
        return 1
    fi

    if [ -z "$DEST_COMMIT" ]; then
        ospkg_error "Missing SDK_COMMIT in $BUILTIN_DIR"
        return 1
    fi

    if [ "$PKG_COMMIT" != "$DEST_COMMIT" ]; then
        ospkg_error "Mismatch SDK_COMMIT $PKG_COMMIT $DEST_COMMIT"
        return 1
    fi

    return 0
}

