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

#include <ctype.h>
#include <errno.h>

#include "log.h"
#include "osa_assert.h"
#include "otbr_cli_util.h"

unsigned int str_leading_whitespace(const char *const str)
{
    const char *p_str;

    if (str == NULL)
    {
        return 0;
    }

    for (p_str = str; *p_str && isspace(*p_str); p_str++)
    {
        /* Do nothing */
    }

    return (unsigned int)(p_str - str);
}

const char *strstra(const char *const haystack, const char *const needle)
{
    if (haystack != NULL)
    {
        const char *const needle_p = strstr(haystack, needle);

        if (needle_p != NULL)
        {
            return needle_p + strlen(needle);
        }
    }
    return NULL;
}

const char *strtonum(const char *const str, void *const value, const size_t sizeof_value, const int base)
{
    char *end;

    if (str == NULL)
    {
        return NULL;
    }

    errno = 0;
    if (base >= 0)
    {
        const uintmax_t max_val = (sizeof_value < 8) ? ((1ULL << (sizeof_value * 8)) - 1) : UINT64_MAX;
        const unsigned long long val = strtoull(str, &end, base);

        if ((errno != 0) || (end == str) || (val > max_val))
        {
            /* Display at-most 21 chars, which is "+18446744073709551615" (UINT64_MAX) in decimal */
            LOGE("Error converting '%.21s' to u%d 0x%.*llx in range [0, %ju] (%d)",
                 str,
                 sizeof_value * 8,
                 sizeof_value * 2,
                 val,
                 max_val,
                 errno);
            end = NULL;
        }
        else if (value != NULL)
        {
            switch (sizeof_value)
            {
                case 1:
                    *(uint8_t *)value = (uint8_t)val;
                    break;
                case 2:
                    *(uint16_t *)value = (uint16_t)val;
                    break;
                case 4:
                    *(uint32_t *)value = (uint32_t)val;
                    break;
                case 8:
                    *(uint64_t *)value = (uint64_t)val;
                    break;
                default:
                    LOGE("Unsupported value size %d", sizeof_value);
                    end = NULL;
                    break;
            }
        }
    }
    else
    {
        const intmax_t max_val = (1LL << (sizeof_value * 8 - 1)) - 1;
        const intmax_t min_val = -(1LL << (sizeof_value * 8 - 1));
        const long long val = strtoll(str, &end, -base);

        if ((errno != 0) || (end == str) || (val > max_val) || (val < min_val))
        {
            LOGE("Error converting '%.21s' to i%d 0x%.*llx in range [%jd, %ju] (%d)",
                 str,
                 sizeof_value * 8,
                 sizeof_value * 2,
                 val,
                 min_val,
                 max_val,
                 errno);
            end = NULL;
        }
        else if (value != NULL)
        {
            switch (sizeof_value)
            {
                case 1:
                    *(int8_t *)value = (int8_t)val;
                    break;
                case 2:
                    *(int16_t *)value = (int16_t)val;
                    break;
                case 4:
                    *(int32_t *)value = (int32_t)val;
                    break;
                case 8:
                    *(int64_t *)value = (int64_t)val;
                    break;
                default:
                    LOGE("Unsupported value size %d", sizeof_value);
                    end = NULL;
                    break;
            }
        }
    }

    return end;
}
