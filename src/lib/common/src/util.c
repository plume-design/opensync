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

#define _GNU_SOURCE
#include <dlfcn.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <errno.h>


#ifdef STDC_HEADERS
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#elif HAVE_STRINGS_H
#include <strings.h>
#endif
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdarg.h>

#include "util.h"
#include "log.h"
#include "os.h"


#define UTIL_URI_MAX_LENG           512

/**
 * Similar to snprintf(), except it appends (concatenates) the resulting string to str.
 * @p str is updated to point to the end of the string while @p size is decreased to
 * reflect the new buffer size.
 *
 * This function can be used to "write" to a memory buffer incrementally.
 */
int csnprintf(char **str, size_t *size, const char *fmt, ...)
{
    va_list va;
    int len;

    va_start(va, fmt);

    len = vsnprintf(*str, *size, fmt, va);
    if (len + 1 >= (int)*size)
    {
        len = *size - 1;
    }

    /* Update pointers */
    *str  += len;
    *size -= len;

    va_end(va);

    return len;
}


/**
 * Split a string into shell-style arguments. This function works in a similar fashion
 * as the strsep() function, except it understands quotes, double quotes and
 * backslashes. This means that, for example, "1 2 3" and '1 2 3' are interpreted
 * as a single tokens.
 *
 * @p cmd should be initialized to the string that is to be split. strargv() will
 * return a pointer to the next token or NULL if end of the string is reached.
 *
 * @warn This function modifies the content of @p cmd; if the content is to be preserved,
 * it is up to to the caller to make a copy before calling this function.
 */
char* strargv(char **cmd, bool with_quotes)
{
    char *dcmd;
    char *scmd;
    char *retval;
    char quote_char;

    enum
    {
        TOK_SPACE,
        TOK_WORD,
        TOK_QUOTE,
        TOK_END,
    }
    state = TOK_SPACE;

    if (*cmd == NULL) return NULL;

    dcmd = scmd = *cmd;

    quote_char = '\0';

    while (state != TOK_END && *scmd != '\0')
    {
        switch (state)
        {
            /* Skip whitespace */
            case TOK_SPACE:
                while (isspace(*scmd)) scmd++;

                if (*scmd == '\0') return NULL;

                state = TOK_WORD;
                break;

            /* Parse non-whitespace sequence */
            case TOK_WORD:
                /* Escape backslashes */
                if (*scmd == '\\')
                {
                    scmd++;

                    if (*scmd != '\0')
                    {
                        *dcmd++ = *scmd++;
                    }
                    else
                    {
                        *scmd = '\\';
                    }
                }

                /* Switch to QUOTE mode */
                if (strchr("\"'", *scmd) != NULL)
                {
                    if (false == with_quotes)
                    {
                        state = TOK_QUOTE;
                    }
                    else
                    {
                        *dcmd++ = *scmd++;
                    }
                    break;
                }

                if (isspace(*scmd))
                {
                    state = TOK_END;
                    break;
                }

                /* Copy chars */
                *dcmd++ = *scmd++;
                break;

            case TOK_QUOTE:
                if (quote_char == '\0') quote_char = *scmd++;

                /* Un-terminated quote */
                if (*scmd == '\0')
                {
                    state = TOK_END;
                    break;
                }

                /* Escape backslashes */
                if (quote_char == '"' && *scmd == '\\')
                {
                    scmd++;

                    if (*scmd != '\0')
                    {
                        *dcmd++ = *scmd++;
                    }
                    else
                    {
                        *scmd = '\\';
                    }

                    break;
                }

                if (*scmd == quote_char)
                {
                    quote_char = '\0';
                    state = TOK_WORD;
                    scmd++;
                    break;
                }


                *dcmd++ = *scmd++;
                break;

            case TOK_END:
                break;
        }
    }

    retval = *cmd;
    if (*scmd == '\0')
    {
        *cmd = NULL;
    }
    else
    {
        *cmd = scmd + 1;
    }

    *dcmd = '\0';

    return retval;
}

/**
 * Compare two strings and their length
 */
int strcmp_len(char *a, size_t alen, char *b, size_t blen)
{
    if (alen != blen) return alen - blen;

    return strncmp(a, b, alen);
}

static char base64_table[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * base64 encode @p input_sz bytes from @p input and store the result to out
 *
 * @retval
 * If the output buffer is not big enough to store the encoded result, a number less than 0
 * is returned. Otherwise the number of bytes stored in @p out is returned.
 */
ssize_t base64_encode(char *out, ssize_t out_sz, void *input, ssize_t input_sz)
{
    uint8_t *pin = input;
    char *pout = out;
    uint8_t m[4];

    if ((((input_sz + 2) / 3) * 4) >= out_sz) return -1;

    while (input_sz > 0)
    {
        m[0] = m[1] = m[2] = m[3] = 0;
        /* pout[0] and pout[1] are never '=' */
        pout[2] = pout[3] = '=';

        switch (input_sz)
        {
            default:
                m[3] = pin[2];
                m[2] = pin[2] >> 6;

                pout[3] = base64_table[m[3] & 63];
                /* Falls through. */

            case 2:
                m[2] |= pin[1] << 2;
                m[1] |= pin[1] >> 4;
                pout[2] = base64_table[m[2] & 63];
                /* Falls through. */

            case 1:
                m[1] |= pin[0] << 4;
                m[0] |= pin[0] >> 2;

                pout[1] = base64_table[m[1] & 63];
                pout[0] = base64_table[m[0] & 63];
                /* Falls through. */
        }

        pout += 4;
        pin += 3;
        input_sz -= 3;
    }

    *pout++ = '\0';

    return pout - out;
}

/**
 * Decode a base64 buffer.
 *
 * @retval
 *
 * This function returns the number of bytes stored in the output buffer.
 * A negative number is returned if the output buffer doesn't contain enough
 * room to store the entire decoded buffer or if there's an invalid character
 * in the input.
 */
ssize_t base64_decode(void *out, ssize_t out_sz, char *input)
{
    int ii;
    ssize_t input_sz = strlen(input);

    if (input_sz == 0) return 0;
    if ((input_sz % 4) != 0) return -1;

    /* Clip ending '=' characters */
    for (ii = 0; ii < 2; ii++) if (input[input_sz - 1] == '=') input_sz -= 1;

    /* Check output length */
    if (((input_sz >> 2) * 3 + (((input_sz & 3) * 3) >> 2)) > out_sz) return -1;

    /* Calculate total output size */
    char *pin = input;
    uint8_t *pout = out;

    while (input_sz > 0)
    {
        uint8_t m[4] = {0};

        /* Calculate number of bytes to process this round */
        int isz = (input_sz > 4) ? 4 : input_sz;

        /* Translate a single character to it's base64 value according to base64_table */
        for (ii = 0; ii < isz; ii++)
        {
            char *p = strchr(base64_table, pin[ii]);

            /* Invalid character found -- error*/
            if (p == NULL) return -1;

            m[ii] = p - base64_table;
        }

        /* Process a 4-byte (or less) block */
        switch (isz)
        {
            default:
                pout[2] = (m[2] << 6) | m[3];
                /* Falls through. */
            case 3:
                pout[1] = (m[1] << 4) | (m[2] >> 2);
                /* Falls through. */
            case 2:
                pout[0] = (m[0] << 2) | (m[1] >> 4);
                /* Falls through. */
        }

        pout += isz - 1;
        input_sz -= 4;
        pin += 4;
    }

    return pout - (uint8_t *)out;
}

/**
 * Remove all characters in @p delim from thee end of the string
 */
void strchomp(char *str, char *delim)
{
    int len = strlen(str);

    while (len > 0 &&
            (strchr(delim, str[len - 1]) != NULL))
    {
        str[len - 1] = '\0';
        len--;
    }
}


/*
 * This function checks array of strings
 * In key is present in at least one of the array members, true is
 * returned
 */
bool is_inarray(const char * key, int argc, char ** argv)
{
    int i;
    bool retval = false;

    for(i = 0; i < argc; i++)
    {
        if (0 == strcmp(key, argv[i]))
        {
            LOG(TRACE, "Found is_inarray()::argc=%d|i=%d|argv[i]=%s", argc, i, argv[i]);
            retval = true;
            break;
        }
    }

    return retval;
}

// count null terminated array of pointers
int count_nt_array(char **array)
{
    int count = 0;
    if (!array) return 0;
    while (*array)
    {
        array++;
        count++;
    }
    return count;
}

char* strfmt_nt_array(char *str, size_t size, char **array)
{
    *str = 0;
    strcpy(str, "[");
    while (array && *array)
    {
        if (str[1]) strlcat(str, ",", size);
        strlcat(str, *array, size);
        array++;
    }
    strlcat(str, "]", size);
    return str;
}

int filter_out_nt_array(char **array, char **filter)
{
    int f_count = count_nt_array(filter);
    int count = 0;
    char **src = array;
    char **dest = array;
    while (src && *src)
    {
        if (!is_inarray(*src, f_count, filter))
        {
            *dest = *src;
            dest++;
            count++;
        }
        src++;
    }
    *dest = NULL;
    return count;
}

// are all src[] entries in dest[]?
bool is_array_in_array(char **src, char **dest)
{
    int count = count_nt_array(dest);
    if (!src && !dest) return true;
    if (!src || !dest) return false;
    while (*src)
    {
        if (!is_inarray(*src, count, dest)) return false;
        src++;
    }
    return true;
}

char* str_bool(bool a)
{
    return a ? "true" : "false";
}

char* str_success(bool a)
{
    return a ? "success" : "failure";
}


void delimiter_append(char *dest, int size, char *src, int i, char d)
{
    if (i > 0)
    {
        int len = strlen(dest);
        dest += len;
        size -= len;
        if (size <= 1) return;
        *dest = d;
        dest++;
        size--;
        if (size <= 1) return;
    }
    strlcpy(dest, src, size);
}


void comma_append(char *dest, int size, char *src, int i)
{
    delimiter_append(dest, size, src, i, ',');
}

void remove_character(char *str, const char character)
{
    char* i = str;
    char* j = str;
    while(*j != 0)
    {
        *i = *j++;
        if(*i != character)
          i++;
    }
    *i = 0;
}


// fsa: fixed size array[len][size] helper functions

// return index
int fsa_find_str(char *array, int size, int len, char *str)
{
    int ii;
    for (ii = 0; ii < len; ii++)
    {
        if (strcmp(array + ii * size, str) == 0) return ii;
    }
    return -1;
}

// return def if not fond
char* fsa_find_key_val_def(char *keys, int ksize, char *vals, int vsize, int len, char *key, char *def)
{
    int i = fsa_find_str(keys, ksize, len, key);
    if (i < 0) return def;
    return vals + i * vsize;
}

// return NULL if not found
char* fsa_find_key_val_null(char *keys, int ksize, char *vals, int vsize, int len, char *key)
{
    return fsa_find_key_val_def(keys, ksize, vals, vsize, len, key, NULL);
}

// return empty string ("") if not found
char* fsa_find_key_val(char *keys, int ksize, char *vals, int vsize, int len, char *key)
{
    return fsa_find_key_val_def(keys, ksize, vals, vsize, len, key, "");
}

char* fsa_item(char *array, int size, int len, int num)
{
    if (num >= len) {
        LOG(CRIT, "FSA out of bounds %d >= %d", num, len);
        return NULL;
    }
    return array + num * size;
}

void fsa_copy(char *array, int size, int len, int num, char *dest, int dsize, int dlen, int *dnum)
{
    int i;
    for (i=0; i<num; i++)
    {
        if (i >= dlen) {
            LOG(CRIT, "FSA copy out of bounds %d >= %d", num, dlen);
            break;
        }
        char *s = fsa_item(array, size, len, i);
        char *d = fsa_item(dest, dsize, dlen, i);
        strlcpy(d, s, dsize);
    }
    *dnum = i;
}


char *str_tolower(char *str)
{
    unsigned char *s = (unsigned char*)str;
    while (s && *s) { *s = tolower(*s); s++; }
    return str;
}

char *str_toupper(char *str)
{
    unsigned char *s = (unsigned char*)str;
    while (s && *s) { *s = toupper(*s); s++; }
    return str;
}


bool str_is_mac_address(const char *mac)
{
    int i;
    for (i = 0; i < 6; i++)
    {
        if (!isxdigit(*mac++))
            return false;
        if (!isxdigit(*mac++))
            return false;
        if (i < 5 && *mac++ != ':')
            return false;
    }

    return true;
}

/*
 * [in] uri
 * [out] proto, host, port
 */
bool parse_uri(char *uri, char *proto, char *host, int *port)
{
    // split
    char *sptr, *tproto, *thost, *pstr;
    int tport = 0;
    char tmp[UTIL_URI_MAX_LENG];

    if (!uri || uri[0] == '\0')
    {
        LOGE("URI empty");
        return false;
    }
    strlcpy(tmp, uri, sizeof(tmp));

    // Split the address up into it's pieces
    tproto = strtok_r(tmp, ":", &sptr);
    thost  = strtok_r(NULL, ":", &sptr);
    pstr  = strtok_r(NULL, ":", &sptr);
    if (pstr) tport = atoi(pstr);

    if (   strcmp(tproto, "ssl")
        && strcmp(tproto, "tcp"))
    {
        LOGE("URI %s proto not supported (Only ssl and tcp)", uri);
        return false;
    }

    if (  !thost
        || thost[0] == '\0'
        || tport <= 0
        )
    {
        LOGE("URI %s malformed (Host or port)", uri);
        return false;
    }
    else
    {
        strcpy(proto, tproto);
        strcpy(host, thost);
        *port = tport;
    }

    return true;
}

// strscpy using strnlen + memcpy
ssize_t strscpy(char *dest, const char *src, size_t size)
{
    size_t len;
    if (size == 0) return -E2BIG;
    len = strnlen(src, size - 1);
    memcpy(dest, src, len);
    dest[len] = 0;
    if (src[len]) return -E2BIG;
    return len;
}

ssize_t strscat(char *dest, const char *src, size_t size)
{
    if (size == 0) return -E2BIG;
    size_t dlen = strnlen(dest, size);
    size_t free = size - dlen;
    if (free == 0) return -E2BIG;
    ssize_t slen = strscpy(dest + dlen, src, free);
    if (slen < 0) return slen;
    return dlen + slen;
}

char *strschr(const char *s, int c, size_t n)
{
    size_t len = strnlen(s, n);
    return memchr(s, c, len);
}

char *strsrchr(const char *s, int c, size_t n)
{
    size_t len = strnlen(s, n);
    return memrchr(s, c, len);
}

