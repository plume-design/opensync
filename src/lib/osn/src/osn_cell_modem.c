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

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#include "memutil.h"
#include "cell_info.h"
#include "osn_cell_modem.h"

#include "log.h"
#include "neigh_table.h"

/*
 * ===========================================================================
 *  Public API implementation stubs.
 * ===========================================================================
 */

static osn_cell_modem_info_t modem_info;

osn_cell_modem_info_t *
osn_get_cell_modem_info(void)
{
    return &modem_info;
}

void
osn_cell_dump_modem_info(void)
{
    return;
}

int
osn_cell_read_modem(void)
{
    return 0;
}

void
osn_cell_start_modem(int source)
{
    return;
}

void
osn_cell_stop_modem(void)
{
    return;
}

bool
osn_cell_modem_init(void)
{
    return 0;
}

void
osn_cell_modem_reset(void)
{
    return;
}

int
osn_cell_set_apn(char *apn)
{
    return 0;
}

int
osn_cell_set_apn_username(char *apn_username)
{
    return 0;
}

int
osn_cell_set_apn_password(char *apn_password)
{
    return 0;
}

int
osn_cell_set_apn_prototype(enum cell_apn_proto_type apn_proto_type)
{
    return 0;
}

int
osn_cell_set_apn_auth_proto(enum cell_apn_auth_proto apn_auth_proto)
{
    return 0;
}

int
osn_cell_set_bands_enabled(char *bands)
{
    return 0;
}

int
osn_cell_set_mode(enum cell_mode mode)
{
    return 0;
}
