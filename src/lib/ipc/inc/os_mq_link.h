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

#ifndef OS_MQ_LINK_INCLUDED
#define OS_MQ_LINK_INCLUDED

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include <ev.h>

/**
 * @brief Message queue (MQ) connectionless link object
 */
typedef struct mq_link mq_link_t;

/**
 * @brief Defines MQ message priority type
 */
typedef unsigned int mq_priority_t;
#define MQ_PRI_MIN 0  //< the lowest message queue priority
#define MQ_PRI_MAX 31 //< the highest message queue priority

// MQ link open mode
enum mq_link_mode
{
    MQ_MODE_RECV = (1<<0),  //< receive only mode
    MQ_MODE_SEND = (1<<1),  //< send only mode
    MQ_MODE_SEND_RECV = (MQ_MODE_RECV | MQ_MODE_SEND) //< receive & send mode
};

/**
 * @brief Creates MQ based link for transmission and reception
 * 
 * @param name MQ name (usually file name in /dev/mqueue starting with ./)
 * @param evloop event loop to be used for message receive notifications
 * @param mode MQ link open mode
 * @param max_msize maximal size of single message in bytes or 0 to use default
 * @param max_mcount maximal number of messages stored in the kernel buffer or 0 to use default
 * @return ptr to MQ object OR NULL on failure
 */
mq_link_t *mq_link_open(const char *name, struct ev_loop *evloop, 
        enum mq_link_mode mode, size_t max_msize, size_t max_mcount);

/**
 * @brief Destructs MQ link object, releases used resources
 * @param self ptr to object to be destroyed
 * @param unlink unlink the queue, it will be destroyed when last reference to
 * this queue is closed. New created links will not be able to access this 
 * message queue in the kernel.
 */
void mq_link_close(mq_link_t *self, bool unlink);

/**
 * @brief Gets MQ name
 * @param self ptr to msg queue link object
 * @return ptr to persistent name string
 */
const char *mq_link_name(const mq_link_t *self);

/**
 * @brief Gets MQ sending capability
 * @param self ptr to ipc msg link object
 * @return true when link can send messages; false otherwise
 */
bool mq_link_can_send(const mq_link_t *self);

/**
 * @brief Gets MQ receiving capability
 * @param self ptr to ipc msg link object
 * @return true when link can receive messages; false otherwise
 */
bool mq_link_can_receive(const mq_link_t *self);

/**
 * @brief Gets maximal size of the message supported by this link
 * 
 * @param self ptr to MQ link object
 * @return max message size in bytes
 */
size_t mq_link_max_msize(const mq_link_t *self);

/**
 * @brief Received message function type called every time new message is received
 * @param self ptr to MQ link invoking this handler
 * @param subscr ptr to subscriber of this event
 * @param msg ptr to the local buffer with received message
 * @param mlen length of the message in bytes
 * @param mpri received message priority
 */
typedef void mq_msg_received_func_t(mq_link_t *self, void *subscr, const uint8_t *msg, size_t mlen, mq_priority_t mpri);

/**
 * @brief Subscribes / unsubscribes for reception of new MQ message
 * Note: only object with mq_link_can_receive() capability and event loop can
 * can subscribe for message reception
 * 
 * @param self ptr to MQ link object
 * @param subscr ptr to subscriber object
 * @param pfn ptr to call handler function or NULL when unsubscribing
 * @return subscription result
 */
bool mq_link_subscribe_receive(mq_link_t *self, void *subscr, mq_msg_received_func_t *pfn);

/**
 * @brief Sends message to specified destination. Function may block when
 * there is no space for new message in the underlying driver
 * 
 * @param self ptr to MQ object
 * @param msg ptr to buffer with message to send
 * @param mlen message length in bytes
 * @param mpri MQ message priority in MQ_PRI_MIN..MQ_PRI_MAX range
 * @return true 'true' when message successfully sent; 'false' otherwise
 */
bool mq_link_sendto(mq_link_t *self, const uint8_t *msg, size_t mlen, mq_priority_t mpri);

/**
 * @brief Blocking reception of new message.
 * Note: use only if you don't subscribe for receive event
 * 
 * Message structure shall contain ptr to buffer for receiving message and
 * size shall be set to max size of this buffer. On successful reception
 * buffer contains the message and size is updated to contain real length
 * of received message.
 * 
 * @param self ptr to MQ link object
 * @param msg ptr to msg buffer in & out
 * @param p_mlen ptr to message buffer size (in) / message length (out)
 * @param p_mpri ptr to message priority field (out) or NULL
 * @return true when message successfully read; false on error
 */
bool mq_link_receive(mq_link_t *self, uint8_t *msg, size_t *p_mlen, mq_priority_t *p_mpri);

#endif
