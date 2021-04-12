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

#ifndef OS_UDS_LINK_INCLUDED
#define OS_UDS_LINK_INCLUDED

// Unix datagram socket link API (connectionless)

#include <stdbool.h>
#include <stdint.h>

#include <ev.h>
#include <sys/un.h>

typedef struct uds_link uds_link_t;

// Unix datagram structure
typedef struct udgram
{
    struct sockaddr_un addr; // socket address to read from / write to
    uint8_t *data; // datagram data buffer
    size_t size; // datagram size in bytes or max buffer size for blocking read
} udgram_t;

/**
 * @brief Read datagram function handler called every time new datagram is received
 * @param self ptr to uds link object invoking this handler
 * @param dg ptr to the structure containing read datagram with sender address 
 */
typedef void (*dgram_read_fp_t)(uds_link_t *self, const udgram_t *dg);

// Unix Datagram Socket link
typedef struct uds_link
{
    struct ev_loop *ev_loop;
    ev_io socket_watcher;

    int socket_fd;
    struct sockaddr_un socket_addr;

    dgram_read_fp_t dg_read_fp;

    const char *sname; // socket name = path
    bool abstract; // abstract namespace socket

    uint32_t cnt_tb; // transmitted bytes
    uint32_t cnt_tdg; // transmitted datagrams
    uint32_t cnt_rb; // received bytes
    uint32_t cnt_rdg; // received datagrams

} uds_link_t;

/**
 * @brief Inits Unix datagram socket link for datagram transfer. On failure
 * prints error messages in the log system.
 * @param self ptr to uds_link_t object to be initialized
 * @param name socket name (file path) with leading @ for abstract namespace socket
 * @param ev ptr to event loop to watch for receving datagrams, or NULL to use
 *           blocking uds_link_receive() for reception
 * @return true when link succesfully initialized; false otherwise (reason in log)
 */
bool uds_link_init(uds_link_t *self, const char *name, struct ev_loop *ev);

/**
 * @brief Destructs open unix datagram socket link
 * 
 * @param self ptr to uds link object
 */
void uds_link_fini(uds_link_t *self);

/**
 * @brief Gets configured socket name of this link
 * 
 * @param self ptr to uds link object
 * @return ptr to this link address structure 
 */
const char *uds_link_socket_name(uds_link_t *self);

/**
 * @brief (Un)Subscribes to read datagram received by this link
 * 
 * @param self ptr to uds link object
 * @param pfn ptr to read datagram function callback
 */
void uds_link_subscribe_datagram_read(uds_link_t *self, dgram_read_fp_t pfn);

/**
 * @brief Sends dgram to specified destination. Function may block when
 * there is no buffer for new dgram in the underlaying socket driver
 * 
 * @param self ptr to uds link object
 * @param dg ptr to filled structure with data and destination socket addr
 * @return true 'true' when dgram succesfully sent; 'false' otherwise
 */
bool uds_link_sendto(uds_link_t *self, const udgram_t *dg);

/**
 * @brief Blocking reception of new datagram. Allowed, only when event driven
 * reception was not set with ev param in uds_link_open().
 * 
 * Dgram structure shall contain ptr to buffer for receiving datagram and
 * size shall be set to max size of this buffer. On succesfull reception
 * buffer contains the message and dg size is updated to contain real length
 * of received datagram
 * 
 * @param self ptr to uds link object
 * @param dg ptr to datagram in & out
 * @return true when datagram succefully read; false on error
 */
bool uds_link_receive(uds_link_t *self, udgram_t *dg);

// Gets number of received datagrams
uint32_t uds_link_received_dgrams(uds_link_t *self);
// Gets number of received bytes
uint32_t uds_link_received_bytes(uds_link_t *self);
// Gets number of sent datagrams
uint32_t uds_link_sent_dgrams(uds_link_t *self);
// Gets number of sent bytes
uint32_t uds_link_sent_bytes(uds_link_t *self);

#endif
