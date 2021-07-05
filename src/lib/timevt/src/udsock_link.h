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

#ifndef UDSOCK_LINK_INCLUDED
#define UDSOCK_LINK_INCLUDED

#include <ev.h>
#include "ipc_msg_link.h"

/**
 * @brief Creates UDS socket based link for transmission and reception
 * 
 * @param addr this socket address, to use abstract namespace socket precede addr with '@'
 * @param dst_addr default dest addr to be used if no addr is specified in sent message;
 * set to NULL to request always destination addr in ipc_msg_link_sendto()
 * @param evloop event loop to be used for message receive notifications and TX resending;
 * if unset (=NULL) then reception works in blocking mode, transmission buffering is still
 * available but w/o retransmission timeouts
 * @param max_msize maximal size of single message in bytes or 0 to use default
 * @param max_mcount maximal number of messages stored in tx buffer or 0 to disable TX bufferring (default socket IPC)
 * @return ptr to message link object OR NULL on failure
 */
ipc_msg_link_t *udsock_link_open(const char *addr, const char *dest_addr, struct ev_loop *evloop,
        size_t max_msize, size_t max_mcount);

#endif
