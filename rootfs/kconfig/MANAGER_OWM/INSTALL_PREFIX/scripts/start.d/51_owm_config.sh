
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

 #!/bin/sh

OPENSYNC_WIRELESS_CONFIG="${INSTALL_PREFIX}/etc/config/interfaces.ini"
ovsh=${INSTALL_PREFIX}/tools/ovsh
osff_get=${INSTALL_PREFIX}/tools/osff_get

uuid_in() {
	grep -o '"[^"]*-[^"]*"'
}

uuid_out() {
	echo -n '["set",['
	sed 's/^/["uuid",/; s/$/]/' | tr '\n' ',' | sed 's/,$//'
	echo ']]'
}

uuid_add() {
	( uuid_in; printf '"%s"\n' "$1"; ) | uuid_out
}

add_uuid() {
	vif_uuid=$1
	radio=$2

	ovsh u Wifi_Radio_Config -w if_name==$radio vif_configs::"$(
	ovsh s -Ur Wifi_Radio_Config -w if_name==$radio vif_configs \
		| uuid_add "$vif_uuid"
	)"
}

radio_suffix() {
	radio=$1

	band=$($ovsh s Wifi_Radio_Config -w if_name==$radio freq_band -r)
	if [ $band =  "5G" ]; then
		echo "50"
	elif [ $band =  "5GL" ]; then
		echo "l50"
	elif [ $band =  "5GU" ]; then
		echo "u50"
	elif [ $band = "2.4G" ]; then
		echo "24"
	elif [ $band = "6G" ]; then
		echo "60"
	fi
}

create_vif() {
	radio=$1
	ifname=$2

	new_uuid=$($ovsh i Wifi_VIF_Config if_name:=$ifname enabled:=false mode:=ap)
	add_uuid $new_uuid $radio
}

create_vifs() {
	radio=$1
	suffix=$2

	if_prefix=$(awk -F "=" '/prefix/ {print $2}' $OPENSYNC_WIRELESS_CONFIG)
	for i in $if_prefix
	do
		ifname="$i$suffix"
		create_vif $radio $ifname
	done
}

populate_Wifi_VIF_Config() {
	for radio in $($ovsh s Wifi_Radio_Config if_name -r)
	do
		suffix=$(radio_suffix $radio)
		create_vifs $radio $suffix
	done
}

use_vif_rows_decoupled() {
	if $osff_get "use_vif_rows_decoupled"; then
		true;
	else
		false;
	fi
}

if use_vif_rows_decoupled
then
	populate_Wifi_VIF_Config
fi
