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

#ifndef OMS_REPORT_H_INCLUDED
#define OMS_REPORT_H_INCLUDED

/**
 * @brief Container of protobuf serialization output
 *
 * Contains the information related to a serialized protobuf
 */
struct packed_buffer
{
    size_t len;    /*<! Length of the serialized protobuf */
    void *buf;     /*<! Dynamically allocataed pointer to serialied data */
};


/**
 * @brief Frees the pointer to serialized data and container
 *
 * Frees the dynamically allocated pointer to serialized data (pb->buf)
 * and the container (pb).
 *
 * @param pb a pointer to a serialized data container
 * @return none
 */
void
oms_report_free_packed_buffer(struct packed_buffer *pb);

/**
 * @brief Generates a flow report serialized protobuf
 *
 * Uses the information pointed by the report parameter to generate
 * a serialized flow report buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see oms_report_free_packed_buffer() for this purpose.
 *
 * @param node info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer *
oms_report_serialize_report(void);


bool oms_report_send_report(char *mqtt_topic);

#endif /* OMS_REPORT_H_INCLUDED */
