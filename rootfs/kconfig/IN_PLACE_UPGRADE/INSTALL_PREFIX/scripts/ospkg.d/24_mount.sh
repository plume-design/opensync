
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


ospkg_move_mounts()
{
    local DEST="$1"
    local MOUNTS=$(cut -d' ' -f2 /proc/mounts)
    local MNT
    # filter out '/' '$DEST' and '$DEST/*'
    MOUNTS=$(echo "$MOUNTS" | grep -v "^/$\|^${DEST}$\|^${DEST}/")
    # filter out sub-mounts because they are moved along with upper
    # mount points (eg. /dev/pts is moved together with /dev)
    for MNT in $MOUNTS; do
        MOUNTS=$(echo "$MOUNTS" | grep -v "^$MNT/.\+")
    done
    for MNT in $MOUNTS; do
        mkdir -p $DEST$MNT
        mount --move $MNT $DEST$MNT
    done
}

ospkg_move_and_pivot_root()
{
    local DEST="$1"
    local PUT_OLD="$2"
    ospkg_move_mounts "$DEST"
    mkdir -p "$PUT_OLD"
    pivot_root "$DEST" "$PUT_OLD"
}

