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

UNIT_NAME := fut_wm

UNIT_DISABLE := n

# Template type:
UNIT_TYPE := FUT
# Output directory
UNIT_DIR := shell/tests/wm

UNIT_FILE := wm2_setup.sh
UNIT_FILE += wm2_dfs_cac_aborted.sh
UNIT_FILE += wm2_ht_mode_and_channel_iteration.sh
UNIT_FILE += wm2_immutable_radio_freq_band.sh
UNIT_FILE += wm2_immutable_radio_hw_mode.sh
UNIT_FILE += wm2_immutable_radio_hw_type.sh
UNIT_FILE += wm2_immutable_radio_if_name.sh
UNIT_FILE += wm2_set_bcn_int.sh
UNIT_FILE += wm2_set_channel.sh
UNIT_FILE += wm2_set_ht_mode.sh
UNIT_FILE += wm2_set_radio_country.sh
UNIT_FILE += wm2_set_radio_enabled.sh
UNIT_FILE += wm2_set_radio_fallback_parents.sh
UNIT_FILE += wm2_set_radio_thermal_tx_chainmask.sh
UNIT_FILE += wm2_set_radio_tx_chainmask.sh
UNIT_FILE += wm2_set_radio_tx_power.sh
UNIT_FILE += wm2_set_radio_vif_configs.sh
