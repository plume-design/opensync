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

#ifndef UPNP_SERVER_H_INCLUDED
#define UPNP_SERVER_H_INCLUDED

/**
 * @file upnp_server.h
 * @brief OpenSync UPnP server oriented interface
 *
 */

/**
 * @brief Opaque type for upnp server object
 */
typedef struct upnp_server upnp_server_t;

/**
 * @brief UPNP server identifier
 */
enum upnp_srv_id
{
    UPNP_ID_WAN = 0, /**< Regular UPNP server for WAN main port */
    UPNP_ID_IPTV,    /**< UPNP server for dedicated IPTV port */
    // place for next idenifiers

    UPNP_ID_COUNT /**< Number of UPNP servers */
};

/**
 * @brief Creates new upnp server object of given id
 * 
 * @param id id of upnp server object to create
 * @return ptr to upnp server object or NULL on failue
 */
upnp_server_t *upnp_server_new(enum upnp_srv_id id);

/**
 * @brief Deletes existing upnp server object.
 * 
 * @param self ptr to upnp server object or NULL
 */
void upnp_server_del(upnp_server_t *self);

/**
 * @brief Attaches external ipv4 interface to upnp server. Only one external ipv4
 * interface may be attached. Interface must be detached to enable
 * another attachment. Attachment fails when server is already running.
 * 
 * @param self ptr to upnp server object
 * @param ifname persistent ptr to interface name string 
 * @return true on succesfull attachment
 * @return false when ifname is invalid or attachment failed (see above)
 */
bool upnp_server_attach_external(upnp_server_t *self, const char *ifname);

/**
 * @brief Attaches external ipv6 interface to upnp server. Only one external ipv6
 * interface may be attached. Interface must be detached to enable
 * another attachment. Attachment fails when server is already running.
 * 
 * @param self ptr to upnp server object
 * @param ifname persistent ptr to interface name string 
 * @return true on succesfull attachment
 * @return false when ifname is invalid or attachment failed (see above)
 */
bool upnp_server_attach_external6(upnp_server_t *self, const char *ifname);

/**
 * @brief Attaches internal interface to upnp server. Multiple internal interfaces
 * may be attached (implementation dependent). At least one internal interface 
 * shall be attached before starting the server. Attachment fails when server
 * is already running.
 * 
 * @param self ptr to upnp server object
 * @param ifname persistent ptr to interface name string
 * @return true on succesfull attachment
 * @return false when ifname is invalid or attachment failed (see above) 
 */
bool upnp_server_attach_internal(upnp_server_t *self, const char *ifname);

/**
 * @brief Detaches external or internal interface from upnp server.
 * Interface may be detached only when server is stopped.
 * 
 * @param self ptr to upnp server obejct
 * @param ifname ptr to interface name string
 * @return true when interface succefully detached, false otherwise
 */
bool upnp_server_detach(upnp_server_t *self, const char *ifname);

/**
 * @brief Starts upnp server. Before starting the server
 * internal and external interfaces must be attached.
 * 
 * @param self ptr to upnp server object
 * @return true when server started succesfully
 * @return false when server cannot start
 */
bool upnp_server_start(upnp_server_t *self);

/**
 * @brief Stops running upnp server
 * 
 * @param self ptr to upnp server object
 * @return true when server is stopped
 * @return false when server cannot be stopped
 */
bool upnp_server_stop(upnp_server_t *self);

/**
 * @brief Gets current status of upnp server process.
 * Does not reflect backdoor process kill/restart enabled by OS
 * 
 * @param self ptr to upnp server object
 * @return true when server was started
 * @return false when server was stopped
 */
bool upnp_server_started(const upnp_server_t *self);

#endif
