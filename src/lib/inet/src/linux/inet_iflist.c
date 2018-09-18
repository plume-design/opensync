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

/*
 * ===========================================================================
 *  Inet Interface Enumeration implementation for Linux/OpenWRT derivatives
 * ===========================================================================
 */

#include <dirent.h>

#include "const.h"
#include "util.h"
#include "log.h"
#include "inet.h"

#define SYS_CLASS_NET "/sys/class/net"

struct __inet_iflist
{
    DIR         *il_dir;
    char        il_ifname[C_IFNAME_LEN];
};

inet_iflist_t *inet_iflist_open(void)
{
    inet_iflist_t *self = NULL;
    DIR *dir = NULL;

    dir = opendir(SYS_CLASS_NET);
    if (dir == NULL)
    {
        LOG(ERR, "inet_iflist_open: Unable to open %s for reading.", SYS_CLASS_NET);
        goto error;
    }

    self = calloc(1, sizeof(inet_iflist_t));
    self->il_dir =dir;
    self->il_ifname[0] = '\0';

    return self;

error:
    if (dir != NULL) closedir(dir);
    if (self != NULL) free(self);

    return NULL;
}

const char *inet_iflist_read(inet_iflist_t *self)
{
    struct dirent *de;

    while (true)
    {
        de = readdir(self->il_dir);
        if (de == NULL) return NULL;

        if (de->d_type == DT_LNK) break;
    }

    STRSCPY(self->il_ifname, de->d_name);

    return self->il_ifname;
}

void inet_iflist_close(inet_iflist_t *self)
{
    closedir(self->il_dir);
    memset(self, 0, sizeof(*self));
    free(self);
}

