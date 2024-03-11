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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "log.h"
#include "const.h"
#include "ovsdb.h"
#include "memutil.h"
#include "osp_temp.h"
#include "osp_temp_srcs.h"


const char* osp_temp_get_temp_src_name(int idx)
{
    const struct temp_src* temp_srcs;

    if ((idx < 0) || (idx > osp_temp_get_srcs_cnt()))
    {
        LOGE("%s Temperature source out of range for index=%d", __func__, idx);
        return NULL;
    }

    temp_srcs = osp_temp_get_srcs();

    return temp_srcs[idx].if_name;
}

int osp_temp_get_idx_from_name(const char *if_name)
{
    int scrs_cnt = osp_temp_get_srcs_cnt();
    const struct temp_src* temp_srcs = osp_temp_get_srcs();

    for (int i = 0; i < scrs_cnt; i++)
    {
        if (strcmp(temp_srcs[i].if_name, if_name) == 0)
        {
            return i;
        }
    }

    LOGE("%s Interface name not found for interface=%s", __func__, if_name);
    return -1;
}

int osp_temp_get_idx_from_band(const char *band)
{
    int scrs_cnt = osp_temp_get_srcs_cnt();
    const struct temp_src* temp_srcs = osp_temp_get_srcs();

    for (int i = 0; i < scrs_cnt; i++)
    {
        if (strcmp(temp_srcs[i].band, band) == 0)
        {
            return i;
        }
    }

    LOGE("%s Interface name not found for interface=%s", __func__, band);
    return -1;
}

int osp_temp_get_temperature(int idx, int *temp)
{
    const struct temp_src* temp_srcs;
    const char *if_name;

    if_name = osp_temp_get_temp_src_name(idx);
    if (if_name == NULL)
    {
        LOGE("%s Interface name not found for index=%d", __func__, idx);
        return -1;
    }

    temp_srcs = osp_temp_get_srcs();
    if (temp_srcs[idx].get_temp == NULL)
    {
        LOGE("%s Function for getting temperature not implemented for index=%d", __func__, idx);
        return -1;
    }

    return temp_srcs[idx].get_temp(if_name, temp);
}
