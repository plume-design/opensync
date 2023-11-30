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

#ifndef OSBUS_DATA_UBUS_H_INCLUDED
#define OSBUS_DATA_UBUS_H_INCLUDED

#include <libubox/blobmsg.h>
#include "osbus_msg.h"

enum blobmsg_type osbus_msg_type_to_blobmsg_type(osbus_msg_type type);
osbus_msg_type osbus_msg_type_from_blobmsg_type(enum blobmsg_type type);
bool osbus_msg_to_blob_buf(const osbus_msg_t *data, struct blob_buf **bb);
bool osbus_msg_from_blob_buf(osbus_msg_t **data, struct blob_buf *bb);
bool osbus_msg_from_blob_attr(osbus_msg_t **data, struct blob_attr *attr);
void osbus_msg_free_blob_buf(struct blob_buf *b);

#endif /* OSBUS_DATA_UBUS_H_INCLUDED */

