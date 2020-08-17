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

#include <jansson.h>

#include "log.h"
#include "osp_ps.h"

#include "wano_localconfig.h"

#include "wano_localconfig.pjs.h"
#include "pjs_gen_c.h"

bool wano_localconfig_load(struct wano_localconfig *wc)
{
    pjs_errmsg_t perr;
    json_error_t jerr;
    ssize_t bufsz;

    json_t *jbuf = NULL;
    osp_ps_t *ps = NULL;
    char *buf = NULL;

    bool retval = false;

    ps = osp_ps_open("local_config", OSP_PS_PRESERVE | OSP_PS_READ);
    if (ps == NULL)
    {
        LOG(DEBUG, "wano_localconfig: Unable to open local_config store.");
        goto exit;
    }

    bufsz = osp_ps_get(ps, "wan", NULL, 0);
    if (bufsz <= 0)
    {
        LOG(DEBUG, "wano_localconfig: No 'wan' key in store.");
        goto exit;
    }

    buf = malloc(bufsz + 1);
    if (buf == NULL)
    {
        LOG(ERR, "wano_localconfig: Unable to allocate buffer for local_config.");
        goto exit;
    }

    if (osp_ps_get(ps, "wan", buf, bufsz) != bufsz)
    {
        LOG(ERR, "wano_localcnfig: Short read when reading 'wan' key.");
        goto exit;
    }

    /* Null terminate it, just in case */
    buf[bufsz] = '\0';

    jbuf = json_loads(buf, 0, &jerr);
    if (jbuf == NULL)
    {
        LOG(ERR, "wano_localconfig: Error parsing local_config: %s", jerr.text);
        goto exit;
    }

    if (!wano_localconfig_from_json(wc, jbuf, false, perr))
    {
        LOG(ERR, "wano_localconfig: Invalid configuration: %s", perr);
        goto exit;
    }

    retval = true;
exit:
    if (jbuf != NULL)
    {
        json_decref(jbuf);
    }

    if (buf != NULL)
    {
        free(buf);
    }

    if (ps != NULL)
    {
        osp_ps_close(ps);
    }

    return retval;
}
