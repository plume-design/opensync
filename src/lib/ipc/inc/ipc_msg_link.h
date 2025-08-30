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

#ifndef OS_IPC_MSG_INCLUDE
#define OS_IPC_MSG_INCLUDE

/* This is a unified interface for IPC message based connectionless 
 * communication link */

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

/* This switch enables abstract class interface for dynamic polymorphism */
#ifdef IPC_MSG_LINK_ABSTRACT
#define IPC_MSG_LINK_STORAGE static
#else /* static (link-time) polymorphism */
#define IPC_MSG_LINK_STORAGE extern
#endif

/**
 * @brief IPC message based connectionless link object
 */
typedef struct ipc_msg_link ipc_msg_link_t;

/**
 * @brief Constructs IPC message-based connectionless link
 * @param addr generic link address string or NULL to use default based on link_id
 * @param data optional user-supplied data (implementation dependent)
 * @param link_id link ID (constant defining object type to be created : factory pattern)
 * @return handle to created link or NULL on failure
 */
ipc_msg_link_t *ipc_msg_link_open(const char *addr, void *data, int link_id);

/**
 * @brief Destructs message link object, releases used resources
 * @param self ptr to object to be destroyed
 */
IPC_MSG_LINK_STORAGE void ipc_msg_link_close(ipc_msg_link_t *self);

/**
 * @brief Gets IPC message link object address
 * @param self ptr to ipc msg link object
 * @return ptr to persistent address string
 */
IPC_MSG_LINK_STORAGE const char *ipc_msg_link_addr(const ipc_msg_link_t *self);

/**
 * @brief Gets message link sending capability
 * @param self ptr to ipc msg link object
 * @return true when link can send messages; false otherwise
 */
IPC_MSG_LINK_STORAGE bool ipc_msg_link_can_send(const ipc_msg_link_t *self);

/**
 * @brief Gets message link receiving capability
 * @param self ptr to ipc msg link object
 * @return true when link can receive messages; false otherwise
 */
IPC_MSG_LINK_STORAGE bool ipc_msg_link_can_receive(const ipc_msg_link_t *self);

/**
 * @brief Gets maximal size of the message supported by this link
 * 
 * @param self ptr to ipc msg link object
 * @return max message size in bytes
 */
IPC_MSG_LINK_STORAGE size_t ipc_msg_link_max_msize(const ipc_msg_link_t *self);

// Defines max length of an address
#define IPC_MAX_ADDR_LEN 255

typedef struct ipc_msg
{
    char *addr; // optional sender addr (on receiver site) / destination addr (sender site), max size=IPC_MAX_ADDR_LEN
    size_t size; // size of the payload in data buffer in bytes; never exceeds ipc_msg_link_max_msize()
    uint8_t *data; // payload data buffer
} ipc_msg_t;

/**
 * @brief Received message function handler called every time new message is received
 * @param self ptr to IPC message link object invoking this handler
 * @param subscr ptr to subscriber of this event
 * @param dg ptr to the structure containing read message with sender address
 */
typedef void ipc_msg_received_func(ipc_msg_link_t *self, void *subscr, const ipc_msg_t *msg);

IPC_MSG_LINK_STORAGE bool ipc_msg_link_subscribe_receive(ipc_msg_link_t *self, void *subscr, ipc_msg_received_func *pfn);

/**
 * @brief Sends message to specified destination. Function may block when
 * there is no space for new message in the underlying driver
 * 
 * @param self ptr to message link object
 * @param msg ptr to message with data and optional destination addr
 * @return true 'true' when message successfully sent; 'false' otherwise
 */
IPC_MSG_LINK_STORAGE bool ipc_msg_link_sendto(ipc_msg_link_t *self, const ipc_msg_t *msg);

/**
 * @brief Blocking reception of new message.
 * Note: use only if you don't subscribe for receive event
 * 
 * Message structure shall contain ptr to buffer for receiving message and
 * size shall be set to max size of this buffer. On successful reception
 * buffer contains the message and size is updated to contain real length
 * of received message.
 * If the caller want to have an address of the sender, it shall provide
 * addr buffer of IPC_MAX_ADDR_LEN size. When addr is available this buffer
 * is filled with addr, otherwise zero-length string is returned
 * 
 * @param self ptr to ipc message link object
 * @param msg ptr to msg buffer in & out
 * @return true when message successfully read; false on error
 */
IPC_MSG_LINK_STORAGE bool ipc_msg_link_receive(ipc_msg_link_t *self, ipc_msg_t *msg);

#ifdef IPC_MSG_LINK_ABSTRACT

/* Opaque message link type for implementation */
typedef struct ipc_msg_link_impl ipc_msg_link_impl_t;

/* IPC msg link : virtual function table for dynamic polymorphism 
 * All methods follow the specification from above declarations */
typedef struct ipc_msg_link_VFT
{
    void (*close)(ipc_msg_link_impl_t *self);
    
    const char *(*get_addr)(const ipc_msg_link_impl_t *self);
    bool (*can_send)(const ipc_msg_link_impl_t *self);
    bool (*can_receive)(const ipc_msg_link_impl_t *self);
    size_t (*get_max_msize)(const ipc_msg_link_impl_t *self);

    bool (*subscribe_receive)(ipc_msg_link_impl_t *self, void *subscr, ipc_msg_received_func *pfn);
    bool (*sendto)(ipc_msg_link_impl_t *self, const ipc_msg_t *msg);
    bool (*receive)(ipc_msg_link_impl_t *self, ipc_msg_t *msg);

} ipc_msg_link_vft_t;

/* IPC msg link abstract class interface */
struct ipc_msg_link
{
    const ipc_msg_link_vft_t *pVFT;
    ipc_msg_link_impl_t *p_impl;
};

/* Set of one-liners for easy calls with use of VFT */

static inline void ipc_msg_link_close(ipc_msg_link_t *self)
{ self->pVFT->close(self->p_impl); }
static inline const char *ipc_msg_link_addr(const ipc_msg_link_t *self)
{ return self->pVFT->get_addr(self->p_impl); }
static inline bool ipc_msg_link_can_send(const ipc_msg_link_t *self)
{ return self->pVFT->can_send(self->p_impl); }
static inline bool ipc_msg_link_can_receive(const ipc_msg_link_t *self)
{ return self->pVFT->can_receive(self->p_impl); }
static inline size_t ipc_msg_link_max_msize(const ipc_msg_link_t *self)
{ return self->pVFT->get_max_msize(self->p_impl); }
static inline bool ipc_msg_link_subscribe_receive(ipc_msg_link_t *self, void *subscr, ipc_msg_received_func *pfn)
{ return self->pVFT->subscribe_receive(self->p_impl, subscr, pfn); }
static inline bool ipc_msg_link_sendto(ipc_msg_link_t *self, const ipc_msg_t *msg)
{ return self->pVFT->sendto(self->p_impl, msg); }
static inline bool ipc_msg_link_receive(ipc_msg_link_t *self, ipc_msg_t *msg)
{ return self->pVFT->receive(self->p_impl, msg); }

#endif // IPC_MSG_LINK_ABSTRACT

#endif
