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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdlib.h>
#include <errno.h>

#include "log.h"
#include "os.h"
#include "memutil.h"
#include "lte_info.h"
#include "osn_lte_modem.h"

#include "ltem_lte_ut.h"

static char modem_cmd_buf[256];
static char lte_at_buf[1024];

char *at_cmd="at\r";
char *ati_cmd="ati\r";
char *qsimstat_cmd="at+qsimstat?\r";
char *gsn_cmd="at+gsn\r";
char *cimi_cmd="at+cimi\r";
char *qccid_cmd="at+qccid\r";
char *creg_cmd="at+creg?\r";
char *csq_cmd="at+csq\r";
char *qgdcnt_cmd="at+qgdcnt?\r";
char *qdsim_cmd="at+qdsim?\r";
char *cops_cmd="at+cops?\r";
char *srv_cell_cmd="at+qeng=\\\"servingcell\\\"\r";
char *neigh_cell_cmd="at+qeng=\\\"neighbourcell\\\"\r";

char *at_at="at\r\r\nOK\r\n";
char *at_ati="ati\r\r\nMyChip\r\nE123\r\nRevision: E1234ALA1A02M4G\r\n\r\nOK\r\n";
char *at_qsimstat="at+qsimstat?\r\r\n+QSIMSTAT: 1,1\r\n\r\nOK\r\n";
char *at_gsn="at+gsn\r\r\n123456789012345\r\n\r\nOK\r\n";
char *at_cimi="at+cimi\r\r\n123456789012345\r\n\r\nOK\r\n";
char *at_qccid="at+qccid\r\r\n+QCCID: 890000000000001234F\r\n\r\nOK\r\n";
char *at_creg="at+creg?\r\r\n+CREG: 0,5\r\n\r\nOK\r\n";
char *at_csq="at+csq\r\r\n+CSQ: 18,99\r\n\r\nOK\r\n";
char *at_qgdcnt="at+qgdcnt?\r\r\n+QGDCNT: 356397,150721\r\n\r\nOK\r\n";
char *at_qdsim="at+qdsim?\r\r\n+QDSIM: 0\r\n\r\nOK\r\n";
char *at_cops="at+cops?\r\r\n+COPS: 0,0,\"AT&T\",7\r\n\r\nOK\r\n";
char *at_srv_cell_lte="at+qeng=\"servingcell\"\r\r\n+QENG: \"servingcell\",\"NOCONN\",\"LTE\",\"FDD\",310,410,A1FBF0A,310,800,2,5,5,8B1E,-115,-14,-80,10,8\r\n\r\nOK\r\n";
char *at_srv_cell_wcdma="at+qeng=\"servingcell\"\r\r\n+QENG: \"servingcell\",\"LIMSRV\",\"WCDMA\",310,410,DEA6,2883C,4385,84,254,-102,-10,-,-,-,-,-\r\n\r\nOK\r\n";
char *at_srv_cell_wcdma_noconn="at+qeng=\"servingcell\"\r\r\n+QENG: \"servingcell\",\"NOCONN\",\"WCDMA\",310,410,DEA6,2883C,4385,84,254,-98,-7,-,-,-,-,-\r\n\r\nOK\r\n";
char *at_neigh_cell="at+qeng=\"neighbourcell\"\r\r\n+QENG: \"neighbourcell intra\",\"LTE\",800,310,-14,-115,-80,0,8,4,10,2,62\r\n+QENG: \"neighbourcell inter\",\"LTE\",5110,263,-11,-102,-82,0,8,2,6,6\r\n+QENG: \"neighbourcell inter\",\"LTE\",66986,-,-,-,-,-,0,6,6,1,-,-,-,-\r\n+QENG: \"neighbourcell\",\"WCDMA\",512,6,14,62,-,-,-,-\r\n+QENG: \"neighbourcell\",\"WCDMA\",4385,0,14,62,84,-1030,-110,15\r\n\r\nOK\r\n";

int
lte_ut_modem_open(char *modem_path)
{
    memset(modem_cmd_buf, 0, sizeof(modem_cmd_buf));
    return 0;
}

ssize_t
lte_ut_modem_write(int fd, const char *cmd)
{
    return STRSCPY(modem_cmd_buf, cmd);
}

ssize_t
lte_ut_modem_read(int fd, char *at_buf, ssize_t at_len)
{
    ssize_t res;

    res = strncmp(modem_cmd_buf, at_cmd, strlen(modem_cmd_buf));
    if (!res)
    {
        strscpy(at_buf, at_at, strlen(at_at));
        return strlen(at_at);
    }
    res = strncmp(modem_cmd_buf, ati_cmd, strlen(modem_cmd_buf));
    if (!res)
    {
        strscpy(at_buf, at_ati, strlen(at_ati));
        return strlen(at_ati);
    }

    res = strncmp(modem_cmd_buf, qsimstat_cmd, strlen(modem_cmd_buf));
    if (!res)
    {
        strscpy(at_buf, at_qsimstat, strlen(at_qsimstat));
        return strlen(at_qsimstat);
    }

    res = strncmp(modem_cmd_buf, gsn_cmd, strlen(modem_cmd_buf));
    if (!res)
    {
        strscpy(at_buf, at_gsn, strlen(at_gsn));
        return strlen(at_gsn);
    }
    res = strncmp(modem_cmd_buf, cimi_cmd, strlen(modem_cmd_buf));
    if (!res)
    {
        strscpy(at_buf, at_cimi, strlen(at_cimi));
        return strlen(at_cimi);
    }
    res = strncmp(modem_cmd_buf, qccid_cmd, strlen(modem_cmd_buf));
    if (!res)
    {
        strscpy(at_buf, at_qccid, strlen(at_qccid));
        return strlen(at_qccid);
    }
    res = strncmp(modem_cmd_buf, creg_cmd, strlen(modem_cmd_buf));
    if (!res)
    {
        strscpy(at_buf, at_creg, strlen(at_creg));
        return strlen(at_creg);
    }
    res = strncmp(modem_cmd_buf, csq_cmd, strlen(modem_cmd_buf));
    if (!res)
    {
        strscpy(at_buf, at_csq, strlen(at_csq));
        return strlen(at_csq);
    }
    res = strncmp(modem_cmd_buf, qgdcnt_cmd, strlen(modem_cmd_buf));
    if (!res)
    {
        strscpy(at_buf, at_qgdcnt, strlen(at_qgdcnt));
        return strlen(at_qgdcnt);
    }
    res = strncmp(modem_cmd_buf, qdsim_cmd, strlen(modem_cmd_buf));
    if (!res)
    {
        strscpy(at_buf, at_qdsim, strlen(at_qdsim));
        return strlen(at_qdsim);
    }
    res = strncmp(modem_cmd_buf, cops_cmd, strlen(modem_cmd_buf));
    if (!res)
    {
        strscpy(at_buf, at_cops, strlen(at_cops));
        return strlen(at_cops);
    }
    res = strncmp(modem_cmd_buf, srv_cell_cmd, strlen(modem_cmd_buf));
    if (!res)
    {
        strscpy(at_buf, at_srv_cell_lte, strlen(at_srv_cell_lte));
        return strlen(at_srv_cell_lte);
    }
    res = strncmp(modem_cmd_buf, neigh_cell_cmd, strlen(modem_cmd_buf));
    if (!res)
    {
        strscpy(at_buf, at_neigh_cell, strlen(at_neigh_cell));
        return strlen(at_neigh_cell);
    }

    memset(at_buf, 0, strlen(at_buf));
    return 0;
}

void
lte_ut_modem_close(int fd)
{
    memset(modem_cmd_buf, 0, sizeof(modem_cmd_buf));
}

char *
lte_ut_run_modem_cmd (const char *cmd)
{
    int fd = 0;
    int res;
    char *at_error = "AT error";

    lte_ut_modem_open(NULL);

    res = lte_ut_modem_write(fd, cmd);
    if (res < 0)
    {
     	LOGE("%s: modem write failed: %s", __func__, strerror(errno));
        lte_ut_modem_close(fd);
        return at_error;
    }

    memset(lte_at_buf, 0, sizeof(lte_at_buf));
    res = lte_ut_modem_read(fd, lte_at_buf, sizeof(lte_at_buf));
    if (res < 0)
    {
     	LOGE("%s: modem read failed: %s", __func__, strerror(errno));
        lte_ut_modem_close(fd);
        return at_error;
    }

    lte_ut_modem_close(fd);
    return lte_at_buf;
}

char *
lte_ut_run_microcom_cmd(char *cmd)
{
    char at_cmd_str[256];

    sprintf(at_cmd_str, "%s\r", cmd);
    return lte_ut_run_modem_cmd(at_cmd_str);
}

