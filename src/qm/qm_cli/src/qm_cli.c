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

#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>
#include <syslog.h>
#include <getopt.h>

#include "ds_tree.h"
#include "log.h"
#include "os.h"
#include "os_backtrace.h"
#include "qm_conn.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

static int qm_log_level = LOG_SEVERITY_INFO;

struct
{
    int cmd;
    char topic[256];
} g_opt;

void qm_get_opt(int argc, char ** argv)
{
    int d;

    g_opt.cmd = QM_CMD_SEND;

    int ch;
    while ((ch = getopt(argc, argv, "sit:d:")) != -1)
    {
        switch(ch)
        {
            case 'd':
                d = atoi(optarg);
                if (d == 1)
                    qm_log_level = LOG_SEVERITY_DEBUG;
                else if (d == 2)
                    qm_log_level = LOG_SEVERITY_TRACE;
                printf("debug %d %d\n", d, qm_log_level);
                break;

            case 's':
                g_opt.cmd = QM_CMD_SEND;
                printf("cmd: send\n");
                break;

            case 'i':
                g_opt.cmd = QM_CMD_STATUS;
                printf("cmd: info stats\n");
                break;

            case 't':
                STRLCPY(g_opt.topic, optarg);
                printf("topic: %s\n", g_opt.topic);
                break;

            default:
                break;
        }
    }

}

void cli_print_res(qm_response_t *res)
{
#define PR(X) printf("%-11s: %d\n", #X, res->X)
    printf("Response: [%.4s] %d\n", res->tag, res->ver);
    PR(seq);
    PR(response);
    PR(error);
    PR(flags);
    PR(conn_status);
    PR(qdrop);
    PR(qlen);
    PR(qsize);
#undef PR
}

void cli_info()
{
    qm_response_t res;
    bool ret;

    printf("%s\n", __FUNCTION__);
    ret = qm_conn_get_status(&res);
    LOG(TRACE, "%s:%d %d", __FUNCTION__, __LINE__, ret);
    if (!ret) {
        printf("Error sending\n");
    }
    cli_print_res(&res);
}

void cli_send()
{
    qm_response_t res;
    char buf[1024*1024];
    char *p = buf;
    int free = sizeof(buf);
    int size = 0;
    int ret;

    printf("%s\n", __FUNCTION__);
    do {
        ret = read(0, p, free);
        if (ret < 0) {
            perror("read");
            exit(1);
        }
        p += ret;
        size += ret;
        free -= ret;
    } while (ret > 0 && free > 0);
    if (size == 0) {
        printf("empty data, not sending\n");
        exit(1);
    }
    if (!qm_conn_send_raw(buf, size, g_opt.topic, &res)) {
        printf("Error sending\n");
    }
    cli_print_res(&res);
}

void cli_action()
{
    switch (g_opt.cmd) {
        case QM_CMD_STATUS:
            cli_info();
            break;
        default:
        case QM_CMD_SEND:
            cli_send();
            break;
    }
}

/******************************************************************************
 *  PUBLIC API definitions
 *****************************************************************************/

int main(int argc, char ** argv)
{
    qm_get_opt(argc, argv);

    // enable logging
    log_open("QM_CLI", LOG_OPEN_STDOUT);
    LOGN("Starting QM_CLI");
    log_severity_set(qm_log_level);
    backtrace_init();

    cli_action();

    LOGN("Exiting QM_CLI");

    return 0;
}
