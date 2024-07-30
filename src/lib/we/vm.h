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

#ifndef VM_H_INCLUDED
#define VM_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

/* Memory */
#define WE_OP_NIL 0
#define WE_OP_NUM 1
#define WE_OP_BUF 2
#define WE_OP_TAB 3
#define WE_OP_ARR 4
#define WE_OP_GET 5
#define WE_OP_SET 6
#define WE_OP_MOV 7
#define WE_OP_POP 8
/* Arithmetic */
#define WE_OP_ADD 9
#define WE_OP_SUB 10
#define WE_OP_MUL 11
#define WE_OP_CMP 12
#define WE_OP_DIV 13
#define WE_OP_MOD 14
#define WE_OP_AND 15
#define WE_OP_IOR 16
#define WE_OP_XOR 17
#define WE_OP_SHL 18
#define WE_OP_SHR 19
#define WE_OP_EQL 20
/* Branch */
#define WE_OP_HLT 21
#define WE_OP_JMP 22
#define WE_OP_BRZ 23
#define WE_OP_EXT 24
/* Internal */
#define WE_OP_TID 25
#define WE_OP_LEN 26
#define WE_OP_REF 27
#define WE_OP_SIZ 28
#define WE_OP_ORD 29
#define WE_OP_CHR 30
#define WE_OP_INT 31
#define WE_OP_STR 32
#define WE_OP_CAT 33
#define WE_OP_COM 34
#define WE_OP_SEL 35
#define WE_OP_IDX 36
#define WE_OP_VAL 37
#define WE_OP_TIE 38
#define WE_OP_OFF 39
#define WE_OP_OID 40

#define WE_OP_CSP 41
#define WE_OP_BIN 42
#define WE_OP_GMT 43
#define WE_OP_SMT 44
#define WE_OP_LBF 45
#define WE_OP_LBT 46
#define WE_OP_RSZ 47
#define WE_OP_WEA 48
#define WE_OP_RES 49
#define WE_OP_EVA 50

#ifdef __cplusplus
}
#endif

#endif /* VM_H_INCLUDED */
