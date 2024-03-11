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

#ifndef CR_SWITCH_H_INCLUDED
#define CR_SWITCH_H_INCLUDED

/* This is most compatible but a bit wacky way to implement
 * resumption. The break/continue ambiguity could lead to
 * unexpected compiler errors, warnings or in extreme cases
 * invalid code generation.
 */
typedef struct
{
    int line;
} cr_state_t;
static inline void cr_state_init(cr_state_t *s)
{
    s->line = 0;
}
#define CR_BEGIN(s) \
    if (!(s)) return true; \
    switch ((s)->line) \
    { \
        case 0:
#define CR_YIELD(s) \
    (s)->line = __LINE__; \
    return false; \
    case __LINE__:
#define CR_END(s) \
    case __LINE__: \
        (s)->line = __LINE__; \
        } \
        ; \
        return true

#endif /* CR_SWITCH_H_INCLUDED */
