#!/bin/sh -axe

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

_self=$(readlink -f "$0")
_dir=$(dirname "$_self")
_device=$(readlink -f "$_dir/../../../../")
cd "$_device"
make -C core TARGET=native src/owm ovsdb-create -j $(nproc)
test -n "$1" || set -- -t
cd core
db=/tmp/conf.db
bck=$PWD/$(echo work/*native*/rootfs/usr/opensync/etc/conf.db.bck)
lib=$PWD/$(echo work/*native*/lib)
owm=$PWD/$(echo work/*native*/bin/owm)
cp -v "$bck" "$db"
export LD_LIBRARY_PATH=$PWD/$(echo work/*native*/lib)
export PLUME_OVSDB_SOCK_PATH=/tmp/db.sock
export OW_CORE_LOG_SEVERITY=debug
ovsdb-server --run "
	$owm $*
" --remote=punix:"$PLUME_OVSDB_SOCK_PATH" "$db"
