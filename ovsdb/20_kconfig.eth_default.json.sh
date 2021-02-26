
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

cat <<EOF
[
    "Open_vSwitch",
$(for i in $(eval echo $CONFIG_TARGET_ETH0_NAME \
                       $CONFIG_TARGET_ETH1_NAME \
                       $CONFIG_TARGET_ETH2_NAME \
                       $CONFIG_TARGET_ETH3_NAME \
                       $CONFIG_TARGET_ETH4_NAME \
                       $CONFIG_TARGET_ETH5_NAME)
do
test "$CONFIG_OVSDB_BOOTSTRAP" = y || continue
cat <<EOI
    {
        "op":"insert",
        "table":"Wifi_Inet_Config",
        "row":
        {
            "if_name": "$i",
            "ip_assign_scheme": "none",
            "if_type": "eth",
            "enabled": true,
            "network": true,
            "mtu": 1500,
            "NAT": false
        }
    },
EOI
done)
    { "op": "comment", "comment": "" }
]
EOF
