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

#ifndef FSM_UTILS_H_INCLUDED
#define FSM_UTILS_H_INCLUDED

#include <netinet/in.h>
#include <stdbool.h>

/**
 * @brief Compares 2 structures containing a generic IP address
 *
 * @param a pointer to one IP address
 * @param b pointer the other IP address
 *
 * @return true if both addresses match
 */
bool sockaddr_storage_equals(struct sockaddr_storage *a, struct sockaddr_storage *b);

/**
 * @brief Compares 2 structures containing a generic IP address
 *
 * @param a pointer to one IP address
 * @param ip_bytes binary representation of the IP address to compare
 * @param len length of the ip_bytes array
 *
 * @return true if both addresses match
 */
bool sockaddr_storage_equals_addr(struct sockaddr_storage *a, uint8_t *ip_bytes, size_t len);

/**
 * @brief Allocates and populates a generic structure for an IP address
 *
 * Function will allocate and populate a generic structure based on
 * the textual IP address passed as argument
 *
 * @param af the address family (AF_INET or AF_INET6)
 * @param ip_str the textual representation of the IP address
 *
 * @return pointer to allocated structure, or NULL in case of an error
 * @remark Caller is responsible to release the memory allocated for
 *         the sockaddr_storage structure.
 */
struct sockaddr_storage *sockaddr_storage_create(int af, char *ip_str);

/**
 * @brief Populates a generic structure for an IP address
 *
 * Function will populate a generic structure based on
 * the AF family and IP address passed as argument
 *
 * @param af the address family (AF_INET or AF_INET6)
 * @param ip the binary representation of the IP address
 * @param dst the populated structure
 *
 * @remark Caller is responsible allocate the referenced destination.
 */
void sockaddr_storage_populate(int af, void *ip, struct sockaddr_storage *dst);

/**
 * @brief Copies a generic structure into another
 *
 *
 * @param to the generic structure to be copied
 * @param from the destination structure
 *
 * @remark Caller is responsible allocate the referenced destination.
 */
void sockaddr_storage_copy(struct sockaddr_storage *from, struct sockaddr_storage *to);

/**
 * @brief Creates string from a generic structure for an IP address
 *
 * @param af the address family (AF_INET or AF_INET6)
 * @param ip_str the textual representation of the IP address
 *
 * @remark Caller is responsible allocate the referenced destination string.
 */
void sockaddr_storage_str(struct sockaddr_storage *addr, char *output);

#endif /* FSM_UTILS_H_INCLUDED */
