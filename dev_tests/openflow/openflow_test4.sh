
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

# Basic openflow testing routines: freeze and unfreeze a client device

conf_dtap_rule() {
    cat <<EOF
set -x;
ovs-ofctl add-flow ${bridge} \
"table=0,\
priority=1000, \
ct_state=-trk, ip, \
actions=ct(table=0,zone=1)";
ovs-ofctl add-flow ${bridge} \
"table=0,\
priority=1000, \
ct_state=+trk, ct_mark=0, ip, \
actions=ct(zone=1,commit,exec(load:0x1->NXM_NX_CT_MARK[])),NORMAL,output:${intf}";
ovs-ofctl add-flow ${bridge} \
"table=0,\
priority=1000, \
ct_state=+trk,ct_mark=1,ct_zone=1,ip, \
actions=NORMAL,output:${intf}";
ovs-ofctl add-flow ${bridge} \
"table=0,\
priority=1000, \
ct_state=+trk,ct_mark=2,ct_zone=1,ip, \
action=NORMAL";
ovs-ofctl add-flow ${bridge} \
"table=0,\
priority=1000, \
ct_state=+trk,ct_mark=3,ct_zone=1,ip, \
action=DROP";
EOF
}

unconf_dtap_rule() {
    cat <<EOF
set -x;
ovs-ofctl del-flows ${bridge} \
"table=0,\
ct_state=-trk, ip";
ovs-ofctl del-flows ${bridge} \
"table=0,\
ct_state=+trk, ct_mark=0, ip";
ovs-ofctl del-flows ${bridge} \
"table=0,\
ct_state=+trk,ct_mark=1, ip";
ovs-ofctl del-flows ${bridge} \
"table=0,\
ct_state=+trk,ct_mark=2, ip";
ovs-ofctl del-flows ${bridge} \
"table=0,\
ct_state=+trk,ct_mark=3, ip";
EOF
}

# Test 4 start routine: configure dpi rules
start_test4() {
    conf_dtap_rule
}

# Test 4 stop routine: unconfigure dpi rules
stop_test4() {
    unconf_dtap_rule
}
