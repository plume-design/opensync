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

#ifndef __UTIL__H__
#define __UTIL__H__

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

int csnprintf(char **str, size_t *size, const char *fmt, ...);
char* strargv(char **cmd, bool with_quotes);
int strcmp_len(char *a, size_t alen, char *b, size_t blen);
ssize_t base64_encode(char *out, ssize_t out_sz, void *input, ssize_t input_sz);
ssize_t base64_decode(void *out, ssize_t out_sz, char *input);
void strchomp(char *str, char *delim);

int count_nt_array(char **array);
char* strfmt_nt_array(char *str, size_t size, char **array);
bool is_inarray(const char * key, int argc, char ** argv);
int filter_out_nt_array(char **array, char **filter);
bool is_array_in_array(char **src, char **dest);
char *str_bool(bool a);
char *str_success(bool a);

void delimiter_append(char *dest, int size, char *src, int i, char d);
void comma_append(char *dest, int size, char *src, int i);
void remove_character(char *str, const char character);

int fsa_find_str(char *array, int size, int len, char *str);
char* fsa_find_key_val_def(char *keys, int ksize, char *vals, int vsize, int len, char *key, char *def);
char* fsa_find_key_val_null(char *keys, int ksize, char *vals, int vsize, int len, char *key);
char* fsa_find_key_val(char *keys, int ksize, char *vals, int vsize, int len, char *key);
char *fsa_item(char *array, int size, int len, int num);
void fsa_copy(char *array, int size, int len, int num, char *dest, int dsize, int dlen, int *dnum);

char *str_tolower(char *str);
char *str_toupper(char *str);
bool str_is_mac_address(const char *mac);
bool parse_uri(char *uri, char *proto, char *host, int *port);

#define STRSCPY(dest, src)  strscpy((dest), (src), sizeof(dest))
ssize_t strscpy(char *dest, const char *src, size_t size);
#define STRSCAT(dest, src)  strscat((dest), (src), sizeof(dest))
ssize_t strscat(char *dest, const char *src, size_t size);
char *strschr(const char *s, int c, size_t n);
char *strsrchr(const char *s, int c, size_t n);

#endif /* __UTIL__H__  */
