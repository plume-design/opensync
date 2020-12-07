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

#ifndef PM_OBJM_H_INCLUDED
#define PM_OBJM_H_INCLUDED

#include "osp_objm.h"

#define PM_OBJM_STORAGE "pm_obj"
#define PM_OBJM_KEY     "store"

#define PM_OBJM_MQTT    "ObjectStore"

#define PM_OBJS_INSTALLED           "install-done"      // Object is installed on the device
#define PM_OBJS_DOWNLOAD_STARTED    "download-started"  // Download of the object started
#define PM_OBJS_DOWNLOAD_FAILED     "download-failed"   // Download of the object failed
#define PM_OBJS_DOWNLOAD_DONE       "download-done"     // Download of the object complete
#define PM_OBJS_INSTALL_FAILED      "install-failed"    // Install of the object failed
#define PM_OBJS_LOAD_FAILED         "load-failed"       // Load of object failed
#define PM_OBJS_ACTIVE              "active"            // Object is activly used by final user
#define PM_OBJS_OBSOLETE            "obsolete"          // Object is not used by final user and can be removed
#define PM_OBJS_REMOVED             "removed"           // Object is removed from device storage
#define PM_OBJS_ERROR               "error"             // General error

#define FIELD_ARRAY_LEN(TYPE,FIELD) ARRAY_LEN(((TYPE*)0)->FIELD)

struct pm_objm_ctx_t
{
    struct ev_async     install_async;
    char url[FIELD_ARRAY_LEN(struct schema_Object_Store_Config, dl_url)];
    char dl_path[PATH_MAX];
    char name[FIELD_ARRAY_LEN(struct schema_Object_Store_Config, name)];
    char version[FIELD_ARRAY_LEN(struct schema_Object_Store_Config, version)];
    int timeout;
    bool fw_integrated;
    char status[64];
};


#endif // PM_OBJM_H_INCLUDED
