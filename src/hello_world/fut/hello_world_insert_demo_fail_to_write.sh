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


# This test script will simulate an issue with the manager target layer
# implementation, which will look like failing to write to file - layer 2 test.
# This test is expected to fail for demonstration purposes, to show what
# happenswhen tests fail due to incorrect manager implementation - in this
# case, manager failing to write to file, but still updating Node_State table.

# Include basic environment config from default shell file and if any from FUT framework generated /tmp/fut_set_env.sh file
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source ${FUT_TOPDIR}/shell/lib/unit_lib.sh

DEMO_MODULE_NAME="hello-world"
DEMO_OUTPUT_FILE=${DEMO_OUTPUT_FILE:-"/tmp/$DEMO_MODULE_NAME-demo"}
DEMO_TEST_TITLE="Fail to write to file"
# Input arguments:
DEMO_TEST_KEY=${1:-"demo"}
DEMO_TEST_VALUE=${2:-"fail-to-write"}

log_title "$DEMO_MODULE_NAME: $DEMO_TEST_TITLE"

log "Test preconditions: Clean ovsdb table if not empty"
${OVSH} delete Node_Config || die "Failed to empty table"

log "Start test: Write to Node_Config"
${OVSH} insert Node_Config module:=$DEMO_MODULE_NAME key:=$DEMO_TEST_KEY value:=$DEMO_TEST_VALUE || die "Failed"

# Level 1 test - checking correct OVSDB behaviour
log "Waiting for Node_State table to reflect entry in Node_Config table"
${OVSH} wait Node_State --where module==$DEMO_MODULE_NAME key:=$DEMO_TEST_KEY module:=$DEMO_MODULE_NAME value:=$DEMO_TEST_VALUE || die_with_code 11 "Failed"

# Level 2 test - checking the expected actions were applied to the system
log "Checking correct system action was performed"
log "Verifying existence of file $DEMO_OUTPUT_FILE."
[ -f $DEMO_OUTPUT_FILE ] || die_with_code 21 "File not present on system!"
file_content="$(cat $DEMO_OUTPUT_FILE)"
log "Expecting file content: $DEMO_TEST_KEY=$DEMO_TEST_VALUE"
[ "$file_content" = "$DEMO_TEST_KEY=$DEMO_TEST_VALUE" ] || die_with_code 22 "File content not correct: $file_content"

pass "$DEMO_TEST_TITLE - TEST PASSED"
