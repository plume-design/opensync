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

#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

#include "memutil.h"
#include "const.h"

#ifndef strdupa
/* In case some libc do not provide strdupa support */
#define strdupa(x) strcpy(alloca(strlen(x)+1), x)
#endif

#define IGNORE_VALUE(a) do { (void)(a); } while (0)

#ifndef MIN
#define MIN(a,b) \
    ({ __typeof__ (a) _a = (a); \
    __typeof__ (b) _b = (b); \
    _a < _b ? _a : _b; })
#endif

#ifndef MAX
#define MAX(a,b) \
    ({ __typeof__ (a) _a = (a); \
    __typeof__ (b) _b = (b); \
    _a > _b ? _a : _b; })
#endif

#define FMT_int(x)  (__FMT_int((char[C_INT32_LEN]){0}, C_INT32_LEN, &x))
char *__FMT_int(char *buf, size_t size, int *value);

int bin2hex(const unsigned char *in, size_t in_size, char *out, size_t out_size);
ssize_t hex2bin(const char *in, size_t in_size, unsigned char *out, size_t out_size);
uint32_t bin2oui24(const uint8_t *in, size_t in_size);
bool ascii2hex(const char *input, char *output, size_t size);
int csnprintf(char **str, size_t *size, const char *fmt, ...);
#define append_snprintf csnprintf
int tsnprintf(char *str, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
char* strargv(char **cmd, bool with_quotes);
int strcmp_len(char *a, size_t alen, char *b, size_t blen);
ssize_t base64_encode(char *out, ssize_t out_sz, void *input, ssize_t input_sz);
ssize_t base64_decode(void *out, ssize_t out_sz, char *input);
char *str_unescape_hex(char *str);
char *strchomp(char *str, char *delim);

int count_nt_array(char **array);
char* strfmt_nt_array(char *str, size_t size, char **array);
void str_array_free(char **arr, size_t size);
char** str_array_dup(char **src, size_t size);
bool is_inarray(const char * key, int argc, char *argv[]);
int filter_out_nt_array(char **array, char **filter);
bool is_array_in_array(char **src, char **dest);
char *str_bool(bool a);
char *str_success(bool a);

void delimiter_append(char *dest, int size, char *src, int i, char d);
void comma_append(char *dest, int size, char *src, int i);
void remove_character(char *str, const char character);

int fsa_find_str(const void *array, int size, int len, const char *str);
void fsa_copy(const void *array, int size, int len, int num, void *dest, int dsize, int dlen, int *dnum);

#define fsa_find_key_val_def(keys, ksize, vals, vsize, len, key, def) \
    (fsa_find_str(keys, ksize, len, key) < 0 \
     ? (def) \
     : (vals) + fsa_find_str(keys, ksize, len, key) * (vsize))

#define fsa_find_key_val_null(keys, ksize, vals, vsize, len, key) \
    fsa_find_key_val_def(keys, ksize, vals, vsize, len, key, NULL)

#define fsa_find_key_val(keys, ksize, vals, vsize, len, key) \
    fsa_find_key_val_def(keys, ksize, vals, vsize, len, key, "")

#define fsa_item(arr, size, len, i) \
    ((i) >= (len) \
     ? (LOG(CRIT, "FSA out of bounds %d >= %d", i, len), NULL) \
     : (arr) + (i) * (size))

char *str_tolower(char *str);
char *str_toupper(char *str);
char *str_trimws(char *s);
char *strstrip(char *str, const char *chars);
bool str_is_mac_address(const char *mac);
bool parse_uri(char *uri, char *proto, size_t proto_size, char *host, size_t host_size, int *port);


#ifdef static_assert
#define ASSERT_ARRAY(A) \
    ({ \
        static_assert( /* is array */ \
            !__builtin_types_compatible_p(typeof(A), typeof(&(A)[0])), \
            "NOT AN ARRAY: " #A \
        ); \
        A; \
    })
#else
#define ASSERT_ARRAY(A) A
#endif

#define SPRINTF(dest, ...) snprintf(ASSERT_ARRAY(dest), sizeof(dest), ##__VA_ARGS__)

#define STRSCMP(a, b) strcmp((a == NULL || WARN_ON(a == (void *)MEMUTIL_MAGIC)) ? "" : a, \
                             (b == NULL || WARN_ON(b == (void *)MEMUTIL_MAGIC)) ? "" : b)
#define STRSLEN(a) strlen((a == NULL || WARN_ON(a == (void *)MEMUTIL_MAGIC)) ? "" : a)
#define STRSCPY(dest, src)  strscpy(ASSERT_ARRAY(dest), (src), sizeof(dest))
#define STRSCPY_WARN(dest, src) IGNORE_VALUE(WARN_ON(STRSCPY((dest), (src)) < 0))
ssize_t strscpy(char *dest, const char *src, size_t size);
#define STRSCPY_LEN(dest, src, len)  strscpy_len(ASSERT_ARRAY(dest), (src), sizeof(dest), len)
ssize_t strscpy_len(char *dest, const char *src, size_t size, ssize_t src_len);
#define STRSCAT(dest, src)  strscat(ASSERT_ARRAY(dest), (src), sizeof(dest))
ssize_t strscat(char *dest, const char *src, size_t size);
char *strschr(const char *s, int c, size_t n);
char *strsrchr(const char *s, int c, size_t n);
#define strdupafree(s) ({ char *__p = s, *__q = __p ? strdupa(__p) : NULL; FREE(__p); __q; })
char *strfmt(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));
#define strfmta(fmt, ...) strdupafree(strfmt(fmt, ##__VA_ARGS__))
char *argvstr(const char *const*argv);
#define argvstra(argv) strdupafree(argvstr(argv))
char *strexread(const char *prog, const char *const*argv);
#define strexreada(prog, argv) strdupafree(strexread(prog, argv))
int strexread_spawn(const char *prog, const char *const*argv, pid_t *pid, int *read_fd);
#define __strexa_arg1(x, ...) x
#define strexa(...) strdupafree(strchomp(strexread(__strexa_arg1(__VA_ARGS__), (const char *[]){ __VA_ARGS__, NULL }), " \t\r\n"))
#define strexpect(str, prog, ...) ({ char *__p = strexa(prog, ##__VA_ARGS__); __p && !strcmp(__p, str); })
char *strdel(char *heystack, const char *needle, int (*strcmp_fun) (const char*, const char*));
char *strgrow(char **buf, const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));

int    str_count_lines(char *s);
bool   str_split_lines_to(char *s, char **lines, int size, int *count);
char** str_split_lines(char *s, int *count);
bool   str_join(char *str, int size, char **list, int num, char *delim);
bool   str_join_int(char *str, int size, int *list, int num, char *delim);
bool   str_startswith(const char *str, const char *start);
bool   str_endswith(const char *str, const char *end);

char **ini_get_multiple_str_sep(const char *buf, const char *key, const char *sep, size_t *out_len);
char **ini_get_multiple_str(const char *buf, const char *key, size_t *out_len);
int *ini_get_multiple_int(const char *buf, const char *key, size_t *out_len);
char  *ini_get(const char *buf, const char *key);
#define ini_geta(buf, key) strdupafree(ini_get(buf, key))
int    file_put(const char *path, const char *buf);
char  *file_get(const char *path);
#define file_geta(path) strdupafree(file_get(path))
const int *unii_5g_chan2list(int chan, int width);
/*
 * Gets 6GHz channels list for required segment: width, chan1, chan2, ..., 0
 * Function is limited to support channel width 20Mhz - 160 MHz
 * @param channel included in segment
 * @param width channel width required for segment
 * @returns list of channels for required segment
 */
const int *unii_6g_chan2list(int chan, int width);
/*
 * Gets 6GHz channels list for required segment.
 * Functions below are specialised for 320MHz
 * channels with respect to band scheme (_1/_2)
 * @param channel
 * @return pointer to the list of channels terminated
 * by value '0'
 */
const int *unii_6g_320_1_chan2list(int chan);
const int *unii_6g_320_2_chan2list(int chan);
/*
 * Checks if given center channel number matches
 * any of the center channel numbers for respective
 * band scheme
 * @param center channel number
 * @returns true if center channel matches given scheme
 */
bool unii_6g_is_320_1(int centerchan);
bool unii_6g_is_320_2(int centerchan);
/*
 * Based on primary channel and selected band scheme (1/2)
 * returns center channel number.
 * @param primary channel
 * @param band scheme, either 1 or 2, equivalent to 320-1
 * 320-2 band specifiers.
 * @returns center channel number
 */
int unii_6g_320_get_centerchan(int primary, int bandscheme);
int chanlist_to_center(const int *chans);
bool is_private_ip(char *ip_str);
char *str_replace_with(const char *str, const char *from, const char *to);
int str_replace_fixed(char *str, int size, const char *from, const char *to);

/**
 * Check if all bytes in a memory region are equal to the specified value
 *
 * @param[in] buffer     Pointer to the block of memory to check. Must not be NULL.
 * @param[in] value      Value to compare against (interpreted as an unsigned char).
 * @param[in] num_bytes  Number of bytes to compare.
 *
 * @return true if all `num_bytes` bytes in the `buffer` are equal to the specified `value`, false otherwise
 */
bool memcmp_b(const void *buffer, int value, size_t num_bytes);

bool __is_input_shell_safe(const char* input, const char *calling_func);

/**
 * Check if a string contains any characters which can be dangerous when
 * executed in a shell session. Function is used to prevent command injection.
 *
 * @param[in] input         The string we are inspecting for dangerous characters
 *
 * @return true if input string does not contain any dangerous characters, false otherwise
 */
#define is_input_shell_safe(input) __is_input_shell_safe(input, __func__)

/**
 * os_readlink(): a safer readlink() alternative
 * which takes care for NUL string termination
 * returns -1 in case of truncation or other error
 * suitable as a drop-in replacement for readlink()
 */
ssize_t os_readlink(const char *restrict pathname, char *restrict buf, size_t bufsiz);

#endif /* UTIL_H_INCLUDED */
