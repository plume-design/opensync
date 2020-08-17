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


# This test script will verify that the manager only operates on ovsdb entries
# that match the "module" field with value "hello-world", and not others. This
# shows that several managers can use the same ovsdb table and not interfere
# with each other, if the correct "module" field value is chosen.

# Include basic environment config from default shell file and if any from FUT framework generated /tmp/fut_set_env.sh file
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source ${FUT_TOPDIR}/shell/lib/unit_lib.sh

DEMO_MODULE_NAME="hello-world"
DEMO_OUTPUT_FILE=${DEMO_OUTPUT_FILE:-"/tmp/$DEMO_MODULE_NAME-demo"}
DEMO_TEST_TITLE="Fail to update ovsdb table"
# Input arguments:
DEMO_TEST_KEY=${1:-"fut-variable"}
DEMO_TEST_VALUE=${2:-"test-value"}
DEMO_TEST_FOREIGN_MODULE=${3:-"bye-bye"}

DEMO_OUTPUT_ALT_FILE=${DEMO_OUTPUT_ALT_FILE:-"/tmp/$DEMO_TEST_FOREIGN_MODULE"}

log_title "$DEMO_MODULE_NAME: $DEMO_TEST_TITLE"

log "Test preconditions: Clean ovsdb table if not empty"
${OVSH} delete Node_Config || die "Failed to empty table"

log "Start test: Write to Node_Config"
${OVSH} insert Node_Config module:=$DEMO_TEST_FOREIGN_MODULE key:=$DEMO_TEST_KEY value:=$DEMO_TEST_VALUE || die "Failed"

# Level 1 test - checking correct OVSDB behaviour
log "Checking for Node_State table entry, reflecting entry in Node_Config table"
${OVSH} select Node_State --where module==$DEMO_TEST_FOREIGN_MODULE key:=$DEMO_TEST_KEY module:=$DEMO_TEST_FOREIGN_MODULE value:=$DEMO_TEST_VALUE || die_with_code 11 "Failed - entry found!"
log "OK: no entry found, continuing"

# Level 2 test - checking that no actions were applied to the system
log "Checking correct system action was performed"
log "Verifying existence of file $DEMO_OUTPUT_FILE."
if [ -f $DEMO_OUTPUT_FILE ]; then
    log "File present on system, expecting empty file"
    [ -n "$(cat $DEMO_OUTPUT_FILE)" ] || die_with_code 22 "File not empty!"
else
    log "OK: File $DEMO_OUTPUT_FILE not present on system"
fi
log "Verifying existence of file $DEMO_OUTPUT_ALT_FILE."
[ -f $DEMO_OUTPUT_ALT_FILE ] && die_with_code 23 "File exists!"

pass "$DEMO_TEST_TITLE - TEST PASSED"
