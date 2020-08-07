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


# This is a basic test script, confirming that updating existing ovsdb entries
# works as expected. It verifies the manager target layer implementation in
# two layers of tests:
#   Layer 1: verify that whatever was written into Node_Config table
#            is reflected in Node_State table, after supposed.
#   Layer 2: verify that the expected action resulted in changes on the system
#            is reflected in Node_State table, after supposed expected action.

# Include basic environment config from default shell file and if any from FUT framework generated /tmp/fut_set_env.sh file
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source ${FUT_TOPDIR}/shell/lib/unit_lib.sh

DEMO_MODULE_NAME="hello-world"
DEMO_OUTPUT_FILE=${DEMO_OUTPUT_FILE:-"/tmp/$DEMO_MODULE_NAME-demo"}
DEMO_TEST_TITLE="Update module-key-value in OVSDB"
# Input arguments:
DEMO_TEST_KEY=${1:-"fut-variable"}
DEMO_TEST_VALUE=${2:-"test-value"}
DEMO_TEST_ALT_VALUE=${3:-"changed-value"}

log_title "$DEMO_MODULE_NAME: $DEMO_TEST_TITLE"

# Table should have entry - insert if not true, e.g. if executing the test standalone
log "Test preconditions: Ovsdb table should have correct entry"
${OVSH} select Node_Config --where module==$DEMO_MODULE_NAME --where key==$DEMO_TEST_KEY --where value==$DEMO_TEST_VALUE
if [ $? -ne 0 ]; then
    log "No entry or entry not correct! Clean and populate ovsdb table"
    ${OVSH} delete Node_Config || die "Failed to empty table"
    ${OVSH} insert Node_Config module:=$DEMO_MODULE_NAME key:=$DEMO_TEST_KEY value:=$DEMO_TEST_VALUE || die "Failed to initialize test"
    ${OVSH} wait Node_State --where value==$DEMO_TEST_VALUE module:=$DEMO_MODULE_NAME key:=$DEMO_TEST_KEY value:=$DEMO_TEST_VALUE || die "Failed to initialize test"
fi

log "Start test: Update an existing entry in Node_Config"
${OVSH} update Node_Config --where key==$DEMO_TEST_KEY module:=$DEMO_MODULE_NAME key:=$DEMO_TEST_KEY value:=$DEMO_TEST_ALT_VALUE || die "Failed"

# Level 1 test - checking correct OVSDB behaviour
log "Waiting for Node_State table to reflect changed entry in Node_Config table"
${OVSH} wait Node_State --where module==$DEMO_MODULE_NAME key:=$DEMO_TEST_KEY module:=$DEMO_MODULE_NAME value:=$DEMO_TEST_ALT_VALUE || die_with_code 11 "Failed"

# Level 2 test - checking the expected actions were applied to the system
log "Checking correct system action was performed"
log "Verifying existence of file $DEMO_OUTPUT_FILE."
[ -f $DEMO_OUTPUT_FILE ] || die_with_code 21 "File not present on system!"
file_content="$(cat $DEMO_OUTPUT_FILE)"
log "Expecting file content: $DEMO_TEST_KEY=$DEMO_TEST_ALT_VALUE"
[ "$file_content" = "$DEMO_TEST_KEY=$DEMO_TEST_ALT_VALUE" ] || die_with_code 22 "File content not correct: $file_content"

pass "$DEMO_TEST_TITLE - TEST PASSED"
