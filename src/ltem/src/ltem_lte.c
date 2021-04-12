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

#include "ltem_mgr.h"
#include "log.h"
#include "neigh_table.h"

static char lte_at_buf[1024];

int
lte_modem_open(void)
{
    int fd;
    char *qtty = "/dev/ttyUSB2";
    struct termios term_attr;
    speed_t baud;

    fd = open(qtty, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        printf("Open of %s failed: %s\n", qtty, strerror(errno));
        return -1;
    }
    fcntl(fd, F_SETFL, O_RDWR);

    tcgetattr(fd, &term_attr);
    baud = cfgetispeed(&term_attr);
    if (baud != B9600)
    {
        cfsetispeed(&term_attr, B9600);
        tcsetattr(fd, TCSANOW, &term_attr);
    }

    return fd;
}

ssize_t
lte_modem_write(const char *cmd, int fd)
{
    ssize_t res;
    ssize_t len;

    len = strlen(cmd);
    res = write(fd, cmd, len);
    if (res < 0)
    {
        LOGE("%s: lte_modem_write failed: %s\n", __func__, strerror(errno));
    }
    return res;
}

/*
 * Convert an LTE_ATCMD_* enum to a human readable string
 */
const char *
lte_at_cmd_tostr(enum lte_at_cmd cmd)
{
    const char *at_str[AT_MAX + 1] =
    {
        #define _STR(sym, str) [sym] = str,
        LTE_ATCMD(_STR)
        #undef _STR
    };

    ASSERT(cmd <= AT_MAX, "Unknown at_cmd value");

    return at_str[cmd];
}

/**
 * @brief
 */
int
ltem_run_at_cmd (enum lte_at_cmd cmd, char *lte_at_resp)
{
    int fd;
    const char *at_cmd;
    int res;

    fd = lte_modem_open();
    if (fd < 0) {
        return fd;
    }

    at_cmd = lte_at_cmd_tostr(cmd);

    res = lte_modem_write(at_cmd, fd);
    if (res < 0)
    {
        return res;
    }

    res = read(fd, lte_at_buf, sizeof(lte_at_buf));
    if (res < 0)
    {
        LOGE("%s: read failed: %s", __func__, strerror(errno));
        return res;
    }

    return 0;

}
