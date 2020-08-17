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

#define PJS_LOCALCONFIG_PPPOE                                   \
    PJS(wano_localconfig_pppoe,                                 \
            PJS_BOOL(enabled)                                   \
            PJS_STRING(username, 256)                           \
            PJS_STRING(password, 256))                          \

#define PJS_LOCALCONFIG_DATASERVICE                             \
    PJS(wano_localconfig_dataservice,                           \
            PJS_BOOL(enabled)                                   \
            PJS_INT(VLAN)                                       \
            PJS_INT(QoS))

#define PJS_LOCALCONFIG_STATICIPV4                              \
    PJS(wano_localconfig_staticipv4,                            \
            PJS_BOOL(enabled)                                   \
            PJS_STRING(ip, 16)                                  \
            PJS_STRING(subnet, 16)                              \
            PJS_STRING(gateway, 16)                             \
            PJS_STRING(primaryDns, 16)                          \
            PJS_STRING(secondaryDns, 16))

#define PJS_LOCALCONFIG                                         \
    PJS(wano_localconfig,                                       \
        PJS_STRING(wanConnectionType, 32)                       \
        PJS_SUB_Q(PPPoE, wano_localconfig_pppoe)                \
        PJS_SUB_Q(DataService, wano_localconfig_dataservice)    \
        PJS_SUB_Q(staticIPv4, wano_localconfig_staticipv4))

#define PJS_GEN_TABLE                                           \
    PJS_LOCALCONFIG_PPPOE                                       \
    PJS_LOCALCONFIG_DATASERVICE                                 \
    PJS_LOCALCONFIG_STATICIPV4                                  \
    PJS_LOCALCONFIG

