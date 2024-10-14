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

#ifndef WE_H
#define WE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    WE_NIL,
    WE_NUM,
    WE_BUF,
    WE_TAB,
    WE_ARR
} we_type_t;

typedef struct we_arr *we_state_t;

/* we_create() - Allocate state */
int we_create(we_state_t *s, size_t stacksize);

/* we_destroy() - Free state */
int we_destroy(we_state_t s);

/* we_clone() - Clone state */
int we_clone(we_state_t *clone, we_state_t from);

/* we_setup() - Set application externals */
int we_setup(int id, int (*ext)(we_state_t, void *));

/**
 * we_push() - Push a value to the top of the stack
 *
 * Returns the stack position (index), or a negative error code on failure
 **/
int we_pushnil(we_state_t s);
int we_pushnum(we_state_t s, int64_t num);
int we_pushstr(we_state_t s, size_t len, const char *str);
int we_pushbuf(we_state_t s, size_t len, void *buf);
int we_pushtab(we_state_t s, void *tab);
int we_pusharr(we_state_t s, void *arr);

/* we_trim() - Trim bytes from a buffer
 *
 * If n is a negative value, n bytes are trimmed from the tail
 * If n is a positive value, n bytes are trimmed from the head
 *
 * Returns the new length of the buffer
 */
int we_trim(we_state_t s, int reg, int n);

/* we_pop() - Pop the top of the stack */
int we_pop(we_state_t s);

/* we_popr() - Pop the top of the stack into @reg */
int we_popr(we_state_t s, int reg);

/* we_move() - Move a value in the stack to the top */
int we_move(we_state_t s, int reg);

/* we_set() - Set a value in a table */
int we_set(we_state_t s, int reg);

/* we_get() - Get a value in a table */
int we_get(we_state_t s, int reg);

/* we_call() - Call a function */
int we_call(we_state_t *s, void *arg);

/* we_type() - Get a values type from a specific position */
int we_type(we_state_t s, int reg);

/* we_read() - Get a value of kind type from a specific position  */
int we_read(we_state_t s, int reg, we_type_t type, void *val);

/* we_top() - Get the top of stack position */
int we_top(we_state_t s);

/* we_hold() - Hold a reference to the WE_BUF at a position @reg */
int we_hold(we_state_t s, int reg, void **val);

/* we_sync() - Remove a reference to @val and copy the data if necessary */
int we_sync(void *val);

/* we_len() */
int we_len(we_state_t s, int reg);

/* we_next() - Produce the next index in reg.
 *
 *     we_pushnil(s);             ; push iterator
 *     while (we_next(s, -2))     ; push next value
 *         we_pop(s);             ; pop value
 *     we_pop(s);                 ; pop iterator
 */
int we_next(we_state_t s, int reg);

#ifdef __cplusplus
}
#endif
#endif
