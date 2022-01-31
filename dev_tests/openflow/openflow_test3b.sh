
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

# Test 3b settings: allow client1 location devices access only
# Set a high priority rule disabling access to the bridge mac address
# Set a lower priority rule enabling access to any other destination
set_location_only() {
    client_mac=$1
    cat  <<EOF
set -x;
ovs-ofctl add-flow ${bridge} \
"table=0,\
dl_src=${client_mac},\
dl_dst=${br_mac},\
priority=1000 \
action=drop";
ovs-ofctl add-flow ${bridge} \
"table=0,\
dl_src=${client_mac},\
priority=900 \
action=normal";
ovs-ofctl add-flow ${bridge} \
"table=0,\
dl_dst=${client_mac},\
dl_src=${br_mac},\
priority=1000 \
action=drop";
ovs-ofctl add-flow ${bridge} \
"table=0,\
dl_dst=${client_mac},\
priority=900 \
action=normal";
EOF
}

# Test 3a unsetting: unfreeze client1's traffic
unset_location_only() {
    client_mac=$1
    cat <<EOF
set -x;
ovs-ofctl del-flows ${bridge} \
"table=0,\
dl_src=${client_mac},\
dl_dst=${br_mac},\
priority=1000" \
--strict;
ovs-ofctl del-flows ${bridge} \
"table=0,\
dl_src=${client_mac},\
priority=900" \
--strict;
ovs-ofctl del-flows ${bridge} \
"table=0,\
dl_dst=${client_mac},\
dl_src=${br_mac},\
priority=1000" \
--strict;
ovs-ofctl del-flows ${bridge} \
"table=0,\
dl_dst=${client_mac},\
priority=900" \
--strict;
EOF
}

# Test 3b start routine:
# - add a rule to Openflow allowing internet access only
start_test3b() {
    set_location_only ${client1_mac}
}

# Test 3b stop routine:
# - destroy the tap interface
# - delete the icmp mirroring rule
stop_test3b() {
    unset_location_only ${client1_mac}
}
