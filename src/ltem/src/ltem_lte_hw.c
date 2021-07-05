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

#include "log.h"
#include "os.h"
#include "ltem_lte_modem.h"

#define LTE_MODEM_DELAY (100 * 1000) /* 100ms */


int
lte_modem_open(char *modem_path)
{
    int fd;
    struct termios term_attr;
    speed_t baud;

    fd = open(modem_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        LOGE("Open of %s failed: %s", modem_path, strerror(errno));
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
    tcsetattr(fd, TCSAFLUSH, &term_attr);

    return fd;
}

ssize_t
lte_modem_write(int fd, const char *cmd)
{
    ssize_t res;
    ssize_t len;

    len = strlen(cmd);
    res = write(fd, cmd, len);
    if (res < 0)
    {
        LOGE("%s: lte_modem_write failed: %s\n", __func__, strerror(errno));
        return res;
    }
    return res;
}

ssize_t
lte_modem_read(int fd, char *at_buf, ssize_t at_len)
{
    ssize_t res;

    usleep(LTE_MODEM_DELAY); // wait 100ms for the modem to respond
    res = read(fd, at_buf, at_len);
    if (res <= 0)
    {
        LOGE("read failed: %d, errno %s", (int)res, strerror(errno));
    }
    return res;
}

void
lte_modem_close(int fd)
{
    close(fd);
}


