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

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "log.h"

int pid_write(const char *pidfile)
{
    int fd = -1;
    int rv = -1;
    int len;
    char buf[100];
    pid_t pid = getpid();
    struct flock fl;

    fd = open(pidfile, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0)
    {
        LOGE("Could not create PID file.");
        goto fail;
    }
    fl.l_start = 0;
    fl.l_len = 0;
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    if (fcntl(fd, F_SETLK, &fl) < 0)
    {
        LOGE("Another instance of this program is already running.");
        goto fail;
    }

    rv = ftruncate(fd, 0);
    if (rv != 0)
    {
        LOGE("Could not truncate PID file.");
        goto fail;
    }

    len = snprintf(buf, sizeof(buf), "%d", pid);

    rv = write(fd, buf, len + 1);
    if (rv != (len + 1))
    {
        LOGE("Could not write to PID file.");
        goto fail;
    }

    rv = fsync(fd);
    if (rv != 0)
    {
        LOGE("Could not sync PID file.");
        goto fail;
    }

    rv = 0;
fail:
    if (fd >= 0)
    {
        close(fd);
    }

    return rv;
}

pid_t pid_get(const char *pidfile)
{
    int fd = -1;
    int rv = -1;
    pid_t pid = 0;
    char buf[100];

    fd = open(pidfile, O_RDONLY);
    if (fd < 0)
    {
        /*LOGE("Could not open PID file.");*/
        goto fail;
    }

    rv = read(fd, buf, sizeof(buf) - 1);
    if (rv < 0)
    {
        /*LOGE("Could not read from PID file.");*/
        goto fail;
    }
    buf[rv] = '\0';

    sscanf(buf, "%d", &pid);
fail:
    if (fd >= 0)
    {
        close(fd);
    }
    return pid;
}

int pid_check(const char *pidfile)
{
    pid_t pid;

    pid = pid_get(pidfile);

    /* Already holding the pid file... */
    if ((pid == 0) || (pid == getpid()))
    {
        return 0;
    }

    /*
     * We send a signal 0 to the process and
     * if an ESRCH error is returned the process cannot
     * be found.
     *
     * Note: errno is usually changed only on error
     */
    if ((kill(pid, 0)) && (errno == ESRCH))
    {
        return 0;
    }

    return pid;
}

int pid_remove(const char *pidfile)
{
    return unlink(pidfile);
}

int daemonize(const char *pidfile)
{
    pid_t pid;

    pid = fork();
    if (pid < 0)
    {
        LOGE("Could not fork().");
        exit(EXIT_FAILURE);
    }
    else if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }

    /* Get a new process group */
    if (setsid() < 0)
    {
        LOGE("Could not setsid().");
        exit(EXIT_FAILURE);
    }

    /* Set file permissions 750 */
    umask(027);

    if (chdir("/") != 0)
    {
        LOGE("Could not chdir(\"/\")");
        exit(EXIT_FAILURE);
    }

    if (pid_check(pidfile) != 0)
    {
        LOGE("Daemon already running");
        exit(EXIT_FAILURE);
    }

    /* Write our own PID to the file */
    if (pid_write(pidfile) != 0)
    {
        LOGE("Could not write PID file.");
        exit(EXIT_FAILURE);
    }

    return 0;
}
