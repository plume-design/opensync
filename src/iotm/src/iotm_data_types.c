/*
Copyright (c) 2020, Charter Communications Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Charter Communications Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "log.h"

#include "iotm_data_types.h"
#include "iotm_data_types_private.h"

int hex2bin(char *source_str, unsigned char *dest_buffer)
{
    char *line = source_str;
    if (strstr(line, "0x")) line += 2;  // 0x01 -> 01

    char *data = line;
    int offset;
    int read_byte;
    int data_len = 0;

    while (sscanf(data, " %02x%n", &read_byte, &offset) == 1)
    {
        dest_buffer[data_len++] = read_byte;
        data += offset;
    }
    return data_len;
}

int bin2hex(unsigned char * in, size_t insz, char * out, size_t outsz)
{
    if (insz * 2 > outsz) return -1;

    unsigned char * pin = in;
    const char * hex = "0123456789ABCDEF";
    char * pout = out;
    for(; pin < in+insz; pout +=2, pin++)
    {
        pout[0] = hex[(*pin>>4) & 0xF];
        pout[1] = hex[ *pin     & 0xF];
        if ((size_t)( pout + 2 - out ) > outsz) break;
    }
    out[insz * 2] = '\0';
    return 0;
}

int char_to_uint8(char *input, uint8_t *output)
{
    if (input == NULL) return -1;
    bool too_long = false;

    if (strstr(input, "0x"))
    {
        if (strlen(input) > 4) too_long = true;
    }
    else if(strlen(input) > 2) too_long = true;

    if (too_long)
    {
        LOGE("%s: Input [%s] would overflow u_int8_t, can't parse.",
                __func__, input);
        return -1;
    }

    int len = -1;
    len = hex2bin(input, output);
    if (len <= 0) return -1;
    return 0;
}

int char_to_uint16(char *input, uint16_t *output)
{
    bool too_long = false;

    if (strstr(input, "0x"))
    {
        if (strlen(input) > 6) too_long = true;
    }
    else if(strlen(input) > 4) too_long = true;

    if (too_long)
    {
        LOGE("%s: Input [%s] would overflow u_int16_t, can't parse.",
                __func__, input);
        return -1;
    }
    *output = strtol(input,NULL,16);
    return 0;
}

int char_to_int(char *input, int *output)
{
    *output = atoi(input);
    return 0;
}


int int_to_char(int *input, char *output)
{
    sprintf(output, "%d", *input);
    return 0;
}

int char_to_hex(char *input, iot_barray_t *output)
{
    unsigned char *out_array = output->data;
    char *line = input;
    if (strstr(line, "0x")) line += 2;  // 0x01 -> 01

    char *data = line;
    int offset;
    int read_byte;
    int data_len = 0;

    while (sscanf(data, " %02x%n", &read_byte, &offset) == 1)
    {
        out_array[data_len++] = read_byte;
        data += offset;
    }
    output->data_length = data_len;
    return data_len;
}

int hex_to_char(iot_barray_t *input, char *output)
{
    if (input == NULL
            || output == NULL)
    {
        LOGE("%s: Invalid input parameters, not continuing.", __func__);
        return -1;
    }

    size_t insz = -1;
    char *out = NULL;
    unsigned char *in = NULL;

    insz = input->data_length;
    out = output;
    in = input->data;

    unsigned char * pin = in;
    const char * hex = "0123456789ABCDEF";
    char * pout = out;
    for(; pin < in+insz; pout +=2, pin++)
    {
        pout[0] = hex[(*pin>>4) & 0xF];
        pout[1] = hex[ *pin     & 0xF];
    }
    out[insz * 2] = '\0';
    return 0;
}

int char_to_long(char *input, long *output)
{
    *output = strtol(input, NULL, 10);
    return 0;
}

int long_to_char(long *input, char *output)
{
    sprintf(output, "%ld", *input);
    return 0;
}

int uint8_to_char(uint8_t *input, char *output)
{
    int err = -1;
    err = bin2hex(input, 1, output, 2);
    return err;
}

int uint16_to_char(uint16_t *input, char *output)
{
    int err = -1;
    unsigned char conv[2];
    conv[0] = *input >> 8;
    conv[1] = *input & 0xFF;
    err = bin2hex(conv, 2, output, 4);
    return err;
}



int alloc_data(int type, void **out)
{
    int err = -1;
    switch(type)
    {
        case UINT8:
            *out = calloc(1, sizeof(uint8_t));
            err = 0;
            break;
        case UINT16:
            *out = calloc(1, sizeof(uint16_t));
            err = 0;
            break;
        case STRING:
            *out = calloc(1, sizeof(char) * 1024);
            err = 0;
            break;
        case INT:
            *out = calloc(1, sizeof(int));
            err = 0;
            break;
        case LONG:
            *out = calloc(1, sizeof(long));
            err = 0;
            break;
        default:
            err = -1;
    }
    return err;
}

char *alloc_type_string(int type)
{
    switch(type)
    {
        case UINT8:
            return calloc(1, sizeof(char) * 3);
        case UINT16:
            return calloc(1, sizeof(char) * 5);
        case STRING:
            return calloc(1, sizeof(char) * 1024);
        case INT:
            return calloc(1, sizeof(char) * sizeof(int));
        case LONG:
            return calloc(1, sizeof(char) * sizeof(long));
        default:
            return NULL;
    }
}

int convert_from_type(void *input, int type, char *out)
{
    int err = -1;
    switch(type)
    {
        case UINT8:
            err = uint8_to_char((uint8_t *)input, out);
            break;
        case UINT16:
            err = uint16_to_char((uint16_t *)input, out);
            break;
        case STRING:
            strcpy(out, (char *)input);
            err = 0;
            break;
        case INT:
            err = int_to_char((int *)input, out);
            break;
        case LONG:
            err = long_to_char((long *)input, out);
            break;
        case HEX_ARRAY:
            err = hex_to_char((iot_barray_t *)input, out);
            break;
        default:
            LOGE("%s: No matches for type [%d]", __func__, type);
            break;
    }
    if (err)
    {
        LOGE("%s: Failed to convert to type [%d]",
                __func__, type);
    }
    return err;
}

int convert_to_type(char *input, int type, void *out)
{
    int err = -1;
    switch (type)
    {
        case UINT8:
            err = char_to_uint8(input, (uint8_t *)out);
            break;
        case UINT16:
            err = char_to_uint16(input, (uint16_t *)out);
            break;
        case INT:
            err = char_to_int(input, (int *)out);
            break;
        case LONG:
            err = char_to_long(input, (long *)out);
            break;
        case STRING:
            strcpy((char *)out, input);
            err = 0;
            break;
        case HEX_ARRAY:
            err = char_to_hex(input, (iot_barray_t *)out);
            break;
        default:
            LOGE("%s: No matches for type [%d]", __func__, type);
            break;
    }

    if (err)
    {
        LOGE("%s: Failed to convert [%s] to type [%d]",
                __func__, input, type);
    }
    return err;
}
