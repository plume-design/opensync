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

#ifndef OSP_BTN_H_INCLUDED
#define OSP_BTN_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>


/// @file
/// @brief Button API
///
/// @addtogroup OSP
/// @{


// ===========================================================================
//  Button API
// ===========================================================================

/// @defgroup OSP_BTN Button API
/// OpenSync Button API
/// @{

/**
 * Enumeration of buttons supported by OpenSync
 */
enum osp_btn_name
{
    OSP_BTN_NAME_RESET = (1 << 0),      /**< Factory reset button */
    OSP_BTN_NAME_WPS = (1 << 1),        /**< WiFi WPS button */

    /* More buttons can be added */
};

/**
 * Get the capabilities related to the buttons
 *
 * @param[out] caps Bitmask of buttons supported by the target
 *                  You can test if a button is supported by testing the bitmask
 *                  For example, to test if the reset button is supported by the
 *                  target, you can test (caps & @ref OSP_BTN_NAME_RESET)
 *
 * @return true on success
 */
bool osp_btn_get_caps(uint32_t *caps);

/**
 * Definition of an event associated with a button
 *
 * Example 1: Button is pushed
 *            - pushed = true
 *            - duration = 0
 *            - double_click = false
 *
 * Example 2: Button is double click
 *            - pushed = false
 *            - duration = 0
 *            - double_click = true
 *
 * Example 3: Button is released after 1 second
 *            - pushed = false
 *            - duration = 1000
 *            - double_click = false
 *
 * Example 4: Button is released after 5 seconds
 *            - pushed = false
 *            - duration = 5000
 *            - double_click = false
 */
struct osp_btn_event
{
    /**
     * True if the button is pushed, false if the button is released
     */
    bool pushed;

    /**
     * Duration in milliseconds of pressing the button
     *
     * Valid only when the button is released and it was not a double click.
     */
    unsigned int duration;

    /**
     * True if the button is pushed and released two times in less than
     * 1000 milliseconds
     *
     * Valid only when the button is released.
     */
    bool double_click;
};

/**
 * Callback called by the target layer when an event is received on a button
 *
 * @param[in] obj    Pointer to the object that was supplied when the callback
 *                   was registered (@ref osp_btn_register call)
 * @param[in] name   Button associated with the event
 * @param[in] event  Details of the button event
 */
typedef void (*osp_btn_cb)(void *obj, enum osp_btn_name name, const struct osp_btn_event *event);

/**
 * Register the callback to receive button events
 *
 * @param[in] cb  Callback called by the target layer when an event is received
 *                on a button.
 *                If callback is NULL, the target must unregister the previous
 *                one for this specific obj.
 * @param[in] obj User pointer which will be given back when the callback will
 *                be called
 *
 * @return true on success
 */
bool osp_btn_register(osp_btn_cb cb, void *obj);


/// @} OSP_BTN
/// @} OSP

#endif /* OSP_BTN_H_INCLUDED */
