/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef WM2_DPP_H_INCLUDED
#define WM2_DPP_H_INCLUDED

/**
 * Checks if a DPP activity takes place on given interface
 * name. If interface name is omitted (=NULL), then DPP
 * activity on any interface is considered.
 */
bool
wm2_dpp_in_progress(const char *ifname);

/**
 * Allows checking if the current DPP auth job involves
 * chipring. This is intended to be used to identify if the
 * system is trying to onboard.
 */
bool
wm2_dpp_is_chirping(void);

/**
 * Marks current DPP activity as needing to be restarted
 * instead of timed out or cancelled. This schedules a DPP
 * recalculation.
 */
void
wm2_dpp_interrupt(void);

/**
 * Marks given interface as either ready, or not. This is
 * for DPP job dependency on ifnames[] they are intended to
 * be run for. Typically wm2_dpp_interrupt() is expected to
 * be called afterwards.
 */
void
wm2_dpp_ifname_set(const char *ifname, bool enabled);

/**
 * Allows inferring what oftag should be used for given net
 * access key. This is used to handle clients connecting
 * with DPP AKM.
 */
bool
wm2_dpp_key_to_oftag(const char *key, char *oftag, int size);

/**
 * Callback for target_radio_ops. Processes DPP Announcement
 * events from target.
 */
void
wm2_dpp_op_announcement(const struct target_dpp_chirp_obj *c);

/**
 * Callback for target_radio_ops. Processes DPP
 * Configuration object events from target. These happen
 * after DPP Authentication, during DPP Configuration.
 */
void
wm2_dpp_op_conf_enrollee(const struct target_dpp_conf_enrollee *c);

/**
 * Callback for target_radio_ops. Processes DPP
 * Configuration object events from target. These happen
 * after DPP Authentication, during DPP Configuration.
 */
void
wm2_dpp_op_conf_network(const struct target_dpp_conf_network *c);

/**
 * Callback for target_radio_ops. Processes DPP
 * Configuration object events from target. These happen
 * after DPP Authentication, during DPP Configuration.
 */
void
wm2_dpp_op_conf_failed(void);

/**
 * Initializes internal structures for WM2's DPP logic.
 */
void
wm2_dpp_init(void);

#endif /* WM2_DPP_H_INCLUDED */
