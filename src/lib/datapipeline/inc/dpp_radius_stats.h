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

#ifndef DPP_RADIUS_STATS_H
#define DPP_RADIUS_STATS_H

/*
 * This is the intersection header of SM and DPP holding the common object definitions.
 * The common data object dpp_radius_stats_rec_t is typedefed:
 *
 *  1. to dppline_radius_stats_rec_t in dppline.c
 *  2. to sm_radius_stats_t          in sm.h
 *
 * If we'd want to enforce type-safety between the modules, we could use single-member structs.
 *
 * This way we can shave off some computing/mem-allocation by only allocating a record object
 * once and then pass around a pointer. Some extra care must be taken when doing it this way,
 * to not end up with memory leaks. For this reason the following "promises" are made:
 *
 *  1. When an entity transfers its ownership of a memory resource to another entity
 *     (by passing a pointer to the resource to the latter), it promises to not use
 *     the resource, unless the respective pointer is passed back to it again.
 *  2. When an entity owning the resource doesn't transfer the ownership it promises to
 *     properly free the resource (for example, by calling FREE).
 *  3. When an entity binds a resource to a non-standard object that needs to be released
 *     in a specific manner, it promises to provide means to do so without requiring the
 *     entity releasing the resource to know anything about the binding object.
 */

typedef struct dpp_radius_stats_rec
{
    char vif_name[16];
    char vif_role[64];
    char radiusAuthServerAddress[48];
    int  radiusAuthServerIndex;
    int  radiusAuthClientServerPortNumber;
    int  radiusAuthClientRoundTripTime;
    int  radiusAuthClientAccessRequests;
    int  radiusAuthClientAccessRetransmissions;
    int  radiusAuthClientAccessAccepts;
    int  radiusAuthClientAccessRejects;
    int  radiusAuthClientAccessChallenges;
    int  radiusAuthClientMalformedAccessResponses;
    int  radiusAuthClientBadAuthenticators;
    int  radiusAuthClientPendingRequests;
    int  radiusAuthClientTimeouts;
    int  radiusAuthClientUnknownTypes;
    int  radiusAuthClientPacketsDropped;

    /*
    * The function that allocated the data object provides the cleanup function.
    * As the data object pointer is passed, the ownership is transferred and the
    * last owner, that is, the function that does not transfer the ownership,
    * "promises" that it will call the cleanup function by passing to it the
    * pointer to the data object it inherited.
    */
    void (*cleanup)(struct dpp_radius_stats_rec*);
} dpp_radius_stats_rec_t;

typedef struct {
    dpp_radius_stats_rec_t **records;
    uint64_t timestamp;
    uint32_t qty;
} dpp_radius_stats_report_data_t;

#endif /* DPP_RADIUS_STATS_H */
