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

#include <fcntl.h>
#include "os_common.h"

#define LOG_MODULE_ID  LOG_MODULE_ID_OSA

#include "os.h"
#include "log.h"
#include "os_proc.h"
#include "os_file_ops.h"

void os_time_stamp(char *bfr, int32_t len)
{
    time_t now;
    struct tm ts;

    time(&now);
    /* Format time, "yyyy-mm-dd hh:mm:ss zzz" */
    ts = *localtime(&now);
    memset(bfr, 0x00, len);
    strftime(bfr, len, "%Y%m%d_%H:%M:%S", &ts);
}

/* compose a file name "location/<prefix>_process_name_<time_stamp>.pid>" */
bool os_file_name_timestamp_pid(char *file, int size, char *location, char *prefix)
{
    char time_stamp[32];
    char pname[64];
    int32_t pid = getpid();
    int len;

    if (os_pid_to_name(pid, pname, sizeof(pname)) != 0) {
        LOG(ERR, "Error: os_pid_to_name(%d) failed", pid);
        return false;
    }
    os_time_stamp(time_stamp, sizeof(time_stamp));
    len = snprintf(file, size, "%s/%s_%s_%s.%d", location, prefix, pname, time_stamp, pid);
    if (len < 0 || len >= size) return false;

    return true;
}


/* Open the text file "<prefix>_process_name_<time_stamp>.pid>" at specified location. */
FILE *os_file_open(char *location, char *prefix)
{
    char file[256];
    FILE *fp;

    if (!os_file_name_timestamp_pid(file, sizeof(file), location, prefix))
    {
        LOG(ERR, "Error formatting file name: %s %s", location, prefix);
        return NULL;
    }

    fp = fopen(file, "w+");
    if (NULL == fp)
        LOG(ERR, "Error opening the file: %s", file);

    return fp;
}

int os_file_open_fd(char *location, char *prefix)
{
    char file[256];
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
    int flags = O_CREAT | O_TRUNC | O_RDWR;
    int fd;

    if (!os_file_name_timestamp_pid(file, sizeof(file), location, prefix))
    {
        LOG(ERR, "Error formatting file name: %s %s", location, prefix);
        return -1;
    }

    fd = open(file, flags, mode);
    if (fd < 0)
        LOG(ERR, "Error opening the file: %s", file);

    return fd;
}

void os_file_close(FILE *fp)
{
    if (NULL != fp)
        fclose(fp);
}

