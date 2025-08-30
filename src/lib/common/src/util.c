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
#include <spawn.h>
#include <linux/limits.h>

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
#include "memutil.h"
#include "log.h"
#include "os.h"


#define UTIL_URI_MAX_LENG           512
#define DANGEROUS_CHARACTERS        "|;<>&`$"

static bool
is_private_10(uint32_t addr)
{
    return (addr & htonl(0xff000000)) == htonl(0x0a000000);
}

static bool
is_private_172(uint32_t addr)
{
    return (addr & htonl(0xfff00000)) == htonl(0xac100000);
}

static bool
is_private_192(uint32_t addr)
{
    return (addr & htonl(0xffff0000)) == htonl(0xc0a80000);
}

/**
 * @brief Check if the given IPv4 address is a local IP
 *
 * @param ip_str: IP address to check
 * @return true if private IP false otherwise
 */
static bool
is_private_ipv4(char *ip_str)
{
  struct sockaddr_in sa;
  uint32_t addr;
  bool is_local;
  int rc;

  rc = inet_pton(AF_INET, ip_str, &(sa.sin_addr));
  /* if inet_pton fails return false, as ip_str
   * is not a valid IPv4 address
   */
  if (rc != 1) return false;

  addr = *(uint32_t *)&sa.sin_addr;

  is_local = is_private_10(addr) || is_private_172(addr) ||
             is_private_192(addr);

  return is_local;
}

static bool
is_ipv6_addr_ULA(uint8_t *ipv6_addr)
{
    return ((((uint32_t *) (ipv6_addr))[0] & htonl (0xff000000)) == htonl (0xfd000000));
}

static bool
is_ipv6_link_local(uint8_t *ipv6_addr)
{
    return ((ipv6_addr[0] == 0xfe) && ((ipv6_addr[1] & 0xc0) == 0x80));
}

static bool
is_ipv6_addr_sitelocal(uint8_t *ipv6_addr)
{
    return ((ipv6_addr[0] == 0xfe) && ((ipv6_addr[1] & 0xc0) == 0xc0));
}

static bool
is_private_ipv6(char *ip_str)
{
    struct in6_addr ip_addr;
    int is_local;
    int rc;

    rc = inet_pton(AF_INET6, ip_str, &ip_addr);
    if (rc != 1)
    {
        return false;
    }

    is_local = is_ipv6_addr_sitelocal(ip_addr.s6_addr) || is_ipv6_addr_ULA(ip_addr.s6_addr) ||
               is_ipv6_link_local(ip_addr.s6_addr);

    return is_local;
}

/**
 * @brief Check if the given IP address is a local IP
 *
 * @param ip_str: IP address to check
 * @return true if private IP false otherwise
 */
bool
is_private_ip(char *ip_str)
{
    struct sockaddr_in sa;
    struct in6_addr addr;
    int rc;

    if (ip_str == NULL) return false;

    rc = inet_pton(AF_INET, ip_str, &(sa.sin_addr));
    if (rc == 1) return is_private_ipv4(ip_str);

    rc = inet_pton(AF_INET6, ip_str, &addr);
    if (rc == 1) return is_private_ipv6(ip_str);

    return false;
}

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
    int ret;
    int len;

    va_start(va, fmt);

    ret = vsnprintf(*str, *size, fmt, va);
    len = ret;
    if (ret < 0) len = 0;
    if (len >= (int)*size)
    {
        len = *size - 1;
    }

    /* Update pointers */
    *str  += len;
    *size -= len;

    va_end(va);

    return ret;
}


/**
 * tsnprintf = truncate snprintf
 * Same as snprintf() but don't warn about truncation (caused by -Wformat-truncation)
 * This can be used when the truncation of str to size is explicitly desired
 * Either when str size can't be increased or it is known to be large enough
 * Examples: dns hostname: 64, mac address: 18, ...
 */
int tsnprintf(char *str, size_t size, const char *fmt, ...)
{
    va_list va;
    int ret;
    va_start(va, fmt);
    ret = vsnprintf(str, size, fmt, va);
    va_end(va);
    return ret;
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
                /* FALLTHROUGH */

            case 2:
                m[2] |= pin[1] << 2;
                m[1] |= pin[1] >> 4;
                pout[2] = base64_table[m[2] & 63];
                /* FALLTHROUGH */

            case 1:
                m[1] |= pin[0] << 4;
                m[0] |= pin[0] >> 2;

                pout[1] = base64_table[m[1] & 63];
                pout[0] = base64_table[m[0] & 63];
                /* FALLTHROUGH */
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
                /* FALLTHROUGH */
            case 3:
                pout[1] = (m[1] << 4) | (m[2] >> 2);
                /* FALLTHROUGH */
            case 2:
                pout[0] = (m[0] << 2) | (m[1] >> 4);
                /* FALLTHROUGH */
        }

        pout += isz - 1;
        input_sz -= 4;
        pin += 4;
    }

    return pout - (uint8_t *)out;
}

/**
 * Unescape \xXX sequences in @p str.
 */
char *str_unescape_hex(char *str)
{
    char *s;
    char *d;
    int n;

    for (s=str, d=str; *s; d++) {
        if (*s == '\\') {
            s++;
            switch (*s++) {
                case '\\': *d = '\\';   break;
                case '"':  *d = '"';    break;
                case 'e':  *d = '\033'; break;
                case 't':  *d = '\t';   break;
                case 'n':  *d = '\n';   break;
                case 'r':  *d = '\r';   break;
                case 'x':  n = 0; sscanf(s, "%02hhx%n", d, &n); s += n; break;
                default:   *d = 0; return str;
            }
        } else {
            *d = *s++;
        }
    }
    *d = 0;
    return str;
}

/**
 * Remove all characters in @p delim from the end of the string
 */
char *strchomp(char *str, char *delim)
{
    int len;

    if (!str)
        return NULL;

    len = strlen(str);
    while (len > 0 &&
            (strchr(delim, str[len - 1]) != NULL))
    {
        str[len - 1] = '\0';
        len--;
    }
    return str;
}


/*
 * This function checks array of strings
 * In key is present in at least one of the array members, true is
 * returned
 */
bool is_inarray(const char * key, int argc, char *argv[])
{
    int i;
    bool retval = false;

    for(i = 0; i < argc; i++)
    {
        if (0 == strcmp(key, argv[i]))
        {
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
    strscpy(str, "[", size);
    while (array && *array)
    {
        if (str[1]) strlcat(str, ",", size);
        strlcat(str, *array, size);
        array++;
    }
    strlcat(str, "]", size);
    return str;
}

void str_array_free(char **arr, size_t size)
{
    size_t i;
    if (arr != NULL) {
        for (i = 0; i < size; i++) {
            FREE(arr[i]);
        }
        FREE(arr);
    }
}

char** str_array_dup(char **src, size_t size)
{
    size_t i;
    char** dst = CALLOC(size, sizeof(*dst));
    for (i = 0; i < size; i++) {
        dst[i] = STRDUP(src[i]);
    }
    return dst;
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
    strscpy(dest, src, size);
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

int fsa_find_str(const void *array, int size, int len, const char *str)
{
    for (len--; len >= 0; len--)
        if (strcmp(array + len * size, str) == 0)
            return len;
    return -1;
}

void fsa_copy(const void *array, int size, int len, int num, void *dest, int dsize, int dlen, int *dnum)
{
    int i;
    for (i=0; i<num; i++)
    {
        if (i >= dlen) {
            LOG(CRIT, "FSA copy out of bounds %d >= %d", num, dlen);
            break;
        }
        const char *s = fsa_item(array, size, len, i);
        char *d = fsa_item(dest, dsize, dlen, i);
        strscpy(d, s, dsize);
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

char *str_trimws(char *s)
{
    size_t size = strlen(s);
    if (0 == size) return s;

    // trim trailing whitespaces
    char *end = s + size - 1;
    while (end >= s && isspace(*end)) { end--; }
    *(end + 1) = '\0';

    // trim leading whitespaces
    while (*s && isspace(*s)) { s++; }

    return s;
}

/*
 * Delete all occurrences or characters `chars` in string
 * `str`
 *
 * Note: This function modifies the input string.
 */
char *strstrip(char *str, const char *chars)
{
    char *psrc, *pdst;

    psrc = pdst = str;
    while (*psrc != '\0')
    {
        *pdst = *psrc++;
        if (strchr(chars, *pdst) == NULL) pdst++;
    }
    *pdst = '\0';

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
bool parse_uri(char *uri, char *proto, size_t proto_size, char *host, size_t host_size, int *port)
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
    STRSCPY(tmp, uri);

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
        strscpy(proto, tproto, proto_size);
        strscpy(host, thost, host_size);
        *port = tport;
    }

    return true;
}

/**
 * strscpy: safe string copy (using strnlen + memcpy)
 * Safe alternative to strncpy() as it always zero terminates the string.
 * Also safer than strlcpy in case of unterminated source string.
 *
 * Returns the length of copied string or a -E2BIG error if src len is
 * too big in which case the string is copied up to size-1 length
 * and is zero terminated.
 */
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

/**
 * strscpy_len copies a slice of src with length up to src_len
 * but never more than what fits in dest size.
 * If src_len is negative then it is an offset from the end of the src string.
 *
 * Returns the length of copied string or a -E2BIG error if src len is
 * too big in which case the string is copied up to size-1 length
 * and is zero terminated. In case the src_len is a negative offset
 * that is larger than the actual src length, it returns -EINVAL and
 * sets the dest buffer to empty string.
 */
ssize_t strscpy_len(char *dest, const char *src, size_t size, ssize_t src_len)
{
    size_t len;
    if (size == 0) return -E2BIG;
    // get actual src_len
    if (src_len < 0) {
        // using (size - str_len) to limit to size
        src_len = strnlen(src, size - src_len) + src_len;
        // check if offset is larger than actual src len
        if (src_len < 0) {
            *dest = 0;
            return -EINVAL;
        }
    } else {
        // limit to src_len
        src_len = strnlen(src, src_len);
    }
    len = src_len;
    // trim if too big
    if (len > size - 1) len = size - 1;
    // copy the substring
    memcpy(dest, src, len);
    // zero terminate
    dest[len] = 0;
    // return error if src len was too big
    if ((size_t)src_len > len) return -E2BIG;
    // return len on success
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

__attribute__ ((format(printf, 1, 2)))
char *strfmt(const char *fmt, ...)
{
    va_list ap;
    char c, *p;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(&c, 1, fmt, ap);
    va_end(ap);
    if (n >= 0 && (p = MALLOC(++n))) {
        va_start(ap, fmt);
        vsnprintf(p, n, fmt, ap);
        va_end(ap);
        return p;
    }

    return NULL;
}

char *argvstr(const char *const*argv)
{
    char *q;
    int i, n;
    if (!argv)
        return NULL;
    for (n=1, i=0; argv[i]; i++)
        n += strlen(argv[i]) + sizeof(',');
    q = CALLOC(1, n);
    for (i=0; argv[i]; i++)
        if (strscat(q, argv[i], n) >= 0 && argv[i+1])
            strscat(q, ",", n);
    return q;
}

int strexread_spawn(const char *prog,
                    const char *const*argv,
                    pid_t *pid,
                    int *read_fd)
{
    const char *ctx = strfmta("%s(%s, [%s]", __func__, prog ?: "", argvstra(argv) ?: "");

    int fd[2];
    if (pipe(fd) < 0) {
        LOGW("%s: failed to pipe(): %d (%s)", ctx, errno, strerror(errno));
        return -1;
    }

    const int pipe_writeable = fd[1];
    const int pipe_readable = fd[0];

    posix_spawn_file_actions_t actions;
    MEMZERO(actions);

    int err = 0;
    err |= posix_spawn_file_actions_init(&actions);
    err |= posix_spawn_file_actions_addclose(&actions, STDIN_FILENO);
    err |= posix_spawn_file_actions_addclose(&actions, STDOUT_FILENO);
    err |= posix_spawn_file_actions_addclose(&actions, STDERR_FILENO);
    err |= posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    err |= posix_spawn_file_actions_adddup2(&actions, pipe_writeable, STDOUT_FILENO);
    err |= posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    err |= posix_spawn_file_actions_addclose(&actions, pipe_writeable);
    err |= posix_spawn_file_actions_addclose(&actions, pipe_readable);
    if (err) {
        LOGT("%s: failed to assemble posix file actions", ctx);
        close(pipe_writeable);
        close(pipe_readable);
        posix_spawn_file_actions_destroy(&actions);
        return -1;
    }

    err = posix_spawnp(pid, prog, &actions, NULL, (char **)argv, (char **)environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipe_writeable);

    if (err) {
        LOGT("%s: failed to posix_spawnp(): %d, %s (%d)", ctx, err, strerror(errno), errno);
        close(pipe_readable);
        return -1;
    }

    *read_fd = pipe_readable;
    return 0;
}

char *strexread_read(const char *prog,
                     const char *const*argv,
                     pid_t pid,
                     int read_fd)
{
    const char *ctx = strfmta("%s(%s, [%s]", __func__, prog ?: "", argvstra(argv) ?: "");

    int i;
    int j;
    int n;
    char *p;
    char *q;
    for (n=0,i=0,p=0;;) {
        if (i+1 >= n) {
            if ((q = REALLOC(p, (n+=4096))))
                p = q;
            else
                break;
        }
        if ((j = read(read_fd, p+i, n-i-1)) <= 0)
            break;
        i += j;
    }
    p[i] = 0;

    /* If for() above aborted before fully draining the pipe
     * it's imperative to drain it before waiting for the
     * child to exit.  Otherwise child could get stuck at
     * write() and never exit.
     */
    char c;
    while (read(read_fd, &c, 1) == 1);
    close(read_fd);

    int status;
    int err;
    do {
        err = waitpid(pid, &status, 0);
        if (err == -1)
            break;
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));

    LOGT("%s: err=%d status=%d output='%s'", ctx, err, status, p);
    if ((errno = (WIFEXITED(status) ? WEXITSTATUS(status) : -1)) == 0)
        return p;
    FREE(p);
    return NULL;
}

char *strexread(const char *prog, const char *const*argv)
{
    const char *ctx = strfmta("%s(%s, [%s]", __func__, prog ?: "", argvstra(argv) ?: "");

    if (!prog || !argv) {
        LOGW("%s: invalid arguments (prog=%p, argv=%p)", ctx, prog, argv);
        return NULL;
    }

    pid_t pid;
    int read_fd;
    int err = strexread_spawn(prog, argv, &pid, &read_fd);
    if (err) {
        LOGT("%s: strexread_spawn() failed", ctx);
        return NULL;
    }

    return strexread_read(prog, argv, pid, read_fd);
}

char *strdel(char *heystack, const char *needle, int (*strcmp_fun) (const char*, const char*))
{
    const size_t heystack_size = strlen(heystack) + 1;
    char *p = strdupa(heystack ?: "");
    char *q = strdupa("");
    char *i;
    while ((i = strsep(&p, " ")))
        if (strcmp_fun(i, needle))
            q = strfmta("%s %s", i, q);
    strscpy(heystack, strchomp(q, " "), heystack_size);
    return heystack;
}

/*
 * This is printf-like helper appending text to dynamically growing buffer.
 * 'buf' has to be non NULL and '*buf' has to point to either valid
 * dynamically allocated string or NULL.
 */
__attribute__ ((format(printf, 2, 3)))
char *strgrow(char **buf, const char *fmt, ...)
{
    va_list ap;
    size_t old_len;
    size_t new_len;

    if (!buf) return NULL;

    va_start(ap, fmt);
    new_len = vsnprintf(NULL, 0, fmt, ap) + 1;
    va_end(ap);

    old_len = *buf ? strlen(*buf) : 0;
    *buf = REALLOC(*buf, old_len + new_len);

    va_start(ap, fmt);
    vsnprintf(*buf + old_len, new_len, fmt, ap);
    va_end(ap);

    return *buf;
}

int str_count_lines(char *s)
{
    int count = 0;
    if (!s) return 0;
    while (*s) {
        count++;
        s = strchr(s, '\n');
        if (!s) break;
        s++;
    }
    return count;
}

// zero terminate each line in a block of text,
// store ptr to each line in lines array
// count is actual number of lines stored
// return false if size too small
bool str_split_lines_to(char *s, char **lines, int size, int *count)
{
    int i = 0;
    while (*s) {
        if (i >= size) return false;
        lines[i] = s;
        i++;
        *count = i;
        s = strchr(s, '\n');
        if (!s) break;
        *s = 0; // zero term
        s++;
    }
    return true;
}

// zero terminate each line in a block of text,
// allocate and store ptr to each line in lines array
// return lines array or NULL if empty or error allocating
char** str_split_lines(char *s, int *count)
{
    *count = 0;
    int num = str_count_lines(s);
    if (!num) return NULL;
    char **lines = CALLOC(num, sizeof(char*));
    str_split_lines_to(s, lines, num, count);
    return lines;
}

// join a list of strings using delimiter
// return false if size too small
bool str_join(char *str, int size, char **list, int num, char *delim)
{
    char *p = str;
    size_t s = size;
    int i, r;
    for (i=0; i<num; i++) {
        r = csnprintf(&p, &s, "%s%s", list[i], i < num - 1 ? delim : "");
        if (r < 0 || r > (int)s) return false;
    }
    return true;
}

// join a list of ints using delimiter
// return false if size too small
bool str_join_int(char *str, int size, int *list, int num, char *delim)
{
    char *p = str;
    size_t s = size;
    int i, r;
    for (i=0; i<num; i++) {
        r = csnprintf(&p, &s, "%d%s", list[i], i < num - 1 ? delim : "");
        if (r < 0 || r > (int)s) return false;
    }
    return true;
}

/**
 * Returns true if string 'str' starts with string 'start'
 */
bool str_startswith(const char *str, const char *start)
{
    return strncmp(str, start, strlen(start)) == 0;
}

/**
 * Returns true if string 'str' ends with string 'end'
 */
bool str_endswith(const char *str, const char *end)
{
    int i = strlen(str) - strlen(end);
    if (i < 0) return false;
    return strcmp(str + i, end) == 0;
}

char *ini_get(const char *buf, const char *key)
{
    char *lines = strdupa(buf);
    char *line;
    const char *k;
    const char *v;
    while ((line = strsep(&lines, "\t\r\n")))
        if ((k = strsep(&line, "=")) &&
            (v = strsep(&line, "")) &&
            (!strcmp(k, key)))
            return strdup(v);
    return NULL;
}

char **ini_get_multiple_str(const char *buf, const char *key, size_t *out_len)
{
    size_t count = 0;
    char **arr = NULL;
    char *lines = strdupa(buf);
    char *line;
    const char *k;
    const char *v;
    while ((line = strsep(&lines, "\t\r\n"))) {
        if ((k = strsep(&line, "=")) &&
            (v = strsep(&line, "")) &&
            (!strcmp(k, key)))
        {
            arr = REALLOC(arr, sizeof(*arr) * (count + 1));
            arr[count++] = STRDUP(v);
        }
    }
    *out_len = count;
    return arr;
}

char **ini_get_multiple_str_sep(const char *buf, const char *key, const char *sep, size_t *out_len)
{
    size_t count = 0;
    char **arr = NULL;
    char *lines = strdupa(buf);
    char *line;
    const char *k;
    const char *v;
    while ((line = strsep(&lines, "\t\r\n"))) {
        if ((k = strsep(&line, "=")) &&
            (v = strsep(&line, "")) &&
            (!strcmp(k, key)))
        {
            char *list = strdupa(v);
            char *val;
            while ((val = strsep(&list, sep))) {
                arr = REALLOC(arr, sizeof(*arr) * (count + 1));
                arr[count++] = STRDUP(val);
            }
            break;
        }
    }
    *out_len = count;
    return arr;
}

int *ini_get_multiple_int(const char *buf, const char *key, size_t *out_len)
{
    size_t count = 0;
    int *arr = NULL;
    char *lines = strdupa(buf);
    char *line;
    const char *k;
    const char *v;
    while ((line = strsep(&lines, "\t\r\n"))) {
        if ((k = strsep(&line, "=")) &&
            (v = strsep(&line, "")) &&
            (!strcmp(k, key)))
        {
            arr = REALLOC(arr, sizeof(*arr) * (count + 1));
            arr[count++] = atoi(v);
        }
    }
    *out_len = count;
    return arr;
}

int file_put(const char *path, const char *buf)
{
    ssize_t len = strlen(buf);
    ssize_t n;
    int fd;
    if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
        return -1;
    if ((n = write(fd, buf, len)) != len)
        goto err_close;
    close(fd);
    LOGT("%s: wrote %-100s", path, buf);
    return 0;
err_close:
    close(fd);
    LOGT("%s: failed to write %-100s: %d (%s)", path, buf, errno, strerror(errno));
    return -1;
}

char *file_get(const char *path)
{
    ssize_t n;
    ssize_t size = 0;
    ssize_t len = 0;
    char *buf = NULL;
    char *nbuf = NULL;
    char *hunk[4096];
    int fd;
    if ((fd = open(path, O_RDONLY)) < 0)
        goto err;
    while ((n = read(fd, hunk, sizeof(hunk))) > 0) {
        nbuf = REALLOC(buf, (size += n) + 1);
        buf = nbuf;
        memcpy(buf + len, hunk, n);
        len += n;
        buf[len] = 0;
    }
    if (n < 0)
        goto err_free;
    close(fd);
    LOGT("%s: read %-100s", path, (const char *)buf);
    return buf;
err_free:
    FREE(buf);
err_close:
    close(fd);
err:
    LOGT("%s: failed to read: %d (%s)", path, errno, strerror(errno));
    return NULL;
}

const int *unii_5g_chan2list(int chan, int width)
{
    static const int lists[] = {
        /* <width>, <chan1>, <chan2>..., 0, */
        20, 36, 0,
        20, 40, 0,
        20, 44, 0,
        20, 48, 0,
        20, 52, 0,
        20, 56, 0,
        20, 60, 0,
        20, 64, 0,
        20, 100, 0,
        20, 104, 0,
        20, 108, 0,
        20, 112, 0,
        20, 116, 0,
        20, 120, 0,
        20, 124, 0,
        20, 128, 0,
        20, 132, 0,
        20, 136, 0,
        20, 140, 0,
        20, 144, 0,
        20, 149, 0,
        20, 153, 0,
        20, 157, 0,
        20, 161, 0,
        20, 165, 0,
        40, 36, 40, 0,
        40, 44, 48, 0,
        40, 52, 56, 0,
        40, 60, 64, 0,
        40, 100, 104, 0,
        40, 108, 112, 0,
        40, 116, 120, 0,
        40, 124, 128, 0,
        40, 132, 136, 0,
        40, 140, 144, 0,
        40, 149, 153, 0,
        40, 157, 161, 0,
        80, 36, 40, 44, 48, 0,
        80, 52, 56, 60, 64, 0,
        80, 100, 104, 108, 112, 0,
        80, 116, 120, 124, 128, 0,
        80, 132, 136, 140, 144, 0,
        80, 149, 153, 157, 161, 0,
        160, 36, 40, 44, 48, 52, 56, 60, 64, 0,
        160, 100, 104, 108, 112, 116, 120, 124, 128, 0,
        -1, /* keep last */
    };
    const int *start;
    const int *p;

    for (p = lists; *p != -1; p++)
    {
        if (*p == width)
        {
            for (start = ++p; *p; p++)
            {
                if (*p == chan)
                    return start;
            }
        }
        else
        {
            while (*p != 0) p++;
        }
    }

    return NULL;
}

static const int g_unii_6g_channels[] = {
    /* <width>, <chan1>, <chan2>..., 0, */
    20, 1, 0,
    20, 5, 0,
    20, 9, 0,
    20, 13, 0,
    20, 17, 0,
    20, 21, 0,
    20, 25, 0,
    20, 29, 0,
    20, 33, 0,
    20, 37, 0,
    20, 41, 0,
    20, 45, 0,
    20, 49, 0,
    20, 53, 0,
    20, 57, 0,
    20, 61, 0,
    20, 65, 0,
    20, 69, 0,
    20, 73, 0,
    20, 77, 0,
    20, 81, 0,
    20, 85, 0,
    20, 89, 0,
    20, 93, 0,
    20, 97, 0,
    20, 101, 0,
    20, 105, 0,
    20, 109, 0,
    20, 113, 0,
    20, 117, 0,
    20, 121, 0,
    20, 125, 0,
    20, 129, 0,
    20, 133, 0,
    20, 137, 0,
    20, 141, 0,
    20, 145, 0,
    20, 149, 0,
    20, 153, 0,
    20, 157, 0,
    20, 161, 0,
    20, 165, 0,
    20, 169, 0,
    20, 173, 0,
    20, 177, 0,
    20, 181, 0,
    20, 185, 0,
    20, 189, 0,
    20, 193, 0,
    20, 197, 0,
    20, 201, 0,
    20, 205, 0,
    20, 209, 0,
    20, 213, 0,
    20, 217, 0,
    20, 221, 0,
    20, 225, 0,
    20, 229, 0,
    20, 233, 0,
    40, 1, 5, 0,
    40, 9, 13, 0,
    40, 17, 21, 0,
    40, 25, 29, 0,
    40, 33, 37, 0,
    40, 41, 45, 0,
    40, 49, 53, 0,
    40, 57, 61, 0,
    40, 65, 69, 0,
    40, 73, 77, 0,
    40, 81, 85, 0,
    40, 89, 93, 0,
    40, 97, 101, 0,
    40, 105, 109, 0,
    40, 113, 117, 0,
    40, 121, 125, 0,
    40, 129, 133, 0,
    40, 137, 141, 0,
    40, 145, 149, 0,
    40, 153, 157, 0,
    40, 161, 165, 0,
    40, 169, 173, 0,
    40, 177, 181, 0,
    40, 185, 189, 0,
    40, 193, 197, 0,
    40, 201, 205, 0,
    40, 209, 213, 0,
    40, 217, 221, 0,
    40, 225, 229, 0,
    80, 1, 5, 9, 13, 0,
    80, 17, 21, 25, 29, 0,
    80, 33, 37, 41, 45, 0,
    80, 49, 53, 57, 61, 0,
    80, 65, 69, 73, 77, 0,
    80, 81, 85, 89, 93, 0,
    80, 97, 101, 105, 109, 0,
    80, 113, 117, 121, 125, 0,
    80, 129, 133, 137, 141, 0,
    80, 145, 149, 153, 157, 0,
    80, 161, 165, 169, 173, 0,
    80, 177, 181, 185, 189, 0,
    80, 193, 197, 201, 205, 0,
    80, 209, 213, 217, 221, 0,
    160, 1, 5, 9, 13, 17, 21, 25, 29, 0,
    160, 33, 37, 41, 45, 49, 53, 57, 61, 0,
    160, 65, 69, 73, 77, 81, 85, 89, 93, 0,
    160, 97, 101, 105, 109, 113, 117, 121, 125, 0,
    160, 129, 133, 137, 141, 145, 149, 153, 157, 0,
    160, 161, 165, 169, 173, 177, 181, 185, 189, 0,
    160, 193, 197, 201, 205, 209, 213, 217, 221, 0,
    -1, /* keep last */
};

static const int g_unii_6g_channels_320_1[] = {
    320, 1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57, 61, 0,
    320, 65, 69, 73, 77, 81, 85, 89, 93, 97, 101, 105, 109, 113, 117, 121, 125, 0,
    320, 129, 133, 137, 141, 145, 149, 153, 157, 161, 165, 169, 173, 177, 181, 185, 189, 0,
    -1, /* keep last */
};

static const int g_unii_6g_channels_320_2[] = {
    320, 33, 37, 41, 45, 49, 53, 57, 61, 65, 69, 73, 77, 81, 85, 89, 93, 0,
    320, 97, 101, 105, 109, 113, 117, 121, 125, 129, 133, 137, 141, 145, 149, 153, 157, 0,
    320, 161, 165, 169, 173, 177, 181, 185, 189, 193, 197, 201, 205, 209, 213, 217, 221, 0,
    -1, /* keep last */
};

static const int *unii_6g_chan2list_find(const int *lists, int chan, int width)
{
    const int *start;
    const int *p;

    for (p = lists; *p != -1; p++)
    {
        if (*p == width)
        {
            for (start = ++p; *p; p++)
            {
                if (*p == chan)
                    return start;
            }
        }
        else
        {
            while (*p != 0) p++;
        }
    }

    return NULL;
}

const int *unii_6g_chan2list(int chan, int width)
{
    return unii_6g_chan2list_find(g_unii_6g_channels, chan, width);
}

const int *unii_6g_320_1_chan2list(int chan)
{
    return unii_6g_chan2list_find(g_unii_6g_channels_320_1, chan, 320);
}

const int *unii_6g_320_2_chan2list(int chan)
{
    return unii_6g_chan2list_find(g_unii_6g_channels_320_2, chan, 320);
}

bool unii_6g_is_320_1(int centerchan)
{
    return (centerchan == 31 || centerchan == 95 || centerchan == 159);
}

bool unii_6g_is_320_2(int centerchan)
{
    return (centerchan == 63 || centerchan == 127 || centerchan == 191);
}

int chanlist_to_center(const int *chans)
{
    int sum = 0;
    int cnt = 0;

    if (!chans)
        return 0;

    while (*chans) {
        sum += *chans;
        cnt++;
        chans++;
    }

    if (WARN_ON(cnt == 0))
        return 0;

    return sum / cnt;
}

int bin2hex(const unsigned char *in, size_t in_size, char *out, size_t out_size)
{
    unsigned int i;
    char *ptr;

    if (out_size < (in_size * 2 + 1))
        return -1;

    memset(out, 0, out_size);
    ptr = &out[0];

    for (i = 0; i < in_size; i++)
        csnprintf(&ptr, &out_size, "%02hhx", in[i]);

    return 0;
}

ssize_t hex2bin(const char *in, size_t in_size, unsigned char *out, size_t out_size)
{
    size_t i = 0;
    size_t j = 0;

    if (in_size & 1)
        return -1;

    if ((out_size * 2) < in_size)
        return -1;

    memset(out, 0, out_size);

    for (i = 0; i < in_size; i += 2) {
        if (sscanf(in + i, "%02hhx", &out[j]) != 1)
            return -1;

        j++;
    }

    return j;
}

uint32_t bin2oui24(const uint8_t *in, size_t in_size)
{
    uint32_t oui = 0;

    if (in_size < 3) return 0;

    oui = ((uint32_t)((in[0] << 16)) |
           (uint32_t)((in[1] << 8))  |
           (uint32_t)((in[2] << 0)));

    return oui;
}

bool ascii2hex(const char *input, char *output, size_t size)
{
    return bin2hex((unsigned char *)input, strlen(input), output, size) == 0;
}

char *str_replace_with(const char *str,
                 const char *from,
                 const char *to)
{
    const size_t from_len = strlen(from);
    const size_t to_len = strlen(to);
    const char *pos = str;
    const char *end = str + strlen(pos);
    char *out = NULL;
    size_t out_size = 1;
    size_t out_len = 0;

    for (;;) {
        const char *found = strstr(pos, from);
        const char *copy_until = found ? found : end;
        const size_t copy_len = (copy_until - pos);

        out_size += copy_len;
        if (found) out_size += to_len;
        out = REALLOC(out, out_size);
        memcpy(out + out_len, pos, copy_len);
        if (found) memcpy(out + out_len + copy_len, to, to_len);
        out_len = out_size - 1;

        if (found == NULL) break;

        pos = found + from_len;
    }

    if (out != NULL) {
        out[out_len] = 0;
    }

    return out;
}

int str_replace_fixed(
        char *str,
        int size,
        const char *from,
        const char *to)
{
    char *out = str_replace_with(str, from, to);
    int ret = strscpy(str, out, size);
    free(out);
    return ret;
}

char *__FMT_int(char *buf, size_t size, int *value)
{
    snprintf(buf, size, "%d", *value);
    return buf;
}

bool memcmp_b(const void *buffer, const int value, size_t num_bytes)
{
    const unsigned char *byte_buffer = (const unsigned char *)buffer;

    while (num_bytes--)
    {
        if (*(byte_buffer++) != (unsigned char)value)
        {
            return false;
        }
    }

    return true;
}

bool __is_input_shell_safe(const char* input, const char *calling_func)
{
    if (input == NULL) return false;

    LOGT("%s: Input string [%s] being checked", calling_func, input);

    if (strpbrk(input, DANGEROUS_CHARACTERS) != NULL)
    {
        LOGE("%s: Input string [%s] contains dangerous character", calling_func, input);
        return false;
    }

    return true;
}

/**
 * os_readlink(): a safer readlink() alternative
 * which takes care for NUL string termination
 * returns -1 in case of truncation or other error
 * suitable as a drop-in replacement for readlink()
 *
 * libc readlink():
 * - does not append a terminating null byte to buf.
 * - It will (silently) truncate the contents to a length of bufsiz characters
 * - If the returned value equals bufsiz, then truncation may have occurred.
 */
ssize_t os_readlink(const char *restrict pathname, char *restrict buf, size_t bufsiz)
{
    ssize_t len = readlink(pathname, buf, bufsiz);
    if (len > 0 && len < (ssize_t)bufsiz)
    {
        buf[len] = 0;
        return len;
    }
    return -1;
}

