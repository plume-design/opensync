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

#ifndef OSN_TC_H_INCLUDED
#define OSN_TC_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

/**
 * @file osn_tc.h
 * @brief OpenSync TC
 *
 * @addtogroup OSN
 * @{
 */

/*
 * ===========================================================================
 *  TC Configuration API
 * ===========================================================================
 */

/**
 * @defgroup OSN_TC TC
 *
 * OpenSync TC API
 *
 * @{
 */

/**
 * OSN TC object type
 *
 * This is an opaque type. The actual structure implementation is hidden and is
 * platform dependent. A new instance of the object can be obtained by calling
 * @ref osn_tc_new() and must be destroyed using @ref osn_tc_del().
 */
typedef struct osn_tc osn_tc_t;

/**
 * Create new TC object for interface @p ifname.
 *
 * @param[in]   ifname  Interface that will be subject to TC configuration
 *
 * @return
 * This function returns NULL if an error occurs, otherwise a valid @ref
 * osn_tc_t object is returned.
 */
osn_tc_t* osn_tc_new(const char *ifname);

/**
 * Delete TC object and any related resources
 *
 * @param[in]   tc     A valid pointer to an osn_tc_t object
 */
void osn_tc_del(osn_tc_t *tc);

/**
 * Apply the TC configuration to the system
 *
 * @param[in]   tc     A valid pointer to an osn_tc_t object
 *
 * @return
 * This function returns true if the action was successful, false otherwise.
 */
bool osn_tc_apply(osn_tc_t *tc);

/**
 * Begin a TC discipline definition
 *
 * This function starts a TC discipline definition. For disciplines that
 * support multiple queues, they may be defined between a @p osn_tc_begin() and
 * @p osn_tc_end() functions.
 *
 * If called between a @p osn_tc_filter_begin() and @p osn_tc_filter_end() and
 * if the parent discipline supports it, the discipline is attached to the
 * parent queue.
 *
 * Disciplines cannot be nested directly, but must be attached to other queues.
 *
 * @param[in]   tc             A valid pointer to an osn_tc_t object
 *
 * @return
 * This function returns true on success, false otherwise.
 */
bool osn_tc_begin(osn_tc_t *tc);

/**
 * End a TC discipline definition
 *
 * @return
 * This function returns true on success, false otherwise.
 */
bool osn_tc_end(osn_tc_t *tc);

/**
 * Start a new TC filter definition.
 *
 * For disciplines that support queue nesting, subordinate queues may be
 * defined between a @p osn_tc_queue_begin() and @p osn_tc_filter_end()
 * function calls.
 *
 * TC disciplines can be attached to filters, if the current TC discipline
 * supports it.
 *
 * @param[in]   tc             A valid pointer to an osn_tc_t object.
 *
 * @param[in]   priority       Queue priority or a negative value if not
 *                             present.
 *
 * @param[in]   ingress        Direction of the packet to match on.
 *
 * @param[in]   match          Match string that is used to specify the
 *                             parameters of the packet to filter on.
 *
 * @param[in]   action         Action to be taken on the matched packet.
 *
 *
 * @return
 * This function returns true if successful, false otherwise.
 */
bool osn_tc_filter_begin(
        osn_tc_t *self,
        int   priority,
        bool  ingress,
        char  *match,
        char  *action);
/**
 * End a TC queue definition
 *
 * @return
 * This function returns true on success, false otherwise.
 */
bool osn_tc_filter_end(osn_tc_t *tc);

/** @} OSN_TC */
/** @} OSN */

#endif /* OSN_TC_H_INCLUDED */
