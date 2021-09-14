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


## DESCRIPTION ##
# This is a series of tests validating DPP. They
# cover multiple aspects:
#  * responding
#  * initiating
#  * chirping
#  * responding to chirping
#  * different channel configurations to test:
#    * on-chan behavior
#    * off-chan behavior
#    * non-standard spec channel behavior (DPP CC IE)
## /DESCRIPTION ##

## PARAMETERS ##
# dut shell ssh access command, eg. ssh user@host sh -axe
test -n "$dut"
test -n "$dut_phy"
test -n "$dut_vif_ap0"
test -n "$dut_vif_ap1"
test -n "$dut_vif_ap0_idx"
test -n "$dut_vif_ap1_idx"
# ref shell ssh access command, eg. ssh user@host sh -axe
test -n "$ref"
test -n "$ref_phy"
test -n "$ref_vif_sta"
test -n "$ref_vif_ap0"
test -n "$ref_mac_sta"
test -n "$ref_mac_ap0"
ssid=${ssid:-"test-ssid"}
ssid2=${ssid2:-"test-ssid-2"}
psk=${psk:-"12345678"}
confhex=${confhex:-"307702010104202d8961acd594a13ce84028f97e5c0d652f784d70b5d6fe216ef06b5aa9ad2132a00a06082a8648ce3d030107a144034200048467341d13849741f9fdb2ce7b843ee72bb8ee284c696a228e00fec9b6ea37c48ca5fe2f021ee73081078a6805477430442c14efb85ae24461cfc3aaafcd6ee2"}
urikeyhex=${urikeyhex:-"30770201010420835bb02c1319ed97efff01a3db2c8da14603becd7b4c08d7423beecb4657f2cea00a06082a8648ce3d030107a144034200041555426691efc4b17688f739cdaa23663a73e875e78aae0e479f8d0e4b4e6b6f01aba682ce0997c0e9242515cb6b4326daa5caccfcd5a629e6641c1c0b1bfa3e"}
urikeypk=${urikeypk:-"95a7c4aa285f8fb0dff4c7de0cb6c7d1a00f9a2300f5a2954c9e8ec591001dbc"}
urikeychirp=${urikeychirp:-"479af679f6e9e14926d0b357be084d3348ff54c23b8d9c28ba75905491c152dc"}
urikey2hex=${urikey2hex:-"3077020101042017578fd98e92ecf7f9765c7b1eb25c1ae1661fa4f7bff36245657db6a38b5e9ea00a06082a8648ce3d030107a14403420004135b7ff6d2655f55794d386986dbaaba8728d4970c348c3e5f14c0ffcfcc96f901f03f6c3fbb189012ce17bf522a189ae77a64b6ea53285b185b146f4e8cc56e"}
urikey2pk=${urikey2pk:-"35515e923ba25873e1aefc79fdaa610dee9f2c86d9992980e4712329149b0a7c"}
urikey2chirp=${urikey2chirp:-"e9849511cb519fbac35952283bc9b438b5960de3c6809842830646e768f2c1b9"}
urikey3hex=${urikey3hex:-"30770201010420dba178fa14a87a9d509cfeed51865a2efc08bd2994147880b86437285d993ec5a00a06082a8648ce3d030107a14403420004ed7bd054ea90fc73d007c0dbbadb245f3ee84da0f859be56915b27d28236089d50cd4d453f4a7578e4c77682e4a0d00fdd3fa8d9e78e232123d56c276cec6ff2"}
urikey3pk=${urikey3pk:-"b516e034be5ff66977aa854f11b8310c165efa85ce9303484f538df5a941cc54"}
urikey3chirp=${urikey3chirp:-"bdc43132013d89715a4f76e62b7a919da3955568d560138202218df3eeebb808"}
dppconn=${dppconn:-"eyJ0eXAiOiJkcHBDb24iLCJraWQiOiJfNURkaUZVZ3dYZXJWTlktaHlDY2hqZmg3Si1nakN0R0RVZE9PMm1na1lZIiwiYWxnIjoiRVMyNTYifQ.eyJncm91cHMiOlt7Imdyb3VwSWQiOiIqIiwibmV0Um9sZSI6ImFwIn1dLCJuZXRBY2Nlc3NLZXkiOnsia3R5IjoiRUMiLCJjcnYiOiJQLTI1NiIsIngiOiJzOEZOTVJDVi02QWhWTDV6RVRIMlloeklreHUyRFdpelFVWXdnandCMHJjIiwieSI6InBDT0ZvMjllZ3lnSmZNMzBESlFqa3lLMUF2UFdBUGtQLUlvQ284Q3cwaGMifX0._VVcNqEE-GONeAmqrtc-7jsVrVHSZprlLklMJBFb6R7-SOXtleqryCQxQbQkLWZ9sL8qeZvnA2z1NIHQmio5jw"}
dppcsign=${dppcsign:-"3059301306072a8648ce3d020106082a8648ce3d030107034200048467341d13849741f9fdb2ce7b843ee72bb8ee284c696a228e00fec9b6ea37c48ca5fe2f021ee73081078a6805477430442c14efb85ae24461cfc3aaafcd6ee2"}
dppnet=${dppnet:-"3077020101042001444f5f096619c8ba3a47e7d5bfbe237def3908cc278d6b42f65de49d04513ba00a06082a8648ce3d030107a14403420004b3c14d311095fba02154be731131f6621cc8931bb60d68b3414630823c01d2b7a42385a36f5e8328097ccdf40c94239322b502f3d600f90ff88a02a3c0b0d217"}
## /PARAMETERS ##

self=$0
tohex() { od -tx1 -An | tr -d ' \n'; }

setchan() {
	chan_sta=$1
	chan_ap=$2
	dppfreq_sta=$(expr $chan_sta '*' 5 + 2407)
	dppfreq_ap=$(expr $chan_ap '*' 5 + 2407)
	dppchan="C:81/$chan_sta;"
	uri="DPP:${dppchan}V:2;K:MDkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDIgACFVVCZpHvxLF2iPc5zaojZjpz6HXniq4OR5+NDktOa28=;;"
	uri2="DPP:${dppchan}V:2;K:MDkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDIgACE1t/9tJlX1V5TThphtuquoco1JcMNIw+XxTA/8/Mlvk=;;"
	uri3="DPP:${dppchan}V:2;K:MDkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDIgAC7XvQVOqQ/HPQB8DbutskXz7oTaD4Wb5WkVsn0oI2CJ0=;;"
}

ssidhex=$(echo -n $ssid | tohex)
pskhex=$(echo -n $psk | tohex)

prepare_dut() {
	$dut <<. || return $?
		ovsh d Wifi_VIF_Config
		ovsh d Wifi_Radio_Config
		ovsh d Wifi_Associated_Clients
		ovsh d DPP_Oftag
		ovsh d Openflow_Tag

		ovsh i Openflow_Tag \
			name:=home--1

		vif=\$(ovsh -Ur i Wifi_VIF_Config \
			enabled:=true \
			if_name:=$dut_vif_ap0 \
			dpp_connector~=$dppconn \
			dpp_csign_hex~=$dppcsign \
			dpp_netaccesskey_hex~=$dppnet \
			dpp_cc:=true \
			mac_list_type:=none \
			ssid:=$ssid \
			wpa:=true \
			'wpa_key_mgmt::["set", ["wpa2-psk", "dpp"]]' \
			'wpa_oftags::["map", [["key-1", "home--1"]]]' \
			'wpa_psks::["map", [["key-1", "'"$psk"'"]]]' \
			vif_radio_idx:=$dut_vif_ap0_idx \
			mode:=ap
		)

		vif2=\$(ovsh -Ur i Wifi_VIF_Config \
			enabled:=true \
			if_name:=$dut_vif_ap1 \
			dpp_connector~=$dppconn \
			dpp_csign_hex~=$dppcsign \
			dpp_netaccesskey_hex~=$dppnet \
			dpp_cc:=true \
			mac_list_type:=none \
			ssid:=$ssid2 \
			wpa:=true \
			'wpa_key_mgmt::["set", ["wpa2-psk", "dpp"]]' \
			'wpa_oftags::["map", [["key-1", "home--1"]]]' \
			'wpa_psks::["map", [["key-1", "'"$psk"'"]]]' \
			vif_radio_idx:=$dut_vif_ap1_idx \
			mode:=ap
		)

		# add test to check if VIF_State maps properly all requested items
		# FIXME looks like dpp_ ones aren't respected on target_qca at least now
		# FIXME test on hwsim
		# FIXME on target_qca ath0: is running in unexpected min_hw_mode

		ovsh i Wifi_Radio_Config \
			enabled:=true \
			if_name:=$dut_phy \
			ht_mode:=HT20 \
			hw_mode:=11n \
			freq_band:=2.4G \
			channel:=$chan_ap \
			"vif_configs::[\"set\",[[\"uuid\",\"\$vif\"],[\"uuid\",\"\$vif2\"]]]"

		ovsh w Wifi_Radio_State -w if_name==$dut_phy enabled:=true
		ovsh w Wifi_Radio_State -w if_name==$dut_phy ht_mode:=HT20
		ovsh w Wifi_Radio_State -w if_name==$dut_phy hw_mode:=11n
		ovsh w Wifi_Radio_State -w if_name==$dut_phy freq_band:=2.4G
		ovsh w Wifi_Radio_State -w if_name==$dut_phy channel:=$chan_ap

		ovsh w Wifi_VIF_State -w if_name==$dut_vif_ap0 enabled:=true
		ovsh w Wifi_VIF_State -w if_name==$dut_vif_ap0 dpp_connector:=$dppconn
		ovsh w Wifi_VIF_State -w if_name==$dut_vif_ap0 dpp_csign_hex:=$dppcsign
		ovsh w Wifi_VIF_State -w if_name==$dut_vif_ap0 dpp_netaccesskey_hex:=$dppnet

		#ovsh w Wifi_VIF_State -w if_name==$dut_vif_ap0 ssid:=$ssid
		#ovsh w Wifi_VIF_State -w if_name==$dut_vif_ap0 wpa:=true
		#ovsh w Wifi_VIF_State -w if_name==$dut_vif_ap0 mode:=ap
		# todo check maps?

		sleep 3 # extra settle time..
.
}

prepare_ref_sta() {
	$ref <<. || return $?
		timeout 1 wpa_cli -p /var/run/wpa_supplicant -i $ref_vif_sta raw TERMINATE || true
		timeout 1 wpa_cli -p /var/run/hostapd -i $ref_vif_ap0 raw TERMINATE || true
		sleep 3

		cat <<_ >/tmp/wpas
			p2p_disabled=1
			ctrl_interface=/var/run/wpa_supplicant
			dpp_config_processing=2
_
		wpa_supplicant -B -i $ref_vif_sta -c /tmp/wpas -dd -f /tmp/wpas.log -t
.
}

timeout_auth() {
	$dut <<. || return $?
		ovsh d DPP_Config
		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_ap0"]]' \
			configurator_key_hex~=$confhex \
			configurator_key_curve~=prime256v1 \
			configurator_conf_role~=sta-dpp-psk-sae \
			configurator_conf_psk_hex~=$pskhex \
			configurator_conf_ssid_hex~=$ssidhex \
			timeout_seconds:=10 \
			auth:=initiate_now \
			status:=requested \
			peer_bi_uri~="$uri"
		ovsh w DPP_Config -w auth==initiate_now status:=timed_out
.
}

timeout_respond() {
	$dut <<. || return $?
		ovsh d DPP_Config
		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_ap0"]]' \
			configurator_key_hex~=$confhex \
			configurator_key_curve~=prime256v1 \
			configurator_conf_role~=sta-dpp-psk-sae \
			configurator_conf_psk_hex~=$pskhex \
			configurator_conf_ssid_hex~=$ssidhex \
			timeout_seconds:=10 \
			auth:=respond_only \
			status:=requested \
			own_bi_key_curve~=prime256v1 \
			own_bi_key_hex~=$confhex
		ovsh w DPP_Config -w auth==respond_only status:=timed_out
.
}

timeout_chirp_tx() {
	$dut <<. || return $?
		ovsh d DPP_Config
		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_ap0"]]' \
			timeout_seconds:=10 \
			auth:=chirp_and_respond \
			status:=requested \
			own_bi_key_curve~=prime256v1 \
			own_bi_key_hex~=$confhex
		ovsh w DPP_Config -w auth==chirp_and_respond status:=timed_out
.
}

timeout_chirp_rx() {
	$dut <<. || return $?
		ovsh d DPP_Config
		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_ap0"]]' \
			configurator_key_hex~=$confhex \
			configurator_key_curve~=prime256v1 \
			configurator_conf_role~=sta-dpp-psk-sae \
			configurator_conf_psk_hex~=$pskhex \
			configurator_conf_ssid_hex~=$ssidhex \
			timeout_seconds:=10 \
			auth:=initiate_on_announce \
			status:=requested \
			peer_bi_uri~="$uri"
		ovsh w DPP_Config -w auth==initiate_on_announce status:=timed_out
.
}

announcement_ageout() {
	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
		wpa_cli -i $ref_vif_sta dpp_bootstrap_gen type=qrcode key="$urikeyhex"
.

	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta dpp_chirp own=1 iter=1
.

	$dut <<. || return $?
		ovsh d DPP_Announcement
		ovsh d DPP_Config
		ovsh w DPP_Announcement sta_mac_addr:=$ref_mac_sta -w chirp_sha256_hex=="$urikeychirp"
		sleep 60
		ovsh s DPP_Announcement | grep .
		sleep 30
		ovsh s DPP_Announcement | grep .
		sleep 40
		! ovsh s DPP_Announcement | grep .
.

	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta dpp_chirp own=1 iter=1
.

	$dut <<. || return $?
		ovsh w DPP_Announcement sta_mac_addr:=$ref_mac_sta -w chirp_sha256_hex=="$urikeychirp"
		sleep 60
		ovsh s DPP_Announcement | grep .
.

	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta dpp_chirp own=1 iter=1
.

	$dut <<. || return $?
		sleep 60
		ovsh s DPP_Announcement | grep .
		sleep 30
		ovsh s DPP_Announcement | grep .
		sleep 40
		! ovsh s DPP_Announcement | grep .
.
}

reset() {
	# After chirping AP seems to go silently down. At least
	# hwsim does. This needs investigation, but workaround for
	# the time being to unblock DPP testing bringup.
	$dut <<. || return $?
		for i in \$(ovsh -r s Wifi_VIF_Config if_name)
		do
			hostapd_cli -p /var/run/hostapd-\$(ovsh -r s Wifi_Radio_Config if_name) -i \$i disable
			hostapd_cli -p /var/run/hostapd-\$(ovsh -r s Wifi_Radio_Config if_name) -i \$i enable
		done
.
}

pair_auth() {
	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
.

	$dut <<. || return $?
		ovsh d DPP_Announcement
		ovsh d DPP_Config
		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_ap0"]]' \
			configurator_key_hex~=$confhex \
			configurator_key_curve:=prime256v1 \
			configurator_conf_role:=sta-$1 \
			configurator_conf_psk_hex~=$pskhex \
			configurator_conf_ssid_hex~=$ssidhex \
			timeout_seconds:=120 \
			auth:=initiate_now \
			status:=requested \
			peer_bi_uri:="$uri"

		ovsh w DPP_Config -w status==in_progress status:=in_progress
		ovsh s DPP_Config -c >&2
.


	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta dpp_bootstrap_gen type=qrcode key="$urikeyhex"
		wpa_cli -i $ref_vif_sta dpp_listen $dppfreq_sta
.

	$dut <<. || return $?
		ovsh w DPP_Config -w status==succeeded status:=succeeded
		ovsh s DPP_Config -c >&2

		if test "$2" = oftag-pre
		then
			ovsh i DPP_Oftag \
				sta_netaccesskey_sha256_hex:=\$(ovsh -r s DPP_Config sta_netaccesskey_sha256_hex) \
				oftag:=home--1
		fi

		ovsh w Wifi_Associated_Clients -w mac==$ref_mac_sta mac:=$ref_mac_sta
		ovsh s Wifi_Associated_Clients

		# The oftag-post verifies if WM can apply tags after a client
		# associates to account for desync/lag between device-cloud.
		if test "$2" = oftag-post
		then
			ovsh i DPP_Oftag \
				sta_netaccesskey_sha256_hex:=\$(ovsh -r s DPP_Config sta_netaccesskey_sha256_hex) \
				oftag:=home--1
		fi

		if test "$2" = oftag-pre || test "$2" = oftag-post
		then
			ovsh s Openflow_Tag
			ovsh w Openflow_Tag -w "device_value:inc:[\"set\",[\"$ref_mac_sta\"]]" name:=home--1
		fi
.
}

pair_respond() {
	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
.

	$dut <<. || return $?
		ovsh d DPP_Announcement
		ovsh d DPP_Config
		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_ap0"]]' \
			configurator_key_hex~=$confhex \
			configurator_key_curve~=prime256v1 \
			configurator_conf_role~=sta-$1 \
			configurator_conf_psk_hex~=$pskhex \
			configurator_conf_ssid_hex~=$ssidhex \
			timeout_seconds:=120 \
			auth:=respond_only \
			status:=requested \
			own_bi_key_curve~=prime256v1 \
			own_bi_key_hex~=$urikeyhex

		ovsh w DPP_Config -t 130000 -w status==in_progress status:=in_progress
		ovsh s DPP_Config -c >&2
.

	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta dpp_qr_code "$uri"
		wpa_cli -i $ref_vif_sta dpp_auth_init peer=1 role=enrollee
.

	$dut <<. || return $?
		ovsh w DPP_Config -w status==succeeded status:=succeeded
		ovsh s DPP_Config -c >&2
		ovsh w Wifi_Associated_Clients -w mac==$ref_mac_sta mac:=$ref_mac_sta
.
}

pair_respond_multi() {
	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
.

	$dut <<. || return $?
		ovsh d DPP_Announcement
		ovsh d DPP_Config

		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_ap0"]]' \
			configurator_key_hex~=$confhex \
			configurator_key_curve~=prime256v1 \
			configurator_conf_role~=sta-$1 \
			configurator_conf_psk_hex~=$pskhex \
			configurator_conf_ssid_hex~=$ssidhex \
			timeout_seconds:=120 \
			auth:=respond_only \
			status:=requested \
			own_bi_key_curve~=prime256v1 \
			own_bi_key_hex~=$urikeyhex

		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_ap0"]]' \
			configurator_key_hex~=$confhex \
			configurator_key_curve~=prime256v1 \
			configurator_conf_role~=sta-$1 \
			configurator_conf_psk_hex~=$pskhex \
			configurator_conf_ssid_hex~=$ssidhex \
			timeout_seconds:=120 \
			auth:=respond_only \
			status:=requested \
			own_bi_key_curve~=prime256v1 \
			own_bi_key_hex~=$urikey2hex

		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_ap0"]]' \
			configurator_key_hex~=$confhex \
			configurator_key_curve~=prime256v1 \
			configurator_conf_role~=sta-$1 \
			configurator_conf_psk_hex~=$pskhex \
			configurator_conf_ssid_hex~=$ssidhex \
			timeout_seconds:=120 \
			auth:=respond_only \
			status:=requested \
			own_bi_key_curve~=prime256v1 \
			own_bi_key_hex~=$urikey3hex

		ovsh w DPP_Config -t 10000 -w own_bi_key_hex==$urikeyhex status:=in_progress
		ovsh w DPP_Config -t 10000 -w own_bi_key_hex==$urikey2hex status:=in_progress
		ovsh w DPP_Config -t 10000 -w own_bi_key_hex==$urikey3hex status:=in_progress

		ovsh s DPP_Config -c >&2
.

	sleep 3
	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
		wpa_cli -i $ref_vif_sta dpp_qr_code "$uri"
		wpa_cli -i $ref_vif_sta dpp_auth_init peer=1 role=enrollee
.

	$dut <<. || return $?
		ovsh w DPP_Config -w own_bi_key_hex==$urikeyhex status:=succeeded
		ovsh s DPP_Config -c >&2
		ovsh w Wifi_Associated_Clients -w mac==$ref_mac_sta mac:=$ref_mac_sta
.

	sleep 3
	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
		wpa_cli -i $ref_vif_sta dpp_qr_code "$uri2"
		wpa_cli -i $ref_vif_sta dpp_auth_init peer=1 role=enrollee
.

	$dut <<. || return $?
		ovsh w DPP_Config -w status==succeeded status:=succeeded
		ovsh s DPP_Config -c >&2
		ovsh w DPP_Config -w own_bi_key_hex==$urikey2hex status:=succeeded
.

	sleep 3
	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
		wpa_cli -i $ref_vif_sta dpp_qr_code "$uri3"
		wpa_cli -i $ref_vif_sta dpp_auth_init peer=1 role=enrollee
.

	$dut <<. || return $?
		ovsh w DPP_Config -w status==succeeded status:=succeeded
		ovsh s DPP_Config -c >&2
		ovsh w DPP_Config -w own_bi_key_hex==$urikey3hex status:=succeeded
.
}

pair_respond_renew() {
	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
.

	$dut <<. || return $?
		ovsh d DPP_Announcement
		ovsh d DPP_Config
		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_ap0"]]' \
			configurator_key_hex~=$confhex \
			configurator_key_curve~=prime256v1 \
			configurator_conf_role~=sta-$1 \
			configurator_conf_psk_hex~=$pskhex \
			configurator_conf_ssid_hex~=$ssidhex \
			timeout_seconds:=120 \
			auth:=respond_only \
			status:=requested \
			renew:=true \
			own_bi_key_curve~=prime256v1 \
			own_bi_key_hex~=$urikeyhex

		ovsh w DPP_Config -t 130000 -w status==in_progress status:=in_progress
		ovsh s DPP_Config -c >&2
.

	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta dpp_qr_code "$uri"
		wpa_cli -i $ref_vif_sta dpp_auth_init peer=1 role=enrollee
.

	$dut <<. || return $?
		uuid=\$(ovsh -rU s DPP_Config _uuid -w own_bi_key_hex==$urikeyhex)
		ovsh w DPP_Config -w own_bi_key_hex==$urikeyhex status:=in_progress
		ovsh w DPP_Config -w "config_uuid==\$uuid" status:=succeeded
		ovsh s DPP_Config -c >&2
		ovsh w Wifi_Associated_Clients -w mac==$ref_mac_sta mac:=$ref_mac_sta
.
}

pair_respond_multi_renew() {
	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
.

	$dut <<. || return $?
		ovsh d DPP_Announcement
		ovsh d DPP_Config

		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_ap0"]]' \
			configurator_key_hex~=$confhex \
			configurator_key_curve~=prime256v1 \
			configurator_conf_role~=sta-$1 \
			configurator_conf_psk_hex~=$pskhex \
			configurator_conf_ssid_hex~=$ssidhex \
			timeout_seconds:=120 \
			auth:=respond_only \
			renew:=true \
			status:=requested \
			own_bi_key_curve~=prime256v1 \
			own_bi_key_hex~=$urikeyhex

		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_ap0"]]' \
			configurator_key_hex~=$confhex \
			configurator_key_curve~=prime256v1 \
			configurator_conf_role~=sta-$1 \
			configurator_conf_psk_hex~=$pskhex \
			configurator_conf_ssid_hex~=$ssidhex \
			timeout_seconds:=120 \
			auth:=respond_only \
			renew:=true \
			status:=requested \
			own_bi_key_curve~=prime256v1 \
			own_bi_key_hex~=$urikey2hex

		ovsh w DPP_Config -t 10000 -w own_bi_key_hex==$urikeyhex status:=in_progress
		ovsh w DPP_Config -t 10000 -w own_bi_key_hex==$urikey2hex status:=in_progress

		ovsh s DPP_Config -c >&2
.

	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
		wpa_cli -i $ref_vif_sta dpp_qr_code "$uri"
		wpa_cli -i $ref_vif_sta dpp_auth_init peer=1 role=enrollee
.

	$dut <<. || return $?
		uuid=\$(ovsh -rU s DPP_Config _uuid -w own_bi_key_hex==$urikeyhex)
		ovsh w DPP_Config -w own_bi_key_hex==$urikeyhex status:=in_progress
		ovsh w DPP_Config -w "config_uuid==\$uuid" status:=succeeded
		ovsh s DPP_Config -c >&2
		ovsh w Wifi_Associated_Clients -w mac==$ref_mac_sta mac:=$ref_mac_sta
		ovsh d DPP_Config -w status==succeeded
.

	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
		wpa_cli -i $ref_vif_sta dpp_qr_code "$uri"
		wpa_cli -i $ref_vif_sta dpp_auth_init peer=1 role=enrollee
.

	$dut <<. || return $?
		uuid=\$(ovsh -rU s DPP_Config _uuid -w own_bi_key_hex==$urikeyhex)
		ovsh w DPP_Config -w own_bi_key_hex==$urikeyhex status:=in_progress
		ovsh w DPP_Config -w "config_uuid==\$uuid" status:=succeeded
		ovsh s DPP_Config -c >&2
		ovsh w Wifi_Associated_Clients -w mac==$ref_mac_sta mac:=$ref_mac_sta
		ovsh d DPP_Config -w status==succeeded
.

	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
		wpa_cli -i $ref_vif_sta dpp_qr_code "$uri2"
		wpa_cli -i $ref_vif_sta dpp_auth_init peer=1 role=enrollee
.

	$dut <<. || return $?
		uuid=\$(ovsh -rU s DPP_Config _uuid -w own_bi_key_hex==$urikey2hex)
		ovsh w DPP_Config -w own_bi_key_hex==$urikey2hex status:=in_progress
		ovsh w DPP_Config -w "config_uuid==\$uuid" status:=succeeded
		ovsh s DPP_Config -c >&2
		ovsh w Wifi_Associated_Clients -w mac==$ref_mac_sta mac:=$ref_mac_sta
		ovsh d DPP_Config -w status==succeeded
.
}

pair_respond_multi_renew_auth_preempt() {
	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
.

	$dut <<. || return $?
		ovsh d DPP_Announcement
		ovsh d DPP_Config

		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_ap0"]]' \
			configurator_key_hex~=$confhex \
			configurator_key_curve~=prime256v1 \
			configurator_conf_role~=sta-$1 \
			configurator_conf_psk_hex~=$pskhex \
			configurator_conf_ssid_hex~=$ssidhex \
			timeout_seconds:=300 \
			auth:=respond_only \
			renew:=true \
			status:=requested \
			own_bi_key_curve~=prime256v1 \
			own_bi_key_hex~=$urikeyhex

		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_ap0"]]' \
			configurator_key_hex~=$confhex \
			configurator_key_curve~=prime256v1 \
			configurator_conf_role~=sta-$1 \
			configurator_conf_psk_hex~=$pskhex \
			configurator_conf_ssid_hex~=$ssidhex \
			timeout_seconds:=300 \
			auth:=respond_only \
			renew:=true \
			status:=requested \
			own_bi_key_curve~=prime256v1 \
			own_bi_key_hex~=$urikey2hex

		ovsh w DPP_Config -t 10000 -w own_bi_key_hex==$urikeyhex status:=in_progress
		ovsh w DPP_Config -t 10000 -w own_bi_key_hex==$urikey2hex status:=in_progress

		ovsh s DPP_Config -c >&2
.

	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
		wpa_cli -i $ref_vif_sta dpp_qr_code "$uri"
		wpa_cli -i $ref_vif_sta dpp_auth_init peer=1 role=enrollee
.

	$dut <<. || return $?
		uuid=\$(ovsh -rU s DPP_Config _uuid -w own_bi_key_hex==$urikeyhex)
		ovsh w DPP_Config -w own_bi_key_hex==$urikeyhex status:=in_progress
		ovsh w DPP_Config -w "config_uuid==\$uuid" status:=succeeded
		ovsh s DPP_Config -c >&2
		ovsh w Wifi_Associated_Clients -w mac==$ref_mac_sta mac:=$ref_mac_sta
		ovsh d DPP_Config -w status==succeeded
.

	$dut <<. || return $?
		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_ap0"]]' \
			configurator_key_hex~=$confhex \
			configurator_key_curve~=prime256v1 \
			configurator_conf_role~=sta-$1 \
			configurator_conf_psk_hex~=$pskhex \
			configurator_conf_ssid_hex~=$ssidhex \
			timeout_seconds:=120 \
			auth:=initiate_now \
			status:=requested \
			peer_bi_uri~="$uri3"

		ovsh w DPP_Config -t 10000 -w own_bi_key_hex==$urikeyhex status:=requested
		ovsh w DPP_Config -t 10000 -w own_bi_key_hex==$urikey2hex status:=requested
		ovsh w DPP_Config -t 10000 -w peer_bi_uri=="$uri3" status:=in_progress
.

	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
		wpa_cli -i $ref_vif_sta dpp_bootstrap_gen type=qrcode key="$urikey3hex"
		wpa_cli -i $ref_vif_sta dpp_listen $dppfreq_sta
.

	$dut <<. || return $?
		ovsh w DPP_Config -w peer_bi_uri=="$uri3" status:=succeeded
		ovsh s DPP_Config -c >&2
		ovsh w DPP_Config -t 10000 -w own_bi_key_hex==$urikeyhex status:=in_progress
		ovsh w DPP_Config -t 10000 -w own_bi_key_hex==$urikey2hex status:=in_progress
		ovsh d DPP_Config -w status==succeeded
.

	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
		wpa_cli -i $ref_vif_sta dpp_qr_code "$uri"
		wpa_cli -i $ref_vif_sta dpp_auth_init peer=1 role=enrollee
.

	$dut <<. || return $?
		uuid=\$(ovsh -rU s DPP_Config _uuid -w own_bi_key_hex==$urikeyhex)
		ovsh w DPP_Config -w own_bi_key_hex==$urikeyhex status:=in_progress
		ovsh w DPP_Config -w "config_uuid==\$uuid" status:=succeeded
		ovsh s DPP_Config -c >&2
		ovsh w Wifi_Associated_Clients -w mac==$ref_mac_sta mac:=$ref_mac_sta
		ovsh d DPP_Config -w status==succeeded
.
}


pair_chirp_rx() {
	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta disconnect
		wpa_cli -i $ref_vif_sta reconfigure
		wpa_cli -i $ref_vif_sta dpp_bootstrap_remove '*'
		wpa_cli -i $ref_vif_sta dpp_configurator_remove '*'
.

	$dut <<. || return $?
		ovsh d DPP_Announcement
		ovsh d DPP_Config
.

	$ref <<. || return $?
		wpa_cli -i $ref_vif_sta flush
		wpa_cli -i $ref_vif_sta dpp_bootstrap_gen type=qrcode key="$urikeyhex"
		wpa_cli -i $ref_vif_sta dpp_chirp own=1 iter=10
.

	$dut <<. || return $?
		ovsh w DPP_Announcement -w sta_mac_addr==$ref_mac_sta chirp_sha256_hex:=$urikeychirp

		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_ap0"]]' \
			configurator_key_hex~=$confhex \
			configurator_key_curve~=prime256v1 \
			configurator_conf_role~=sta-$1 \
			configurator_conf_psk_hex~=$pskhex \
			configurator_conf_ssid_hex~=$ssidhex \
			timeout_seconds:=120 \
			auth:=initiate_on_announce \
			status:=requested \
			peer_bi_uri~="$uri"

		ovsh w DPP_Config -t 130000 -w status==in_progress status:=in_progress
		ovsh s DPP_Config -c >&2

		ovsh w DPP_Config -t 130000 -w status==succeeded status:=succeeded
		ovsh s DPP_Config -c >&2

		ovsh w Wifi_Associated_Clients -w mac==$ref_mac_sta mac:=$ref_mac_sta
.
}

prepare_ref_ap() {
	$ref <<-. || return $?
		timeout 1 wpa_cli -p /var/run/wpa_supplicant -i $ref_vif_sta raw TERMINATE || true
		timeout 1 wpa_cli -p /var/run/hostapd -i $ref_vif_ap0 raw TERMINATE || true
		sleep 3

		cat <<-_ >/tmp/hapd
			ctrl_interface=/var/run/hostapd
			interface=$ref_vif_ap0
			hw_mode=g
			channel=$chan_ap
			ssid=$ssid
			wpa=2
			ieee80211w=1
			wpa_pairwise=CCMP
			rsn_pairwise=CCMP
			wpa_key_mgmt=WPA-PSK DPP
			wpa_passphrase=$psk
			dpp_connector=$dppconn
			dpp_csign=$dppcsign
			dpp_netaccesskey=$dppnet
_

		hostapd -B /tmp/hapd -f /tmp/hapd.log -dd
.
}

pair_chirp_tx() {
	conf=$(case "$1" in
	dpp-psk-sae) echo psk-sae-dpp ;;
	psk-sae) echo sae-dpp ;;
	*) echo $1 ;;
	esac)

	$ref <<. || return $?

		hostapd_cli -i $ref_vif_ap0 dpp_configurator_add key=$confhex
		hostapd_cli -i $ref_vif_ap0 set dpp_configurator_connectivity 1
		hostapd_cli -i $ref_vif_ap0 set dpp_configurator_params "conf=sta-$conf ssid=$ssidhex pass=$pskhex configurator=1"
		hostapd_cli -i $ref_vif_ap0 dpp_qr_code "$uri"
		hostapd_cli -i $ref_vif_ap0 dpp_listen $dppfreq_ap
.

	$dut <<. || return $?
		# echo -n "$urikeyhex" > /tmp/target_hwsim_dpp_key_hex

		ovsh d DPP_Config
		ovsh d Wifi_VIF_Config
		ovsh w Wifi_VIF_State -n -w if_name==$dut_vif_ap0 if_name:=$dut_vif_ap0
		ovsh w Wifi_VIF_State -n -w if_name==$dut_vif_ap1 if_name:=$dut_vif_ap1
		ovsh d Wifi_Radio_Config

		vif1=\$(ovsh -Ur i Wifi_VIF_Config \
			enabled:=true \
			if_name:=$dut_vif_sta \
			ssid:=ohi \
			'security::["map",[["oftag", "hello"], ["psk","foobarbaz"]]]' \
			mode:=sta
		)

		ovsh i Wifi_Radio_Config \
			enabled:=true \
			if_name:=$dut_phy \
			ht_mode:=HT20 \
			hw_mode:=11g \
			freq_band:=2.4G \
			channel:=$chan_sta \
			"vif_configs::[\"set\",[[\"uuid\",\"\$vif1\"]]]"

		ovsh i DPP_Config \
			'ifnames::["set", ["$dut_vif_sta"]]' \
			timeout_seconds:=120 \
			auth:=chirp_and_respond \
			status:=requested \
			own_bi_key_curve~=prime256v1 \
			own_bi_key_hex~=$urikeyhex

		ovsh w DPP_Config -t 130000 -w status==in_progress status:=in_progress
		ovsh s DPP_Config -c >&2

		ovsh w DPP_Config -t 130000 -w status==succeeded status:=succeeded
		ovsh s DPP_Config -c >&2

		sleep 30
		cat /var/run/*.config || true
		wpa_cli -p /var/run/wpa_supplicant-$dut_phy -i $dut_vif_sta status
		wpa_cli -p /var/run/wpa_supplicant-$dut_phy -i $dut_vif_sta scan_r
		wpa_cli -p /var/run/wpa_supplicant-$dut_phy -i $dut_vif_sta list_n

		ovsh w Wifi_VIF_State -w if_name==$dut_vif_sta parent:=$ref_mac_ap0
		case $1 in
		psk) ovsh s Wifi_VIF_State -w if_name==$dut_vif_sta wpa_key_mgmt | grep psk ;;
		dpp) ovsh s Wifi_VIF_State -w if_name==$dut_vif_sta wpa_key_mgmt | grep dpp ;;
		dpp-psk-sae) ovsh s Wifi_VIF_State -w if_name==$dut_vif_sta wpa_key_mgmt | awk '/dpp/ || /sae/ || /psk/' ;; # TODO: This could try cycling through?
		*) false ;;
		esac
		ovsh s Wifi_VIF_State -w if_name==$dut_vif_sta -c >&2
.
}

# FIXME: the conf roles could be verified with
# wpa_cli set_n 0 key_mgmt FOO to make sure given
# credentials (DPP or PSK or SAE) were given out
# and are usable. This is something that AFAIK is
# supported since DPP 1.2.

step() {
	name=${self}_$(echo "$*" | tr ' ' '_' | tr -dc a-z0-9_)

	if "$@"
	then
		echo "$name PASS" | tee -a "logs/$self/ret"
	else
		echo "$name FAIL" | tee -a "logs/$self/ret"
	fi
}

rm -f "logs/$self/ret"

base() {
	setchan 6 6
	step prepare_dut
	step prepare_ref_sta
	step timeout_auth
	step timeout_respond
	step timeout_chirp_tx
	step timeout_chirp_rx
	step reset
	step announcement_ageout
	step pair_auth dpp-psk-sae
	step pair_auth dpp
	step pair_auth dpp oftag-pre
	step pair_auth dpp oftag-post
	step pair_auth psk
	step pair_respond dpp-psk-sae
	step pair_respond dpp
	step pair_respond psk
	step pair_chirp_rx dpp-psk-sae
	step pair_chirp_rx dpp
	step pair_chirp_rx psk
	step prepare_ref_ap
	step pair_chirp_tx dpp-psk-sae
	step pair_chirp_tx dpp
	step pair_chirp_tx psk
}

offchan_auth() {
	setchan 1 11
	step prepare_dut
	step prepare_ref_sta
	step pair_auth psk offchan
}

nonstdchan_chirp_tx() {
	setchan 4 7
	step prepare_dut
	step prepare_ref_ap
	step pair_chirp_tx psk nonstdchan
}

nonstdchan_chirp_rx() {
	setchan 4 7
	step prepare_dut
	step prepare_ref_sta
	step pair_chirp_rx psk nonstdchan
}

multijob() {
	setchan 6 6
	step prepare_dut
	step prepare_ref_sta
	step pair_respond_multi psk
	step pair_respond_renew psk
	step pair_respond_multi_renew psk
	step pair_respond_multi_renew_auth_preempt psk
}

base
offchan_auth
nonstdchan_chirp_tx
nonstdchan_chirp_rx
multijob

timeout 1 wpa_cli -p /var/run/wpa_supplicant -i $ref_vif_sta raw TERMINATE || true
timeout 1 wpa_cli -p /var/run/hostapd -i $ref_vif_ap0 raw TERMINATE || true

cat "logs/$self/ret"
