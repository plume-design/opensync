/* Service discovery TXT record parsing/generation
 *
 * Copyright (c) 2003  Jeremie Miller <jer@jabber.org>
 * Copyright (c) 2016-2021  Joachim Wiberg <troglobit@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the copyright holders nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
*/

#include "sdtxt.h"
#include <stdlib.h>
#include <string.h>
#include "memutil.h"

static size_t _sd2txt_len(const char *key, char *val)
{
    size_t ret = strlen(key);

    if (!*val)
        return ret;

    ret += strlen(val);
    ret++;

    return ret;
}

static void _sd2txt_count(xht_t *__attribute__((unused)) h, const char *key, void *val, void *arg)
{
    int *const count = arg;

    *count += (int)_sd2txt_len(key, val) + 1;
}

static void _sd2txt_write(xht_t *__attribute__((unused)) h, const char *key, void *val, void *arg)
{
    unsigned char **txtp = arg;
    char *const cval = val;

    /* Copy in lengths, then strings */
    **txtp = _sd2txt_len(key, val);
    (*txtp)++;
    memcpy(*txtp, key, strlen(key));
    *txtp += strlen(key);
    if (!*cval)
        return;

    **txtp = '=';
    (*txtp)++;
    memcpy(*txtp, cval, strlen(cval));
    *txtp += strlen(cval);
}

unsigned char *sd2txt(xht_t *h, int *len)
{
    unsigned char *buf, *raw;

    *len = 0;

    xht_walk(h, _sd2txt_count, len);
    if (!*len) {
        *len = 1;
        return (unsigned char *)STRDUP("");
    }

    raw = buf = MALLOC(*len);
    if (!buf)
        return NULL;

    xht_walk(h, _sd2txt_write, &buf);

    return raw;
}

xht_t *txt2sd(unsigned char *txt, int len)
{
    xht_t *h = 0;

    if (txt == 0 || len == 0 || *txt == 0)
        return 0;

    h = xht_new(23);

    /* Loop through data breaking out each block, storing into hashtable */
    for (; *txt <= len && len > 0; len -= *txt, txt += *txt + 1) {
        char key[256], *val;

        if (*txt == 0)
            break;

        memcpy(key, txt + 1, *txt);
        key[*txt] = 0;

        val = strchr(key, '=');
        if (val) {
            *val++ = 0;
            xht_store(h, key, (int)strlen(key), val, (int)strlen(val));
        }
    }

    return h;
}
