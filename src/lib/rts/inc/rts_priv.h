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

#ifndef RTS_PRIV_H
#define RTS_PRIV_H

#define RTS_H /* Act as rts.h for private, internal use */
#include "rts_types.h"
#undef RTS_H

struct rts_buffer_data {
    int ref;
    unsigned char *data;
    unsigned char memory[0];
};

struct rts_buffer {
    unsigned short type;
    unsigned short off;
    unsigned short len;
    unsigned short cap;
    struct rts_buffer_data *data;
};

struct rts_number {
    unsigned short type;
    unsigned short uu1;
    unsigned short uu2;
    unsigned short uu3;
    long long data;
};

struct rts_value {
    union {
        unsigned short type;
        struct rts_number number;
        struct rts_buffer buffer;
    };
};

struct rts_data {
    unsigned state;
    unsigned flags;
    unsigned offset[2];
};

struct rts_vm {
    struct rts_value s[12];
    unsigned short sp;
    unsigned short sync;
    unsigned resume;
    unsigned resume_fun;
    struct rts_object *list;
    struct rts_object *shared;
    struct rts_thread *thread;
    unsigned generation;
};

struct rts_stream {
    struct rts_data data;
    struct rts_vm vm;

    /* Opaque handle optionally supplied by user in rts_stream_create().
     * This is relayed as context in rts_subscribe callbacks. */
    void *user;
};

#include "rts_bitset.h"
#include "rts_mpmc.h"
#include "rts_slob.h"

/* 65k heap variables */
#define BUFFER_GUID_SHIFT 0
#define BUFFER_GUID_MASK 0x03ffffff

/* 4 possible data types (using 4) */
#define BUFFER_TYPE_SHIFT 26
#define BUFFER_TYPE_MASK 0x0c000000

/* 4 possible flags (using 1) */
#define BUFFER_FLAG_SHIFT 28
#define BUFFER_FLAG_MASK 0xf0000000
#define BUFFER_FLAG_EXPORT 1
#define BUFFER_FLAG_STATIC 2

#define BUFFER_GET_GUID(x) (((x) & BUFFER_GUID_MASK) >> BUFFER_GUID_SHIFT)
#define BUFFER_GET_TYPE(x) (((x) & BUFFER_TYPE_MASK) >> BUFFER_TYPE_SHIFT)
#define BUFFER_GET_FLAG(x) (((x) & BUFFER_FLAG_MASK) >> BUFFER_FLAG_SHIFT)

#define BUFFER_SET_GUID(x, guid) \
    ((x) |= (((guid) << BUFFER_GUID_SHIFT) & BUFFER_GUID_MASK))
#define BUFFER_SET_TYPE(x, type) \
    ((x) |= (((type) << BUFFER_TYPE_SHIFT) & BUFFER_TYPE_MASK))
#define BUFFER_SET_FLAG(x, flag) \
    ((x) |= (((flag) << BUFFER_FLAG_SHIFT) & BUFFER_FLAG_MASK))

/* Service table maximum string length */
#define RTS_STAB_MAXLEN 64

/* rts_data->flags */
#define DATA_FLAG_EXT 0x01 /* extended byte */
#define DATA_FLAG_INV 0x02 /* inverted scan */

/* scan instruction context variable */
#define SCANCTX_STATE_MASK 0x00000000ffffffff
#define SCANCTX_OFF_MASK   0x000000000000ffff
#define SCANCTX_OFF0_SHIFT 32
#define SCANCTX_OFF1_SHIFT 48

#define SCANCTX_GET_STATE(x) ((x) & SCANCTX_STATE_MASK)
#define SCANCTX_GET_OFF0(x) (((x) >> SCANCTX_OFF0_SHIFT) & SCANCTX_OFF_MASK)
#define SCANCTX_GET_OFF1(x) (((x) >> SCANCTX_OFF1_SHIFT) & SCANCTX_OFF_MASK)

#define SCANCTX_SET_STATE(x, state) \
    ((x) |= ((state) & SCANCTX_STATE_MASK))
#define SCANCTX_SET_OFF0(x, off) \
    ((x) |= (((long long)(off) & SCANCTX_OFF_MASK) << SCANCTX_OFF0_SHIFT))
#define SCANCTX_SET_OFF1(x, off) \
    ((x) |= (((long long)(off) & SCANCTX_OFF_MASK) << SCANCTX_OFF1_SHIFT))

/* Value types (VOID and BINARY are used internally only) */
typedef enum {
    RTS_VALUE_TYPE_VOID    = 0,
    RTS_VALUE_TYPE_NUMBER  = 1, /* RTS_TYPE_NUMBER */
    RTS_VALUE_TYPE_STRING  = 2, /* RTS_TYPE_STRING */
    RTS_VALUE_TYPE_BINARY  = 3, /* RTS_TYPE_BINARY */
} rts_value_type;

struct rts_stab {
    unsigned size;
    char data[];
};

struct rts_iset {
    unsigned length;
    unsigned offset;
    unsigned data[];
};

struct rts_itab {
    unsigned size;
    struct rts_iset data[];
};

struct rts_var {
    const char *name;
    void (*func)(rts_stream_t stream, void *user, const char *name,
            uint8_t type, uint16_t length, const void *value);
};

/* variations on transition field sizes to use the minimum amount of space */

/* use extended flags on transitions with 32bit destination */
#define F_EMAP (1 << 31) /* extended dst is a map state (otherwise, range) */
#define F_MAP  (1 << 15) /* dst is a map state (otherwise, range) */

/* get index from extended (32bit) state */
#define SE_IDX(x) ((x) & (~(F_EMAP)))

struct rts_tran8 {
    unsigned dst;
    unsigned short fun;
    unsigned short cap;
};

struct rts_tran4fc {
    uint16_t dst;
    uint8_t fun;
    uint8_t cap;
};

struct rts_tran4f {
    uint16_t dst;
    uint16_t fun;
};

struct rts_tran4c {
    uint16_t dst;
    uint16_t cap;
};

struct rts_tran2 {
    uint16_t dst;
};

#define TRT_8    0
#define TRT_4FC  1
#define TRT_4F   2
#define TRT_4C   3
#define TRT_2    4

struct rts_trans {
    struct rts_tran8    *t8;
    struct rts_tran4fc  *t4fc;
    struct rts_tran4f   *t4f;
    struct rts_tran4c   *t4c;
    struct rts_tran2    *t2;
};

#define F_EOP (1 << 31) /* state has end-of-packet transition */
#define F_OUT (1 << 30) /* state has one transition for all accepted inputs */

/* transition type, not a flag but these are the most convenient bits to use */
#define TRT_SHIFT    27
#define TRT_MASK     0x38000000

#define NEXT(x) ((x) & (~(F_EOP | F_OUT | TRT_MASK)))

/* states come in two types:
 *  1) map with a bit for each transition
 *  2) ran with all transitions in a range
 * id is a packed field with info on the state's transitions
 */
struct rts_state_map {
    unsigned id;
    unsigned pad0;
    struct bitset map;
};

struct rts_state_ran {
    unsigned id;

    /* first accepted input for transition */
    unsigned short base;

    /* one past last accepted input for transition */
    unsigned short end;
};

struct rts_states {
    struct rts_state_map *sm;
    struct rts_state_ran *sr;
    unsigned num_sr;
};

struct rts_handle {};
struct rts_thread {
    struct rts_handle handle;
    unsigned pid;
    unsigned group;
    uint64_t timestamp;
    struct rts_mpmc_handle mqh;
    struct rts_pool mp;
    struct rts_bundle *bundle;
    struct rts_lruhash *dict;
    struct rts_lruhash *flow;

    /* stats */
    unsigned scan_started;
    unsigned scan_stopped;
    unsigned scan_bytes;
};

struct rts_bundle {
    int refcount;
    int codelen;
    unsigned generation;
    unsigned numvars;
    unsigned char *code;
    struct rts_var *vars;
    struct rts_states dfa;
    struct rts_trans trans;
    struct rts_itab *ctab;
    struct rts_itab *ftab;
    struct rts_stab *stab;
    const char *keylist;
};

static inline void
rts_data_init(struct rts_data *s, unsigned state)
{
    s->state = state;
    s->flags = 0;
    s->offset[0] = 0;
    s->offset[1] = 0;
}

/*
 * Header section types in the binary signature file
 */
#define RTS_SECTION_VARS 1
#define RTS_SECTION_TEXT 2
#define RTS_SECTION_AUTM 3
#define RTS_SECTION_AUTR 4
#define RTS_SECTION_CTAB 5
#define RTS_SECTION_FTAB 6
#define RTS_SECTION_STAB 7
#define RTS_SECTION_TRT0 8
#define RTS_SECTION_TRT1 9
#define RTS_SECTION_TRT2 10
#define RTS_SECTION_TRT3 11
#define RTS_SECTION_TRT4 12
#define RTS_SECTION_KEYS 13

/******************************************************************************
 * These are simple rewrites of common stdlib/string functions. Normally a bad
 * idea but we want to limit external dependencies to those defined in rts.h.
 *****************************************************************************/
static inline size_t
rts_strlen(const char *str)
{
    size_t len = 0;
    while (*str++)
        ++len;
    return len;
}

static inline char *
rts_strcpy(char *dst, const char *src)
{
    char *ptr = dst;
    while (*src) {
        *ptr++ = *src++;
    }
    *ptr = '\0';
    return dst;
}

static inline char *
rts_strncpy(char *dst, const char *src, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (src[i])
            dst[i] = src[i];
        else
            break;
    }

    if (i < n)
        dst[i] = '\0';
    return dst;
}

static inline int
rts_strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 == *s2) {
        if (!*s1)
            return 0;
        ++s1;
        ++s2;
        --n;
    }
    if (!n) {
        return 0;
    } else if (*s1 < *s2) {
        return -1;
    } else {
        return +1;
    }
}

/* @base must be 10 or 16 */
static inline long long int
rts_strntod(const char *s, size_t n, int base)
{
    int sb = 1;
    long long int rv = 0;

    if (!n)
        return rv;

    if (*s == '-') {
        sb = -1;
        s++;
        n--;
    }

    while (n && *s) {
        char ch = *s++; n--;
        if (ch >= '0' && ch <= '9')
            ch -= '0';
        else if (ch >= 'A' && ch < 'A' + base - 10)
            ch -= 'A' - 10;
        else if (ch >= 'a' && ch < 'a' + base - 10)
            ch -= 'a' - 10;
        else
            break;
        rv *= base;
        rv += ch;
    }
    return sb * rv;
}

static inline long long int
rts_atoi(const char *s)
{
    int sb = 1;
    long long int rv = 0;

    if (*s == '-') {
        sb = -1;
        s++;
    }

    while (*s) {
        char ch = *s++;
        if (ch < '0' || ch > '9')
            break;
        ch -= '0';
        rv *= 10;
        rv += ch;
    }
    return sb * rv;
}

#define RTS_INT64_DIGITS 19
#define RTS_INT64_BUFFER RTS_INT64_DIGITS + 2
static inline char *
rts_itoa(char *dst, int64_t i)
{
    char *p = dst + RTS_INT64_BUFFER -1;
    if (i >= 0) {
        do {
            *--p = '0' + (i % 10);
            i /= 10;
        } while (i != 0);
    } else {
        do {
            *--p = '0' - (i % 10);
            i /= 10;
        } while (i != 0);
        *--p = '-';
    }
    dst[RTS_INT64_BUFFER-1] = '\0';
    return p;
}

#define rts_min(x,y) ({ \
    typeof(x) _x = (x); \
    typeof(y) _y = (y); \
    (void) (&_x == &_y); \
    _x < _y ? _x : _y; })

#define rts_max(x,y) ({ \
    typeof(x) _x = (x); \
    typeof(y) _y = (y); \
    (void) (&_x == &_y); \
    _x > _y ? _x : _y; })

#endif
