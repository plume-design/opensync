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
 * Band Steering Manager - Kicking Logic
 */

#ifndef __BM_KICK_H__
#define __BM_KICK_H__

#define BM_KICK_MAGIC_NUMBER            1
#define BM_KICK_MAGIC_DEBOUNCE_PERIOD   1

/*****************************************************************************/
typedef enum {
    BM_STEERING_KICK = 0,
    BM_STICKY_KICK,
    BM_FORCE_KICK
} bm_kick_type_t;


/*****************************************************************************/
extern bool             bm_kick_init(void);
extern bool             bm_kick_cleanup(void);
extern bool             bm_kick_cleanup_by_bsal(bsal_t bsal);
extern bool             bm_kick_cleanup_by_client(bm_client_t *client);
extern bool             bm_kick(bm_client_t *client, bm_kick_type_t type, uint8_t rssi);
extern void             bm_kick_measurement(os_macaddr_t macaddr, uint8_t rssi);

#endif /* __BM_KICK_H__ */
