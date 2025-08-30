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

#include "rts_vm.h"
#include "rts_ipaddr.h"
#include "rts_buffer.h"

static inline int16_t
read16(const unsigned char *src)
{
    return
        ((int16_t)src[0] << 8)  | ((int16_t)src[1]);
}

static inline int32_t
read32(const unsigned char *src)
{
    return
        ((int32_t)src[0] << 24) | ((int32_t)src[1] << 16) |
        ((int32_t)src[2] << 8)  | ((int32_t)src[3]);
}

static inline int64_t
read64(const unsigned char *src)
{
    return
        ((int64_t)src[0] << 56) | ((int64_t)src[1] << 48) |
        ((int64_t)src[2] << 40) | ((int64_t)src[3] << 32) |
        ((int64_t)src[4] << 24) | ((int64_t)src[5] << 16) |
        ((int64_t)src[6] << 8)  | ((int64_t)src[7]);
}

static inline void
itoa_instr(struct rts_value *dst, struct rts_pool *mp)
{
    char buf[RTS_INT64_BUFFER];
    char *p = rts_itoa(buf, dst->number.data);

    rts_buffer_init(&dst->buffer);
    while (*p)
        rts_buffer_push(&dst->buffer, *p++, mp);
    dst->type = RTS_VALUE_TYPE_STRING;
}

static inline void
itob_instr(struct rts_value *dst, struct rts_pool *mp)
{
    int64_t data = dst->number.data;

    rts_buffer_init(&dst->buffer);
    if (data) {
        size_t i = ((64 - __builtin_clzll(data)) + 7) / 8;
        unsigned char src[i];
        while (i) {
            src[--i] = data & 0xff;
            data >>= 8;
        }
        rts_buffer_write(&dst->buffer, src, sizeof(src), mp);
    } else {
        rts_buffer_write(&dst->buffer, "\0", 1, mp);
    }
    dst->type = RTS_VALUE_TYPE_BINARY;
}

static inline void
btoa_instr(struct rts_value *dst, struct rts_pool *mp)
{
    (void) mp;
    dst->type = RTS_VALUE_TYPE_STRING;
}

static inline void
atob_instr(struct rts_value *dst, struct rts_pool *mp)
{
    (void) mp;
    dst->type = RTS_VALUE_TYPE_BINARY;
}

static inline void
atoi_instr(struct rts_value *dst, struct rts_pool *mp)
{
    int64_t data;
    if (!rts_buffer_empty(&dst->buffer)) {
        data = rts_strntod((const char *)rts_buffer_data(&dst->buffer, 0), rts_buffer_size(&dst->buffer), 10);
    } else {
        data = 0;
    }

    rts_buffer_exit(&dst->buffer, mp);
    dst->number.data = data;
    dst->type = RTS_VALUE_TYPE_NUMBER;
}

static inline void
btoi_instr(struct rts_value *dst, struct rts_pool *mp)
{
    size_t i, len;
    int64_t data;

    len = rts_buffer_size(&dst->buffer);

    if (len > sizeof(data))
        len = sizeof(data);

    data = 0;
    for (i = 0; i < len; i++) {
        data <<= 8;
        data |= rts_buffer_at(&dst->buffer, i);
    }
    rts_buffer_exit(&dst->buffer, mp);
    dst->number.data = data;
    dst->type = RTS_VALUE_TYPE_NUMBER;
}

static inline void
htoi_instr(struct rts_value *dst, struct rts_pool *mp)
{
    int64_t data;
    if (!rts_buffer_empty(&dst->buffer)) {
        data = rts_strntod((const char *)rts_buffer_data(&dst->buffer, 0), rts_buffer_size(&dst->buffer), 16);
    } else {
        data = 0;
    }

    rts_buffer_exit(&dst->buffer, mp);
    dst->number.data = data;
    dst->type = RTS_VALUE_TYPE_NUMBER;
}

/* A heap object */
struct rts_object {
    unsigned id;
    struct rts_object *next;
    struct rts_value value;
};

/* object_create()
 *
 * Allocates a new object for @id.
 *
 * Returns the address of the previous next pointer, which points
 * to the newly allocated object, or NULL if the allocation fails.
 */
static struct rts_object **
object_create(struct rts_object **ptpn, uint32_t id, struct rts_pool *mp)
{
    struct rts_object *obj = rts_pool_alloc(mp, sizeof(*obj));
    if (obj) {
        obj->next = *ptpn;
        *ptpn = obj;
        obj->id = id;
        if (BUFFER_GET_TYPE(id) == RTS_VALUE_TYPE_NUMBER)
            obj->value.number.data = 0;
        else
            rts_buffer_init(&obj->value.buffer);
        return ptpn;
    }
    return NULL;
}

/* object_destroy()
 *
 * Deallocates the object pointed to by @ptpn and updates list head.
 */
static inline void
object_destroy(struct rts_object **ptpn, struct rts_pool *mp)
{
    struct rts_object *next = (*ptpn)->next;
    if (BUFFER_GET_TYPE((*ptpn)->id) != RTS_VALUE_TYPE_NUMBER)
        rts_buffer_exit(&(*ptpn)->value.buffer, mp);
    rts_pool_free(mp, *ptpn);
    *ptpn = next;
}

/* object_is_multipart()
 *
 * Returns true if the object is part of a multipart object. This is
 * safe to call with any object type.
 */
static inline bool
object_is_multipart(struct rts_object *obj)
{
    return (obj->next && obj->id == obj->next->id);
}

/* object_is_unmanaged()
 *
 * Returns ture if the object is referencing unmanaged memory. This is
 * safe to call with any object type.
 */
static inline bool
object_is_unmanaged(struct rts_object *obj)
{
    return (BUFFER_GET_TYPE(obj->id) != RTS_VALUE_TYPE_NUMBER) &&
        rts_buffer_will_sync(&obj->value.buffer);
}

/* object_merge()
 *
 * Merges multipart objects
 *
 * Returns the address of the previous next pointer, which points
 * to the merged object, or NULL if the allocation fails.
 *
 * Note that failure really isn't something we can tolerate. If a
 * merge fails, there is indication for the caller. However, to
 * protect the integrity of the list, the unmanaged buffer MUST be
 * removed. To provide a consistent behaviour, a failed merge will
 * clear the destination.
 */
static struct rts_object **object_sync(struct rts_object **ptpn, struct rts_pool *mp);
static struct rts_object **
object_merge(struct rts_object **ptpn, struct rts_pool *mp)
{
    struct rts_object *dst;
    bool merged;

    rts_assert(BUFFER_GET_TYPE((*ptpn)->id) != RTS_VALUE_TYPE_NUMBER);

    /* If this objects buffer has an offset, then it can't be multipart
     * any longer and we can destroy the resident object */
    if ((*ptpn)->value.buffer.off) {
        object_destroy(&(*ptpn)->next, mp);
        rts_assert(!object_is_multipart(*ptpn));
        rts_assert(object_is_unmanaged(*ptpn));
        return object_sync(ptpn, mp);
    }

    dst = (*ptpn)->next;
    merged = rts_buffer_append(&dst->value.buffer, &(*ptpn)->value.buffer, mp);

    object_destroy(ptpn, mp);

    if (!merged) {
        rts_buffer_clear(&dst->value.buffer, mp);
        return NULL;
    }

    rts_assert(*ptpn == dst);
    return ptpn;
}

/* object_sync()
 *
 * Returns the address of the previous next point, which points
 * to the merged object, or NULL if the operation failed.
 *
 * If the operation fails, the buffer will be cleared.
 */
static struct rts_object **
object_sync(struct rts_object **ptpn, struct rts_pool *mp)
{
    rts_assert(BUFFER_GET_TYPE((*ptpn)->id) != RTS_VALUE_TYPE_NUMBER);

    if (!rts_buffer_sync(&(*ptpn)->value.buffer, mp)) {
        rts_buffer_clear(&(*ptpn)->value.buffer, mp);
        return NULL;
    }
    return ptpn;
}

/* object_find()
 *
 * Returns the address of the previous next pointer, which points
 * to the *first* object matching @id. It is the responsibility of
 * the caller to determine if the object is multipart.
 */
static struct rts_object **
object_find(struct rts_object **ptpn, uint32_t id)
{
    while (*ptpn) {
        if ((*ptpn)->id == id)
            return ptpn;
        ptpn = &(*ptpn)->next;
    }
    return NULL;
}

static void
heap_init(struct rts_object **heap)
{
    *heap = NULL;
}

static void
heap_exit(struct rts_object **heap, struct rts_pool *mp)
{
    struct rts_object **ptpn = heap;
    while (*ptpn)
        object_destroy(ptpn, mp);
    rts_assert(*heap == NULL);
}

static bool
heap_sync(struct rts_object **heap, struct rts_pool *mp)
{
    struct rts_object **ptpn = heap;
    while (*ptpn) {
        /* multipart objects always sync */
        if (object_is_multipart(*ptpn)) {
            if (!object_merge(ptpn, mp))
                return false;
        /* not multipart, but needs sync */
        } else if (object_is_unmanaged(*ptpn)) {
            if (!object_sync(ptpn, mp))
                return false;
        }
        ptpn = &(*ptpn)->next;
    }
    return true;
}

static bool
heap_load(struct rts_object **heap, struct rts_value *dst, uint32_t id, struct rts_pool *mp)
{
    struct rts_object **ptpn = object_find(heap, id);
    if (!ptpn) {
        if (!(ptpn = object_create(heap, id, mp))) {
            rts_assert(0 && "heap_load failed");
            return false;
        }
    }

    rts_assert(BUFFER_GET_TYPE((*ptpn)->id) != RTS_VALUE_TYPE_VOID);

    if (BUFFER_GET_TYPE(id) == RTS_VALUE_TYPE_NUMBER) {
        dst->number.data = (*ptpn)->value.number.data;
    } else {
        if (object_is_multipart(*ptpn) && !object_merge(ptpn, mp))
            return false;
        rts_buffer_copy(&dst->buffer, &(*ptpn)->value.buffer, mp);
    }
    return true;
}

static bool
heap_save(struct rts_object **heap, struct rts_value *src, uint32_t id, struct rts_pool *mp)
{
    struct rts_object **ptpn = object_find(heap, id);
    if (!ptpn && !(ptpn = object_create(heap, id, mp))) {
        rts_assert(0 && "heap_save failed");
        return false;
    }

    if (BUFFER_GET_TYPE(id) == RTS_VALUE_TYPE_NUMBER) {
        (*ptpn)->value.number.data = src->number.data;
    } else {
        rts_buffer_copy(&(*ptpn)->value.buffer, &src->buffer, mp);
    }
    return true;
}

static void
heap_reset(struct rts_object **ptpn, struct rts_pool *mp)
{
    while (*ptpn) {
        if ((BUFFER_GET_FLAG((*ptpn)->id) & BUFFER_FLAG_STATIC) == 0) {
            if (object_is_multipart(*ptpn)) {
                object_destroy(ptpn, mp);
            }
            object_destroy(ptpn, mp);
        } else {
            ptpn = &(*ptpn)->next;
        }
    }
}

static void
heap_drop(struct rts_object **ptpn, uint32_t id, struct rts_pool *mp)
{
    while (*ptpn) {
        /* Would indicate a compiler bug */
        rts_assert((BUFFER_GET_FLAG(id) & BUFFER_FLAG_STATIC) == 0);
        if ((*ptpn)->id == id) {
            if (object_is_multipart(*ptpn)) {
                object_destroy(ptpn, mp);
            }
            object_destroy(ptpn, mp);
        } else {
            ptpn = &(*ptpn)->next;
        }
    }
}

void
rts_vm_buffer_capture(struct rts_vm *vm, unsigned id, struct rts_buffer *src,
    bool adjust_offset, unsigned len)
{
    struct rts_object **ptpn;
    struct rts_pool *mp = &vm->thread->mp;

    if (!(ptpn = object_find(&vm->list, id))) {
        if (!(ptpn = object_create(&vm->list, id, mp)))
            return;
        rts_buffer_copy(&(*ptpn)->value.buffer, src, mp);
        if (rts_buffer_will_sync(src))
            vm->sync++;
        (*ptpn)->value.buffer.len = 0;
    } else if (!object_is_unmanaged(*ptpn)) {
        if (!(ptpn = object_create(ptpn, id, mp)))
            return;
        rts_buffer_copy(&(*ptpn)->value.buffer, src, mp);
        (*ptpn)->value.buffer.len = 0;
        vm->sync++;
    } else {
        /* If the capture buffer already contains data, ensure that this offset
         * adjustment is part of the same sequence. If not, then truncate the
         * old buffer because it is stale. This situation occurs naturally when
         * repeats revive states that previously had capturing transitions that
         * were only partial matches and incomplete. */

        /* Capture a right to left scan; offsets must match otherwise dirty */
        if (adjust_offset) {
            if ((*ptpn)->value.buffer.off != src->off) {
                (*ptpn)->value.buffer.off = src->off;
                (*ptpn)->value.buffer.len = 0;
            }
        /* Normal left to right scan */
        } else if ((*ptpn)->value.buffer.off + (*ptpn)->value.buffer.len != src->off) {
            (*ptpn)->value.buffer.off = src->off;
            (*ptpn)->value.buffer.len = 0;
        }
    }

    /* compiler bug */
    rts_assert(BUFFER_GET_TYPE((*ptpn)->id) == RTS_VALUE_TYPE_BINARY);

    /* anything else is not supported */
    rts_assert((*ptpn)->value.buffer.data == src->data);

    if (adjust_offset)
        (*ptpn)->value.buffer.off -= len;
    (*ptpn)->value.buffer.len += len;
}

void
rts_vm_init(struct rts_vm *vm, struct rts_thread *thread)
{
    heap_init(&vm->list);
    heap_init(&vm->shared);
    vm->thread = thread;
    vm->generation = thread->bundle->generation;
    vm->sp = 0;
    vm->sync = 0;
    vm->resume = 0;
    vm->resume_fun = 0;
}

/* raw access api */
void *
rts_vm_shm_get(struct rts_vm *vm, uint32_t id, size_t size)
{
    struct rts_object **ptpn;
    struct rts_pool *mp = &vm->thread->mp;
    if ((ptpn = object_create(&vm->shared, id, mp))) {
        (*ptpn)->value.type = RTS_VALUE_TYPE_BINARY;
        rts_buffer_init(&(*ptpn)->value.buffer);
        if (rts_buffer_reserve(&(*ptpn)->value.buffer, size, mp)) {
            (*ptpn)->value.buffer.len = size;
            return rts_buffer_data(&(*ptpn)->value.buffer, 0);
        }
        object_destroy(ptpn, mp);
    }
    return NULL;
}

static void
rts_vm_shm_read(struct rts_vm *vm, struct rts_value *dst, uint32_t id)
{
    struct rts_object **ptpn = object_find(&vm->shared, id);
    if (ptpn)
        rts_buffer_copy(&dst->buffer, &(*ptpn)->value.buffer, &vm->thread->mp);
    rts_assert(dst->type == RTS_VALUE_TYPE_BINARY);
}

bool
rts_vm_shm_write(struct rts_vm *vm, const void *data, size_t len, uint32_t id)
{
    struct rts_object **ptpn;
    struct rts_pool *mp = &vm->thread->mp;
    if ((ptpn = object_create(&vm->shared, id, mp))) {
        (*ptpn)->value.type = RTS_VALUE_TYPE_BINARY;
        rts_buffer_init(&(*ptpn)->value.buffer);
        if (rts_buffer_write(&(*ptpn)->value.buffer, data, len, mp))
            return true;
        object_destroy(ptpn, mp);
    }
    return false;
}

bool
rts_vm_shm_zcopy(struct rts_vm *vm, const void *data, size_t len, uint32_t id, size_t *off)
{
    struct rts_object **ptpn;
    struct rts_buffer_data *pdata = rts_container_of(data, struct rts_buffer_data, memory);

    if (!(ptpn = object_create(&vm->shared, id, &vm->thread->mp)))
        return false;

    (*ptpn)->value.type = RTS_VALUE_TYPE_BINARY;
    rts_buffer_init_data(&(*ptpn)->value.buffer, pdata, *off, len);
    *off += len;
    return true;
}

void
rts_vm_exit(struct rts_vm *vm)
{
    struct rts_pool *mp = &vm->thread->mp;

    rts_assert(vm->sp == 0 || vm->resume_fun);

    /* The eval stack wasn't clean on exit. Should be a rare occurrence
     * but difficult to prevent when binops that rely on memory fail.
     */
    while (vm->sp) {
        if (vm->s[vm->sp-1].type != RTS_VALUE_TYPE_NUMBER)
            rts_buffer_exit(&vm->s[vm->sp-1].buffer, mp);
        vm->sp--;
    }
    heap_exit(&vm->list, mp);
    heap_exit(&vm->shared, mp);
    vm->sync = 0;
    vm->resume = 0;
    vm->resume_fun = 0;
}

bool
rts_vm_sync(struct rts_vm *vm)
{
    if (vm->sync) {
        vm->sync = 0;
        return heap_sync(&vm->list, &vm->thread->mp);
    }
    return true;
}

static void
rts_vm_expect(struct rts_vm *vm, int argc, struct rts_value *argv)
{
    (void) argc;

    rts_assert(argc == 7);

    rts_flow_save(vm->thread,
            /* proto */
            argv[0].number.data,
            /* saddr */
            rts_buffer_data(&argv[1].buffer, 0),
            rts_buffer_size(&argv[1].buffer),
            /* sport */
            argv[2].number.data,
            /* daddr */
            rts_buffer_data(&argv[3].buffer, 0),
            rts_buffer_size(&argv[3].buffer),
            /* dport */
            argv[4].number.data,
            /* finst */
            argv[5].number.data,
            /* ttl */
            argv[6].number.data);
}

static inline bool
print_arg(struct rts_buffer *buf, struct rts_pool *mp,
    const char *format, long long num)
{
    int sz;
    char tmp[32];

    sz = __builtin_snprintf(tmp, sizeof(tmp), format, num);
    if (sz < 0 || sz >= (int)sizeof(tmp))
        return false;

    return rts_buffer_write(buf, tmp, sz, mp);
}

/* rts_vm_printf()
 *
 * Implements the print part of the PRNT instruction
 */
static int
rts_vm_printf(const char *fmt, int argc, struct rts_value *argv,
    struct rts_pool *mp)
{
    int i = 0, j, sz;
    const char *s = fmt;
    struct rts_buffer buf;

    /* debug always prints */
#ifdef NDEBUG
    if (!rts_ext_log)
        return 0;
#endif

    rts_buffer_init(&buf);
    if (!rts_buffer_reserve(&buf, rts_strlen(fmt), mp))
        goto printf_err;

    while (*s) {
        if (*s == '%') {

            if (*(s + 1) == '%') {
                if (!rts_buffer_push(&buf, '%', mp))
                    goto printf_err;
                s += 2;
                continue;
            }

            rts_assert (i < argc);
            if (i >= argc)
                return 0;

            switch (*(s + 1)) {
                case 'c':
                    if (!rts_buffer_push(&buf, (unsigned char)argv[i].number.data, mp))
                        goto printf_err;
                    break;
                case 'd':
                    if (!print_arg(&buf, mp, "%lld", argv[i].number.data))
                        goto printf_err;
                    break;
                case 'u':
                    if (!print_arg(&buf, mp, "%llu", argv[i].number.data))
                        goto printf_err;
                    break;
                case 'x':
                    if (!print_arg(&buf, mp, "%llx", argv[i].number.data))
                        goto printf_err;
                    break;
                case 's':
                    for (j = 0, sz = rts_buffer_size(&argv[i].buffer); j < sz; j++) {
                        int chr = rts_buffer_at(&argv[i].buffer, j);
                        if (chr == 0)
                            break;
                        if (!rts_buffer_push(&buf, chr, mp))
                            goto printf_err;
                    }
                    break;
                case 'b':
                    if (!rts_buffer_push(&buf, '[', mp))
                        goto printf_err;
                    for (j = 0, sz = rts_buffer_size(&argv[i].buffer); j < sz; j++) {
                        if (!print_arg(&buf, mp, " %02x", rts_buffer_at(&argv[i].buffer, j)))
                            goto printf_err;
                        if (j + 1 < sz && !rts_buffer_push(&buf, ',', mp))
                            goto printf_err;
                    }
                    if (!rts_buffer_write(&buf, " ]", 2, mp))
                        goto printf_err;
                    break;
                default:
                    if (!rts_buffer_push(&buf, '?', mp))
                        goto printf_err;
                    break;
            }
            i++;
            s += 2;
        } else {
            if (!rts_buffer_push(&buf, *s, mp))
                goto printf_err;
            s++;
        }
    }

    if (!rts_buffer_push(&buf, '\0', mp))
        goto printf_err;

#ifdef NDEBUG
    rts_ext_log((const char *)rts_buffer_data(&buf, 0));
#else
    rts_printf("%s", (const char *)rts_buffer_data(&buf, 0));
#endif

    sz = rts_buffer_size(&buf);
    rts_buffer_exit(&buf, mp);
    return sz;

printf_err:
    rts_buffer_exit(&buf, mp);
    return -1;
}

/* rts_vm_capture()
 *
 * Helper to capture 1 byte for each instruction defined by @cap, @cap_id.
 *
 */
static void
rts_vm_capture(struct rts_vm *vm, struct rts_itab *cap, unsigned cap_id,
    struct rts_buffer *buffer, bool adjust_offset)
{
    unsigned int i;
    unsigned int length = cap->data[cap_id].length;
    unsigned int offset = cap->data[cap_id].offset;

    for (i = 0; i < length; i++) {

        /* @addr is the globally assigned id for this capture. The vm will ask
         * for this buffer. */
        unsigned addr = cap->data[cap->size-1].data[offset + i];
        rts_vm_buffer_capture(vm, addr, buffer, adjust_offset, 1);
    }
}

/* rts_vm_dispatch()
 *
 * Helper to dispatch rts_vm_exec() for each bytecode offset in the function
 * table pointed to by @fun, @fun_id.
 */
static int
rts_vm_dispatch(struct rts_vm *vm, struct rts_data *data, struct rts_buffer *buffer,
    struct rts_itab *fun, unsigned fun_id)
{
    int res = 0;
    unsigned int j, pc;
    unsigned int length = fun->data[fun_id].length;
    unsigned int offset = fun->data[fun_id].offset;

    /* @vm->resume is a packed field:
     *
     * The most significant 8 bits keep track of the code offset relative
     * to the @fun_id index in the rts_itab.
     *
     * The remaining 24 bits contain the vm instruction pointer offset from
     * the loaded @pc from which to resume.
     */

    if (vm->resume) {
        j  = vm->resume >> 24;
        pc = vm->resume & 0x00ffffff;
        vm->resume = 0;
        vm->resume_fun = 0;
    } else {
        j  = 0;
        pc = 0;
    }

    for (; j < length; j++) {

        /* Fresh start */
        if (pc == 0) {
            pc = fun->data[fun->size-1].data[offset + j];
        }

        /* Execute */
        if ((res = rts_vm_exec(vm, pc, data, buffer))) {
            data->state = 0;
            break;
        }

        /* Request to resume */
        if (vm->resume) {

            /* Remember the table index */
            vm->resume_fun = fun_id;
            /* Bug */
            rts_assert((vm->resume & 0xff000000) == 0);
            /* Keep track of @j in the upper 8 bits */
            vm->resume |= (j << 24);
            break;
        }
        /* Reset */
        pc = 0;
    }
    return res;
}

static inline void
get_trans(struct rts_trans *t, int type, unsigned idx, unsigned *dst, unsigned *fun, unsigned *cap)
{
    struct rts_tran8    *t8;
    struct rts_tran4fc    *t4fc;
    struct rts_tran4f    *t4f;
    struct rts_tran4c    *t4c;
    struct rts_tran2    *t2;

    switch (type) {
        case TRT_8:
            t8 = &t->t8[idx];
            *dst = t8->dst;
            *fun = t8->fun;
            if (cap)
                *cap = t8->cap;
            break;

        case TRT_4FC:
            t4fc = &t->t4fc[idx];
            *dst = t4fc->dst;
            if (*dst & F_MAP) {
                *dst &= ~F_MAP;
                *dst |= F_EMAP;
            }
            *fun = t4fc->fun;
            if (cap)
                *cap = t4fc->cap;
            break;

        case TRT_4F:
            t4f = &t->t4f[idx];
            *dst = t4f->dst;
            if (*dst & F_MAP) {
                *dst &= ~F_MAP;
                *dst |= F_EMAP;
            }
            *fun = t4f->fun;
            if (cap)
                *cap = 0;
            break;

        case TRT_4C:
            t4c = &t->t4c[idx];
            *dst = t4c->dst;
            if (*dst & F_MAP) {
                *dst &= ~F_MAP;
                *dst |= F_EMAP;
            }
            *fun = 0;
            if (cap)
                *cap = t4c->cap;
            break;

        case TRT_2:
            t2 = &t->t2[idx];
            *dst = t2->dst;
            if (*dst & F_MAP) {
                *dst &= ~F_MAP;
                *dst |= F_EMAP;
            }
            *fun = 0;
            if (cap)
                *cap = 0;
            break;

        default:
            rts_assert(0 && "unknown transition type");
            *dst = 0;
            *fun = 0;
            if (cap)
                *cap = 0;
            break;
    }
}

/* Transitions the machine with synthetic input \e */
static inline int
eop(struct rts_states *s, unsigned sid, struct rts_trans *t, unsigned *dst, unsigned *fun)
{
    const bool map = (sid & F_EMAP) != 0;
    struct rts_state_map *sm = NULL;
    struct rts_state_ran *sr = NULL;
    int ttype;
    unsigned tid, tidx, index;

    if (map)
        sm = &s->sm[SE_IDX(sid)];
    else
        sr = &s->sr[SE_IDX(sid)];

    tid = map ? sm->id : sr->id;
    ttype = (tid & TRT_MASK) >> TRT_SHIFT;
    tidx = NEXT(tid);

    /* Unlike next(), eop() must return NULL if there isn't
     * actually a eop transition, otherwise we cannot detect
     * the difference between no-eop and eop-with-code-jump
     * because in both cases, t->dst is 0. */
    if (!(tid & F_EOP))
        return -1;

    if (tid & F_OUT)
        index = tidx + 1;
    else if (map)
        index = tidx + rts_bitset_pop(&sm->map);
    else
        index = tidx + (sr->end - sr->base);

    get_trans(t, ttype, index, dst, fun, NULL);
    return 0;
}

/* Transitions the machine with calculated character from @chr & @dir. */
static inline void
next(struct rts_states *s, unsigned sid, struct rts_trans *t, int chr, unsigned *dst, unsigned *fun, unsigned *cap)
{
    const bool map = (sid & F_EMAP) != 0;
    struct rts_state_map *sm = NULL;
    struct rts_state_ran *sr = NULL;
    int ttype;
    unsigned tid, tidx, index;
    __builtin_prefetch(s);

    if (map)
        sm = &s->sm[SE_IDX(sid)];
    else
        sr = &s->sr[SE_IDX(sid)];

    tid = map ? sm->id : sr->id;
    ttype = (tid & TRT_MASK) >> TRT_SHIFT;
    tidx = NEXT(tid);

    if (map) {
        if (!rts_bitset_contains(&sm->map, chr))
            /* no transition; go to 0 */
            index = 0;
        else if (tid & F_OUT)
            /* fast out set; offset is trans index */
            index = tidx;
        else
            /* fast out not set; offset at base + index for this chr */
            index = tidx + rts_bitset_popcount_nth(&sm->map, chr);
    } else {
        if (chr < sr->base || chr >= sr->end)
            index = 0;
        else if (tid & F_OUT)
            index = tidx;
        else
            index = tidx + (chr - sr->base);
    }

    get_trans(t, ttype, index, dst, fun, cap);
}

/* get_adjusted_skip()
 *
 * Sets the length to skip in @skip out param.
 *
 * Returns the adjusted packet req_skip field. If the request can be satisfied
 * in @max_skip len bytes, then the return value is 0.
 */
static inline unsigned short
get_adjusted_skip(unsigned short req_skip, unsigned short max_skip,
    unsigned short *skip)
{
    *skip = rts_min(req_skip, max_skip);
    req_skip -= *skip;
    return req_skip;
}

/* rts_vm_scan_buffer_forward()
 *
 * Scans the data from left to right.
 *
 * Returns the number of bytes consumed.
 */
static int
rts_vm_scan_buffer_forward(struct rts_vm *vm, struct rts_data *data, struct rts_buffer *buffer)
{
    struct rts_bundle *bundle = vm->thread->bundle;
    const unsigned off = buffer->off, dir = data->flags & DATA_FLAG_EXT;
    unsigned fun, cap;

    while (!rts_buffer_empty(buffer)) {

        /* In the middle of a skip */
        if (vm->resume_fun) {
            if (rts_vm_dispatch(vm, data, buffer, bundle->ftab, vm->resume_fun))
                break;
            /* Resume scan while there is buffer */
            continue;
        }

        /* Transition from data */
        next(&bundle->dfa, data->state, &bundle->trans, rts_buffer_at(buffer, 0) + (dir << 8),
                &data->state, &fun, &cap);

        /* Request to capture byte */
        if (cap) {
            rts_vm_capture(vm, bundle->ctab, cap, buffer, false);
        }

        /* Update data offset & length */
        data->offset[dir]++;

        /* Consume byte */
        buffer->off++;
        buffer->len--;

        /* Request to run code */
        if (fun) {
            if (rts_vm_dispatch(vm, data, buffer, bundle->ftab, fun))
                break;
        }

        /* Code requested skip */
        if (vm->resume) {

            /* The value of @vm->resume is the vm instruction pointer to resume after
             * the skip is fulfilled; */
            rts_assert(rts_buffer_empty(buffer));

        /* Done */
        } else if (!data->state) {
            break;
        }
    }
    return buffer->off - off;
}

/* rts_vm_scan_buffer_reverse()
 *
 * Scans the data from right to left.
 *
 * Returns the number of bytes consumed.
 */
static int
rts_vm_scan_buffer_reverse(struct rts_vm *vm, struct rts_data *data, struct rts_buffer *buffer)
{
    struct rts_bundle *bundle = vm->thread->bundle;
    const unsigned len = rts_buffer_size(buffer), dir = data->flags & DATA_FLAG_EXT;
    unsigned fun, cap;

    /* To support capture on reverse streams, we need to manipulate the offset. With
     * each byte read, we will back the offset back down toward its origin offset.
     * The buffer->len must be modified as well to ensure instructions operating on
     * the buffer produce the correct values (for example remaining()). */
    buffer->off += len;

    while (1) {

        if (rts_buffer_empty(buffer)) {
            next(&bundle->dfa, data->state, &bundle->trans, 0x00, &data->state, &fun, NULL);

            if (fun)
                rts_vm_dispatch(vm, data, buffer, bundle->ftab, fun);
            break;
        }

        /* Transition from data */
        next(&bundle->dfa, data->state, &bundle->trans, rts_buffer_at(buffer, -1) + (dir << 8),
                &data->state, &fun, &cap);

        /* Request to capture byte */
        if (cap)
            rts_vm_capture(vm, bundle->ctab, cap, buffer, true);

        data->offset[dir]++;
        buffer->off--;
        buffer->len--;
        if (fun && rts_vm_dispatch(vm, data, buffer, bundle->ftab, fun))
            break;

        if (!data->state)
            break;

    }
    return len - rts_buffer_size(buffer);
}

/* rts_vm_scan_buffer()
 *
 * Returns the number of bytes consumed, -1 on error.
 *
 */
int
rts_vm_scan_buffer(struct rts_vm *vm, struct rts_data *data, struct rts_buffer *buffer)
{
    int scanned;
    int res;
    unsigned dst, fun;

    rts_assert(rts_buffer_will_sync(buffer));

    if (data->flags & DATA_FLAG_INV)
        scanned = rts_vm_scan_buffer_reverse(vm, data, buffer);
    else
        scanned = rts_vm_scan_buffer_forward(vm, data, buffer);

    /* Handle end-of-buffer transition */
    if (data->state && rts_buffer_empty(buffer)) {

        struct rts_bundle *bundle = vm->thread->bundle;
        res = eop(&bundle->dfa, data->state, &bundle->trans, &dst, &fun);
        if (res == 0) {
            data->state = dst;
            if (fun) {
                rts_vm_dispatch(vm, data, buffer, bundle->ftab, fun);
            }
        }
    }

    return scanned;
}

static int
rts_vm_scan_helper(struct rts_vm *vm, struct rts_data *data, struct rts_buffer *buffer)
{
    int scanned;

    /* Best case we are scanning a reference to some external data */
    if (rts_buffer_will_sync(buffer)) {
        scanned = rts_vm_scan_buffer(vm, data, buffer);

    /* Empty so don't bother */
    } else if (!buffer->data) {
        scanned = 0;

    /* The data this buffer points to is resident. This is a problem for
     * zero-copy capture which is setup to adjust offsets only, but only
     * works with transient data. This code forces the buffer to appear
     * transient. In the end this won't prevent a copy, but it will do
     * one large copy instead of multiple byte copies, and the complexity
     * to support this hack is isolated to this function.
     */
    } else {
        struct rts_buffer_data temp = {
            .ref = 1,
            .data = buffer->data->data
        };
        buffer->data = &temp;
        scanned = rts_vm_scan_buffer(vm, data, buffer);
        buffer->data = rts_container_of(temp.data, struct rts_buffer_data, memory);
        if (temp.ref > 1 && !rts_vm_sync(vm))
            return -1;
    }

    return scanned;
}

static void
rts_vm_value_publish(struct rts_vm *vm, struct rts_value *v, uint32_t id)
{
    struct rts_stream *stream = rts_container_of(vm, struct rts_stream, vm);
    uint32_t index = BUFFER_GET_GUID(id);
    struct rts_var *var = &vm->thread->bundle->vars[index];
    void (*func)(rts_stream_t stream, void *user, const char *name,
        uint8_t type, uint16_t length, const void *value);
    uint16_t length;
    const void *value;

    rts_assert(v->type == BUFFER_GET_TYPE(id));

    /* copy to a local so that it does not change after the check */
    func = var->func;
    if (func) {
        if (v->type == RTS_VALUE_TYPE_NUMBER) {
            length = sizeof(v->number.data);
            value = &v->number.data;
        } else {
            length = rts_buffer_size(&v->buffer);
            value = rts_buffer_data(&v->buffer, 0);
        }
        func(stream, stream->user,
            var->name, v->type, length, value);
    }
}

static inline struct rts_value *
rts_vm_push(int type, struct rts_value *v)
{
    rts_buffer_init(&v->buffer);
    v->type = type;
    return v;
}

int
rts_vm_pop(struct rts_vm *vm, struct rts_value *res)
{
    if (vm->sp == 0) {
        return -1;
    } else {
        *res = vm->s[--vm->sp];
    }
    return 0;
}


/*
 * rts_vm_exec()
 *
 * Virtual machine bytecode interpreter.
 */
int
rts_vm_exec(struct rts_vm *vm, unsigned pc, struct rts_data *data, struct rts_buffer *buffer)
{
    int32_t immv;
    int res;
    int64_t number;
    struct rts_bundle *bundle = vm->thread->bundle;
    struct rts_pool *mp = &vm->thread->mp;
    unsigned char *code = bundle->code;
    struct rts_value *v, *s = vm->s;

    #define PUSH(type) (v = rts_vm_push(type, &s[vm->sp++]))
    #define POP() (v = &s[--vm->sp])

    for (;;) {
        switch (code[pc++]) {
            case HALT:
                return RTS_VM_YIELD;
            case SKIP: {
                unsigned short skip, remaining;
                POP();
                remaining = get_adjusted_skip(v->number.data,
                    rts_buffer_size(buffer), &skip);
                if (skip) {
                    data->offset[data->flags & DATA_FLAG_EXT] += skip;
                    if (data->flags & DATA_FLAG_INV)
                        buffer->off -= skip;
                    else
                        buffer->off += skip;
                    buffer->len -= skip;
                }
                if (remaining) {
                    PUSH(RTS_VALUE_TYPE_NUMBER);
                    v->number.data = remaining;
                    vm->resume = pc - 1;
                    return RTS_VM_YIELD;
                }
                break;
            }
            case YANK: {
                unsigned short skip, remaining;
                POP();
                remaining = get_adjusted_skip(v->number.data,
                    rts_buffer_size(buffer), &skip);
                if (skip) {
                    rts_vm_buffer_capture(vm, RTS_VALUE_TYPE_BINARY << BUFFER_TYPE_SHIFT,
                        buffer, (data->flags & DATA_FLAG_INV), skip);
                    data->offset[data->flags & DATA_FLAG_EXT] += skip;
                    if (data->flags & DATA_FLAG_INV)
                        buffer->off -= skip;
                    else
                        buffer->off += skip;
                    buffer->len -= skip;
                }
                if (remaining) {
                    PUSH(RTS_VALUE_TYPE_NUMBER);
                    v->number.data = remaining;
                    vm->resume = pc - 1;
                    return RTS_VM_YIELD;
                }
                break;
            }
            case GOTO:
                data->state = read32(&code[pc]); pc += sizeof(immv);
                break;
            case POPN:
                POP();
                break;
            case POPB:
                POP();
                rts_buffer_exit(&v->buffer, mp);
                break;
            case PNUM1:
                number = (int8_t)code[pc]; pc += 1;
                PUSH(RTS_VALUE_TYPE_NUMBER);
                v->number.data = number;
                break;
            case PNUM2:
                number = read16(&code[pc]); pc += 2;
                PUSH(RTS_VALUE_TYPE_NUMBER);
                v->number.data = number;
                break;
            case PNUM4:
                number = read32(&code[pc]); pc += 4;
                PUSH(RTS_VALUE_TYPE_NUMBER);
                v->number.data = number;
                break;
            case PNUM8:
                number = read64(&code[pc]); pc += 8;
                PUSH(RTS_VALUE_TYPE_NUMBER);
                v->number.data = number;
                break;
            case PSTR:
                immv = read32(&code[pc]); pc += sizeof(immv);
                PUSH(RTS_VALUE_TYPE_STRING);
                if (!rts_buffer_write(&v->buffer, &code[pc], immv, mp)) {
                    return RTS_VM_ERROR;
                }
                pc += immv;
                break;
            case PBIN:
                immv = read32(&code[pc]); pc += sizeof(immv);
                PUSH(RTS_VALUE_TYPE_BINARY);
                if (!rts_buffer_write(&v->buffer, &code[pc], immv, mp)) {
                    return RTS_VM_ERROR;
                }
                pc += immv;
                break;
            case SHMR:
                immv = read32(&code[pc]); pc += sizeof(immv);
                rts_vm_shm_read(vm, PUSH(RTS_VALUE_TYPE_BINARY), immv);
                break;
            case LOAD:
                immv = read32(&code[pc]); pc += sizeof(immv);
                if (!heap_load(&vm->list, PUSH(BUFFER_GET_TYPE(immv)), immv, mp)) {
                    return RTS_VM_ERROR;
                }
                break;
            case SCAN: {
                struct rts_data sdata;
                int32_t ctx_id;
                unsigned short prev_sp;
                long long ctx_value = 0;

                /* retrieve context variable id if set */
                ctx_id = read32(&code[pc]); pc += sizeof(ctx_id);
                if (ctx_id) {
                    struct rts_value ctx;
                    if (BUFFER_GET_TYPE(ctx_id) != RTS_VALUE_TYPE_NUMBER) {
                        return RTS_VM_ERROR;
                    }
                    if (!heap_load(&vm->list, &ctx, ctx_id, mp))
                        return RTS_VM_ERROR;
                    ctx_value = ctx.number.data;
                    rts_assert(SCANCTX_GET_STATE(ctx_value) >= 0);
                    rts_assert(SCANCTX_GET_OFF0(ctx_value) >= 0);
                    rts_assert(SCANCTX_GET_OFF1(ctx_value) >= 0);
                }

                immv = read32(&code[pc]); pc += sizeof(immv);

                /* must check full value, since we do not want to reset if
                 * offsets are non-zero */
                if (ctx_value == 0) {
                    SCANCTX_SET_STATE(ctx_value, immv);
                }
                rts_data_init(&sdata, SCANCTX_GET_STATE(ctx_value));
                sdata.offset[0] = SCANCTX_GET_OFF0(ctx_value);
                sdata.offset[1] = SCANCTX_GET_OFF1(ctx_value);

                immv = read32(&code[pc]); pc += sizeof(immv);
                sdata.flags = immv;
                v = &s[vm->sp-1];
                prev_sp = vm->sp;

                res = rts_vm_scan_helper(vm, &sdata, &v->buffer);
                if (res < 0)
                    return RTS_VM_ERROR;

                rts_assert_msg(prev_sp == vm->sp && vm->resume == 0,
                        "vm->resume=%u, state=%u", vm->resume, sdata.state);
                if (prev_sp != vm->sp || vm->resume != 0)
                    return RTS_VM_ERROR;

                rts_buffer_exit(&v->buffer, mp);
                POP();
                PUSH(RTS_VALUE_TYPE_NUMBER);
                v->number.data = res;

                /* save context */
                if (ctx_id) {
                    struct rts_value ctx;
                    ctx.type = RTS_VALUE_TYPE_NUMBER;

                    rts_assert(sdata.offset[0] <= SCANCTX_OFF_MASK);
                    rts_assert(sdata.offset[1] <= SCANCTX_OFF_MASK);

                    ctx_value = 0;
                    SCANCTX_SET_STATE(ctx_value, sdata.state);
                    SCANCTX_SET_OFF0(ctx_value, sdata.offset[0]);
                    SCANCTX_SET_OFF1(ctx_value, sdata.offset[1]);

                    ctx.number.data = ctx_value;
                    if (!heap_save(&vm->list, &ctx, ctx_id, mp))
                        return RTS_VM_ERROR;
                    rts_assert((BUFFER_GET_FLAG(ctx_id) & BUFFER_FLAG_EXPORT) == 0);
                }

                break;
            }
            case STORE:
                immv = read32(&code[pc]); pc += sizeof(immv);
                v = &s[vm->sp-1];
                if (!heap_save(&vm->list, v, immv, mp))
                    return RTS_VM_ERROR;
                if (BUFFER_GET_FLAG(immv) & BUFFER_FLAG_EXPORT)
                    rts_vm_value_publish(vm, v, immv);
                break;
            case DROP:
                immv = read32(&code[pc]); pc += sizeof(immv);
                if (immv < 0) {
                    heap_reset(&vm->list, mp);
                } else {
                    heap_drop(&vm->list, immv, mp);
                }
                break;
            case PEEK: {
                int peek_len, seek_off, max_len;
                POP(); seek_off = v->number.data;
                POP(); peek_len = v->number.data;

                rts_assert(peek_len >= 0);
                if (peek_len < 0)
                    peek_len = 0;

                if (data->flags & DATA_FLAG_INV) {
                    seek_off = -seek_off - peek_len;

                    /* If the adjusted seek_off + peek_len go past 0, then it is
                     * reaching back, and an invert buffer does not track the
                     * valid range, so assume the data offset gives the valid
                     * length backwards (not a valid assumption outside a
                     * one-shot scan) */
                    max_len = data->offset[data->flags & DATA_FLAG_EXT];
                } else {
                    max_len = buffer->len;
                }

                PUSH(RTS_VALUE_TYPE_BINARY);

                /* If any of these fail, just return an empty string */
                rts_assert_msg(buffer->off + seek_off >= 0,
                        "buffer->off=%u, seek_off=%d, pc=%u",
                        buffer->off, seek_off, pc - 1);
                rts_assert_msg(seek_off + peek_len <= max_len,
                        "seek_off=%d, peek_len=%d, max_len=%d, pc=%u",
                        seek_off, peek_len, max_len, pc - 1);
                if (buffer->off + seek_off >= 0 &&
                        seek_off + peek_len <= max_len) {
                    rts_buffer_copy(&v->buffer, buffer, mp);
                    if (rts_buffer_will_sync(buffer))
                        vm->sync++;
                    v->buffer.off += seek_off;
                    v->buffer.len = peek_len;
                }
                break;
            }
            case SEEK: {
                int seek_off, max_len;
                POP(); seek_off = v->number.data;

                if (data->flags & DATA_FLAG_INV) {
                    seek_off = -seek_off;
                    max_len = data->offset[data->flags & DATA_FLAG_EXT];
                } else {
                    max_len = buffer->len;
                }
                if (buffer->off + seek_off < 0 ||
                        seek_off > max_len)
                    return RTS_VM_ERROR;

                /* return current offset */
                PUSH(RTS_VALUE_TYPE_NUMBER);
                v->number.data = data->offset[data->flags & DATA_FLAG_EXT];
                /* update */
                if (data->flags & DATA_FLAG_INV) {
                    data->offset[data->flags & DATA_FLAG_EXT] -= seek_off;
                    buffer->len += seek_off;
                    buffer->off += seek_off;
                } else {
                    data->offset[data->flags & DATA_FLAG_EXT] += seek_off;
                    buffer->len -= seek_off;
                    buffer->off += seek_off;
                }
                break;
            }
            case IADD:
                s[vm->sp-2].number.data = s[vm->sp-2].number.data + s[vm->sp-1].number.data;
                POP();
                break;
            case ISUB:
                s[vm->sp-2].number.data = s[vm->sp-2].number.data - s[vm->sp-1].number.data;
                POP();
                break;
            case IMUL:
                s[vm->sp-2].number.data = s[vm->sp-2].number.data * s[vm->sp-1].number.data;
                POP();
                break;
            case IDIV:
                s[vm->sp-2].number.data = s[vm->sp-2].number.data / s[vm->sp-1].number.data;
                POP();
                break;
            case IEQL:
                s[vm->sp-2].number.data = s[vm->sp-2].number.data == s[vm->sp-1].number.data;
                POP();
                break;
            case INEQ:
                s[vm->sp-2].number.data = s[vm->sp-2].number.data != s[vm->sp-1].number.data;
                POP();
                break;
            case ISHL:
                s[vm->sp-2].number.data = s[vm->sp-2].number.data << s[vm->sp-1].number.data;
                POP();
                break;
            case ISHR:
                s[vm->sp-2].number.data = s[vm->sp-2].number.data >> s[vm->sp-1].number.data;
                POP();
                break;
            case ILT:
                s[vm->sp-2].number.data = s[vm->sp-2].number.data  < s[vm->sp-1].number.data;
                POP();
                break;
            case IGT:
                s[vm->sp-2].number.data = s[vm->sp-2].number.data  > s[vm->sp-1].number.data;
                POP();
                break;
            case   OR:
                s[vm->sp-2].number.data = s[vm->sp-2].number.data  | s[vm->sp-1].number.data;
                POP();
                break;
            case  AND:
                s[vm->sp-2].number.data = s[vm->sp-2].number.data  & s[vm->sp-1].number.data;
                POP();
                break;
            case  NOT:
                s[vm->sp-1].number.data = ~(s[vm->sp-1].number.data);
                break;
            case  XOR:
                s[vm->sp-2].number.data = s[vm->sp-2].number.data  ^ s[vm->sp-1].number.data;
                POP();
                break;
            case SEQL:
                immv = rts_buffer_eql(&s[vm->sp-2].buffer, &s[vm->sp-1].buffer);
                POP(); rts_buffer_exit(&v->buffer, mp);
                POP(); rts_buffer_exit(&v->buffer, mp);
                PUSH(RTS_VALUE_TYPE_NUMBER);
                v->number.data = immv;
                break;
            case SNEQ:
                immv = rts_buffer_neq(&s[vm->sp-2].buffer, &s[vm->sp-1].buffer);
                POP(); rts_buffer_exit(&v->buffer, mp);
                POP(); rts_buffer_exit(&v->buffer, mp);
                PUSH(RTS_VALUE_TYPE_NUMBER);
                v->number.data = immv;
                break;
            case SCAT:
                if (!rts_buffer_append(&s[vm->sp-2].buffer, &s[vm->sp-1].buffer, mp)) {
                    return RTS_VM_ERROR;
                }
                POP(); rts_buffer_exit(&v->buffer, mp);
                break;
            case SLEN:
                immv = rts_buffer_size(&s[vm->sp-1].buffer);
                POP(); rts_buffer_exit(&v->buffer, mp);
                PUSH(RTS_VALUE_TYPE_NUMBER);
                v->number.data = immv;
                break;
            case SLCE: {
                unsigned off;
                  signed end;
                POP(); end = v->number.data;
                POP(); off = v->number.data;

                if (off >= rts_buffer_size(&s[vm->sp-1].buffer)) {
                    rts_buffer_clear(&s[vm->sp-1].buffer, mp);
                } else {
                    s[vm->sp-1].buffer.off += off;
                    if (end < 0) {
                        if ((signed)(rts_buffer_size(&s[vm->sp-1].buffer) + end) < (signed)off)
                            rts_buffer_clear(&s[vm->sp-1].buffer, mp);
                        else
                            s[vm->sp-1].buffer.len = s[vm->sp-1].buffer.len + end - off;
                    } else {
                        if ((unsigned)end > rts_buffer_size(&s[vm->sp-1].buffer))
                            end = rts_buffer_size(&s[vm->sp-1].buffer);
                        else if ((unsigned)end < off)
                            end = off;
                        s[vm->sp-1].buffer.len = end - off;
                    }
                }
                break;
            }
            case BANG:
                s[vm->sp-1].number.data = !s[vm->sp-1].number.data;
                break;
            case JUMP:
                immv = read32(&code[pc]); pc += immv;
                break;
            case BREZ:
                immv = sizeof(immv); /* skip */
                rts_assert(s[vm->sp-1].type == RTS_VALUE_TYPE_NUMBER);
                if (s[vm->sp-1].number.data == 0)
                    immv = read32(&code[pc]);
                pc += immv;
                POP();
                break;
            case BREZNP:
                immv = sizeof(immv); /* skip */
                rts_assert(s[vm->sp-1].type == RTS_VALUE_TYPE_NUMBER);
                if (s[vm->sp-1].number.data == 0)
                    immv = read32(&code[pc]);
                pc += immv;
                break;
            case BRNEZNP:
                immv = sizeof(immv); /* skip */
                rts_assert(s[vm->sp-1].type == RTS_VALUE_TYPE_NUMBER);
                if (s[vm->sp-1].number.data != 0)
                    immv = read32(&code[pc]);
                pc += immv;
                break;
            case BTOI:
                btoi_instr(&s[vm->sp-1], mp);
                break;
            case ITOB:
                itob_instr(&s[vm->sp-1], mp);
                break;
            case ITOA:
                itoa_instr(&s[vm->sp-1], mp);
                break;
            case ATOI:
                atoi_instr(&s[vm->sp-1], mp);
                break;
            case ATOB:
                atob_instr(&s[vm->sp-1], mp);
                break;
            case BTOA:
                btoa_instr(&s[vm->sp-1], mp);
                break;
            case HTOI:
                htoi_instr(&s[vm->sp-1], mp);
                break;
            case PRNT: {
                const void *fmt;
                immv = read32(&code[pc]); pc += sizeof(immv);
                fmt = &code[pc]; pc += immv;
                immv = read32(&code[pc]); pc += sizeof(immv);
                rts_vm_printf(fmt, immv, &s[vm->sp-immv], mp);
                break;
            }
            case OFFSET:
                immv = read32(&code[pc]); pc += sizeof(immv);
                rts_assert(immv == 0 || immv == 1);
                PUSH(RTS_VALUE_TYPE_NUMBER);
                v->number.data = data->offset[immv & 1];
                break;
            case REMAINING:
                PUSH(RTS_VALUE_TYPE_NUMBER);
                v->number.data = rts_buffer_size(buffer);
                break;
            case EXPECT:
                rts_vm_expect(vm, 7, &s[vm->sp-7]);
                POP();
                POP();
                POP();
                POP(); rts_buffer_exit(&v->buffer, mp);
                POP();
                POP(); rts_buffer_exit(&v->buffer, mp);
                POP();
                break;
            case DICT:
                immv = read32(&code[pc]); pc += sizeof(immv);
                if (immv) {
                    rts_dict_save(vm->thread, &s[vm->sp-3].buffer, &s[vm->sp-2].buffer, s[vm->sp-1].number.data * 1000);
                    POP();
                    POP(); rts_buffer_exit(&v->buffer, mp);
                    POP(); rts_buffer_exit(&v->buffer, mp);
                } else {
                    rts_dict_find(vm->thread, &s[vm->sp-1].buffer, &s[vm->sp-1].buffer);
                }
                break;
            case TIME:
                PUSH(RTS_VALUE_TYPE_NUMBER);
                v->number.data = vm->thread->timestamp;
                break;
            case NOOP:
                break;
            default:
                rts_assert_msg(0, "bad code %d", code[pc-1]);
                return RTS_VM_ERROR;
        }
    }
}

