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

#ifndef RTS_VM_H
#define RTS_VM_H

#include "rts_priv.h"

#define RTS_VM_YIELD 0
#define RTS_VM_ERROR -1
#define RTS_VM_LIMIT_SKIP 2048
#define RTS_VM_LIMIT_YANK 2048

enum {
    /* Halt */
    HALT = 0,

    /* Standard instructions */
    JUMP, BREZ, BREZNP, BRNEZNP, LOAD, STORE, DROP, NOOP,

    /* Push number, string, binary */
    PNUM1, PNUM2, PNUM4, PNUM8, PSTR, PBIN, POPN, POPB,

    /* Integer */
    IADD, ISUB, IMUL, IDIV, IEQL, INEQ, ISHL, ISHR, ILT, IGT,

    /* Logical */
    BANG,

    /* Bitwise */
    AND, OR, NOT, XOR,

    /* Type Conversion */
    BTOI, ITOB, ATOI, ITOA, ATOB, BTOA,

    /* Ascii hex to integer */
    HTOI,

    /* Strings */
    SEQL, SNEQ, SCAT, SLEN, SLCE,

    /* Builtin functions */
    PRNT, YANK, SKIP, OFFSET, REMAINING, GOTO, PEEK, SEEK, SCAN, SHMR, EXPECT,

    /* Dictionary */
    DICT,

    TIME
};

/* Virtual machine primary scan facility */
int  rts_vm_scan_buffer(struct rts_vm *, struct rts_data *, struct rts_buffer *buffer);

/* Virtual machine management */
void rts_vm_init(struct rts_vm *vm, struct rts_thread *thread);
void rts_vm_exit(struct rts_vm *vm);
bool rts_vm_sync(struct rts_vm *vm);
int  rts_vm_exec(struct rts_vm *vm, unsigned ip, struct rts_data *, struct rts_buffer *buffer);
void rts_vm_buffer_push(struct rts_vm *vm, unsigned id, struct rts_buffer *, unsigned off, unsigned len);
int  rts_vm_pop(struct rts_vm *vm, struct rts_value *res);

/* implemented in rts.c, called from rts_vm.c */
void rts_value_publish(struct rts_vm *vm, struct rts_value *v, unsigned id);

void rts_dict_save(struct rts_thread *thread, struct rts_buffer *key, struct rts_buffer *val, int32_t ttl);
void rts_dict_find(struct rts_thread *thread, struct rts_buffer *key, struct rts_buffer *val);

void rts_flow_save(struct rts_thread *thread, uint8_t proto,
    const void *sbuf, unsigned slen, uint16_t sport,
    const void *dbuf, unsigned dlen, uint16_t dport, uint32_t ip, int32_t ttl);

/* Allocate shared memory buffer in region @id of @size bytes.
 * Returns a raw pointer for direct writing.
 */
void *rts_vm_shm_get(struct rts_vm *vm, uint32_t id, size_t size);

/* Allocate a shared memory buffer in region @id and write @len bytes from @data */
bool rts_vm_shm_write(struct rts_vm *vm, const void *data, size_t len, uint32_t id);

/* Allocate a shared memory buffer in region @id and refer @len bytes from @data[@off]
 *
 * @data must have been returned from rts_vm_shm_get()
 * @off/@len  must be a valid within @data
 */
bool rts_vm_shm_zcopy(struct rts_vm *vm, const void *data, size_t len, uint32_t id, size_t *off);


#endif
