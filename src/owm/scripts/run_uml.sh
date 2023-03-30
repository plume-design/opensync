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

#
# This is intended to be run from (the same as native target
# was built in) docker instance, from the opensync root
# directory (the one, where core/, platform/ and vendor/
# directories are at).
#

prepare=$(readlink -f "$0" | cut -f 1 -d '.')_prepare.sh
$prepare
self=$(readlink -f "$0")
init="${self%.*}_init.sh"
owm=$(readlink -f core/work/native-*/bin/owm)
db=$(readlink -f core/work/native-*/rootfs/usr/opensync/etc/conf.db.bck)
ovsh=$(readlink -f core/work/native-*/bin/ovsh)
mem=${mem:-128M}
dir=$(dirname "$self")

linux.uml \
	mem=$mem \
	time-travel=inf-cpu \
	hostfs=/ \
	root=none \
	rootfstype=hostfs \
	mac80211_hwsim.radios=2 \
	rootflags=/ \
	init=$init \
	env=$(env | grep opt_ | base64 -w0) \
	env_dir=$dir \
	env_db=$db \
	env_owm=$owm \
	env_cmd="$@"
