
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

# Basic openflow testing routines: create and destroy a tap interface
set_icmp_tap() {
    client_mac=$1
    cat <<EOF
set -x;
ovs-vsctl add-port ${bridge} ${tap_intf} \
-- set interface ${tap_intf} type=internal \
ofport_request=${tap_ofport};
ip link set ${tap_intf} up;
ovs-ofctl mod-port ${bridge} ${tap_intf} no-flood;
ovs-ofctl add-flow ${bridge} \
"table=0,\
dl_src=${client_mac},\
icmp,\
priority=1000 \
action=normal,output:${tap_ofport}";
EOF
}


delete_icmp_tap() {
    client_mac=$1
    cat <<EOF
set -x;
ovs-vsctl del-port ${bridge} ${tap_intf};
ovs-ofctl del-flows ${bridge} \
"table=0,\
dl_src=${client_mac},\
icmp,\
priority=1000" \
--strict;
EOF
}

# Test 2 start routine:
# - create a tap interface
# - add a rule to Openflow to mirror icmp traffic of client to the tap interface
start_test2() {
    set_icmp_tap ${client1_mac}
}

# Test 2 stop routine:
# - destroy the tap interface
# - delete the icmp mirroring rule
stop_test2() {
    delete_icmp_tap ${client1_mac}
}
