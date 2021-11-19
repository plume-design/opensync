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

#include <mqueue.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#define QUEUE_MSG_SIZE 16384

static const char *help = "bsal_sim command queue_name [input]\n\nPossible commands:\n\tread\n\twrite (needs input)\n";

int
main(int argc,
     char *argv[])
{
    const char *queue_path;
    char buf[QUEUE_MSG_SIZE + 1];
    int queue;
    int open_flags;
    int ret;

    if (argc == 1) {
        fprintf(stdout, "%s", help);
        return 0;
    }

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        fprintf(stdout, "%s", help);
        return 0;
    }

    if (argc < 3) {
        fprintf(stderr, "Not enough arguments\n");
        return -1;
    }

    if (strcmp(argv[1], "read") == 0) {
        open_flags = O_RDONLY;
    }
    else if (strcmp(argv[1], "write") == 0) {
        open_flags = O_WRONLY | O_NONBLOCK;
    }
    else {
        fprintf(stderr, "Unknown command \"%s\"\n", argv[1]);
        return -1;
    }

    queue_path = argv[2];
    queue = mq_open(queue_path, open_flags, 0644, NULL);
    if (queue < 0) {
        fprintf(stderr, "Failed to open: \"%s\"\n", strerror(errno));
        return -1;
    }

    if (strcmp(argv[1], "read") == 0) {
        memset(buf, 0, sizeof(buf));
        ret = mq_receive(queue, buf, sizeof(buf), NULL);
        if (ret < 0) {
            fprintf(stderr, "Failed to read: \"%s\"\n", strerror(errno));
            return -1;
        }

        fprintf(stdout, "%s\n", buf);
    }
    else if (strcmp(argv[1], "write") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Input is missing\n");
            return -1;
        }

        ret = mq_send(queue, argv[3], strlen(argv[3]), 0);
        if (ret < 0) {
            fprintf(stderr, "Failed to write: \"%s\"\n", strerror(errno));
            return -1;
        }
    }

    return 0;
}
