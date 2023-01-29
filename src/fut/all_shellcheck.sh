#!/bin/sh

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


# This script is meant for manual shellcheck on device-core directory.
# To execute shellcheck on all device-core directories that contain shell
# scripts or on individual files, type:
# ./all_shellcheck.sh
# or:
# ./all_shellcheck.sh all
# To execute shellcheck on all device-core testcase scripts in fut directories, type:
# ./all_shellcheck.sh shell
# To execute shellcheck on all device-core libraries in src/fut/shell/lib directory, type:
# ./all_shellcheck.sh lib
# One can also control which files are checked by commenting or uncommenting
# 'shellcheck' lines.

# Ignored shellcheck warnings:
# SC1090: Can't follow non-constant source. Use a directive to specify location.
# SC1091: Not following: Reasons include: file not found, no permissions,
#         not included on the command line, not allowing shellcheck to follow files with -x, etc.
# SC2002: Useless cat. Consider 'cmd < file | ..' or 'cmd file | ..' instead.
# SC2015: Note that A && B || C is not if-then-else. C may run when A is true.
# SC2039: In POSIX sh, *something* is undefined.
# SC2181: Check exit code directly with e.g. 'if mycmd;', not indirectly with $?.

check=${1:-all}

excludes=SC1090,SC1091,SC2002,SC2015,SC2039,SC2181

if [ "$check" = "shell" ] || [ "$check" = "all" ]; then
    shellcheck --exclude=$excludes ./../cm2/fut/*.sh
    shellcheck --exclude=$excludes ./../dm/fut/*.sh
    shellcheck --exclude=$excludes ./../fsm/fut/*.sh
    shellcheck --exclude=$excludes ./../nm2/fut/*.sh
    shellcheck --exclude=$excludes ./../pm/fut/*.sh
    shellcheck --exclude=$excludes ./../qm/fut/*.sh
    shellcheck --exclude=$excludes ./../sm/fut/*.sh
    shellcheck --exclude=$excludes ./../um/fut/*.sh
    shellcheck --exclude=$excludes ./../wm2/fut/*.sh
fi

if [ "$check" = "lib" ] || [ "$check" = "all" ]; then
    shellcheck --exclude=$excludes ./../fut/shell/lib/base_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/brv_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/cm2_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/dm_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/fsm_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/lm_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/nm2_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/onbrd_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/othr_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/qm_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/rpi_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/sm_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/um_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/unit_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/ut_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/wm2_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/vpnm_lib.sh
    shellcheck --exclude=$excludes ./../fut/shell/lib/pm_lib.sh
fi
