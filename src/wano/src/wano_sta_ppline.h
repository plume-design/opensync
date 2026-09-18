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

/*
 * WANO STA pipeline
 *
 * Minimal replacement for the generic WANO plug-in pipeline (wano_ppline)
 * for STA backup uplinks. It DHCP-configures the STA uplink interface
 * (Wifi_Inet_Config), installs the STA uplink policy routing rules and
 * evaluates the uplink L2/L3 state from Wifi_Master_State/Wifi_Inet_State.
 * When the link is up it inserts the Connection_Manager_Uplink row for the
 * interface (unless this is a test connection) and reports state transitions
 * through an optional event callback.
 *
 */
#ifndef WANO_STA_PPLINE_H_INCLUDED
#define WANO_STA_PPLINE_H_INCLUDED

#include <stdbool.h>

typedef struct wano_sta_ppline wano_sta_ppline_t;

enum wano_sta_ppline_event
{
    WANO_STA_PPLINE_L2_UP,
    WANO_STA_PPLINE_L3_UP,
    WANO_STA_PPLINE_CONN_CHECK_OK,
    WANO_STA_PPLINE_CONN_CHECK_NOK,
    WANO_STA_PPLINE_L3_DOWN,
    WANO_STA_PPLINE_L2_DOWN,
};

struct wano_sta_ppline_status
{
    bool has_L2;
    bool has_L3;
    bool conn_check_ok;
};

typedef void wano_sta_ppline_event_cb_fn_t(
        wano_sta_ppline_t *ppline,
        enum wano_sta_ppline_event event,
        struct wano_sta_ppline_status status);

/*
 * Create a new wano_sta_ppline object.
 * Returns NULL if a pipeline for `if_name` already exists.
 */
wano_sta_ppline_t *wano_sta_ppline_new(const char *if_name, const char *if_type);

/* Register an optional event reporting callback. Call before _start(). */
void wano_sta_ppline_event_cb_init(wano_sta_ppline_t *ppline, wano_sta_ppline_event_cb_fn_t *cb);

/*
 * Set the Wifi_Inet_Config:role value to be configured verbatim on the STA
 * uplink interface when the pipeline starts. Call before _start().
 *
 * This is needed so that the interface stats start collecting immediately
 * after we bring up the STA uplink, not waiting for controller connection
 * to be established again.
 */
void wano_sta_ppline_set_inet_role(wano_sta_ppline_t *ppline, const char *inet_role);

/*
 * Mark this pipeline as a "test connection": the uplink is brought up and its
 * connectivity state reported, but the device never switches default traffic to it,
 * and no CMU row is inserted.
 *
 * Call it before _start(); the default is false.
 */
void wano_sta_ppline_set_test_connection(wano_sta_ppline_t *ppline, bool test_connection);

/*
 * Start the pipeline: configure DHCP on the interface,
 * install STA uplink policy routing rules and start
 * evaluating the uplink state.
 */
bool wano_sta_ppline_start(wano_sta_ppline_t *ppline);

/*
 * Stop the pipeline: remove the CMU row, revert the
 * interface DHCP configuration and remove the policy routing rules
 * (when this is the last active pipeline).
 */
void wano_sta_ppline_stop(wano_sta_ppline_t *ppline);

/* Unregister the pipeline (stopping it first if active) and free it. */
void wano_sta_ppline_del(wano_sta_ppline_t *ppline);

/* Get the interface name associated with the pipeline. */
const char *wano_sta_ppline_if_name_get(const wano_sta_ppline_t *ppline);

/* Get a string representation of a wano_sta_ppline event. */
const char *wano_sta_ppline_event_str(enum wano_sta_ppline_event event);

#endif /* WANO_STA_PPLINE_H_INCLUDED */
