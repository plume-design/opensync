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

#include "rts.h"
#include "rts_priv.h"
#include "rts_common.h"
#include "rts_vm.h"
#include "rts_msg.h"
#include "rts_buffer.h"
#include "rts_config.h"
#include "rts_lruhash.h"
#include "rts_ft.h"
#include "rts_slob.h"
#include "rts_lock.h"
#include "memutil.h"
#include "log.h"

#ifdef KERNEL
#include <linux/errno.h>
#else
#include <errno.h>
#include <stdlib.h>
#endif

struct rts_file {
    const unsigned char *buf;
    size_t len;
    size_t off;
};

static size_t
rts_read(struct rts_file *f, void *buf, size_t len)
{
    size_t rlen = rts_min(len, f->len - f->off);
    if (rlen) {
        __builtin_memcpy(buf, &f->buf[f->off], rlen);
        f->off += rlen;
    }
    return rlen;
}

static unsigned
read_section(struct rts_file *f, unsigned *len)
{
    unsigned sec = 0, ret = 0;
    ret += rts_read(f, &sec, sizeof(sec));
    ret += rts_read(f, len, sizeof(*len));
    rts_assert (ret == 8);

    *len = be32toh(*len);
    return be32toh(sec);
}

static int
itab_ntoh(void *data, unsigned size)
{
    unsigned i, iptr, end;
    unsigned *idata;
    struct rts_itab *p = data;

    rts_assert(size >= sizeof(p->size));
    p->size = be32toh(p->size);
    if (size < sizeof(p->size) + p->size * sizeof(p->data[0]))
        return -EINVAL;

    idata = p->data[p->size-1].data;

    for (i = 0; i < p->size; i++) {
        p->data[i].length = be32toh(p->data[i].length);
        p->data[i].offset = be32toh(p->data[i].offset);

        end = p->data[i].offset + p->data[i].length;
        if (p->data[i].length > 0 && (char*)(idata + end - 1) >= (char*)data + size)
                return -EINVAL;

        for (iptr = p->data[i].offset; iptr < end; iptr++)
            idata[iptr] = be32toh(idata[iptr]);
    }

    return 0;
}

static int
stab_ntoh(void *data, unsigned size)
{
    struct rts_stab *s = data;

    rts_assert(size >= sizeof(s->size));
    s->size = be32toh(s->size);

    return size != sizeof(s->size) + s->size * RTS_STAB_MAXLEN ? -EINVAL : 0;
}

static int
vars_ntoh(void *data, unsigned size)
{
    struct rts_var *p = data;
    while (size >= sizeof(*p)) {
        p->name = 0;
        p->func = 0;
        size -= sizeof(*p);
        ++p;
    }
    return size > 0 ? -EINVAL : 0;
}

static int
auto_map_ntoh(void *data, unsigned size)
{
    struct rts_state_map *s = data;
    while (size >= sizeof(*s)) {
        s->id = be32toh(s->id);
        s->pad0 = 0;
        rts_bitset_bswap(&s->map);
        size -= sizeof(*s);
        ++s;
    }
    return size > 0 ? -EINVAL : 0;
}

static int
auto_ran_ntoh(void *data, unsigned size)
{
    struct rts_state_ran *s = data;
    while (size >= sizeof(*s)) {
        s->id   = be32toh(s->id);
        s->base = be16toh(s->base);
        s->end  = be16toh(s->end);
        size -= sizeof(*s);
        ++s;
    }
    return size > 0 ? -EINVAL : 0;
}

static int
tran8_ntoh(void *data, unsigned size)
{
    struct rts_tran8 *t = data;
    while (size >= sizeof(*t)) {
        t->dst = be32toh(t->dst);
        t->cap = be16toh(t->cap);
        t->fun = be16toh(t->fun);
        size -= sizeof(*t);
        ++t;
    }
    return size > 0 ? -EINVAL : 0;
}

static int
tran4fc_ntoh(void *data, unsigned size)
{
    struct rts_tran4fc *t = data;
    while (size >= sizeof(*t)) {
        t->dst = be16toh(t->dst);
        size -= sizeof(*t);
        ++t;
    }
    return size > 0 ? -EINVAL : 0;
}

static int
tran4f_ntoh(void *data, unsigned size)
{
    struct rts_tran4f *t = data;
    while (size >= sizeof(*t)) {
        t->dst = be16toh(t->dst);
        t->fun = be16toh(t->fun);
        size -= sizeof(*t);
        ++t;
    }
    return size > 0 ? -EINVAL : 0;
}

static int
tran4c_ntoh(void *data, unsigned size)
{
    struct rts_tran4c *t = data;
    while (size >= sizeof(*t)) {
        t->dst = be16toh(t->dst);
        t->cap = be16toh(t->cap);
        size -= sizeof(*t);
        ++t;
    }
    return size > 0 ? -EINVAL : 0;
}

static int
tran2_ntoh(void *data, unsigned size)
{
    struct rts_tran2 *t = data;
    while (size >= sizeof(*t)) {
        t->dst = be16toh(t->dst);
        size -= sizeof(*t);
        ++t;
    }
    return size > 0 ? -EINVAL : 0;
}

static bool
set_bundle_var_names(const struct rts_bundle *bundle)
{
    const char *ke, *kp;
    unsigned index;
    int i;

    if (!bundle->vars || !bundle->keylist)
        return false;

    ke = kp = bundle->keylist;
    while (*kp != '\0') {
        while (*ke != '\0')
            ke++;

        index = rts_atoi(ke+1);
        rts_assert(index < bundle->numvars);
        bundle->vars[index].name = kp;

        i = 0;
        while (i < 2) {
            if (*++ke == '\0')
                ++i;
        }

        kp = ++ke;
    }

    return true;
}

static int
bundle_load(struct rts_bundle **bundlep, const unsigned char *buf, size_t len)
{
    static unsigned loads;
    struct rts_bundle *bundle;
    int res = -EINVAL;
    unsigned section, size;
    void *data;
    char magic[4];
    unsigned char version[4];
    struct rts_file *f, file = {
        .buf = buf,
        .off = 0,
        .len = len
    };

    f = &file;

    if (rts_read(f, magic, sizeof(magic)) != sizeof(magic))
        return -EINVAL;

    if (rts_read(f, version, sizeof(version)) != sizeof(version))
        return -EINVAL;

    if (rts_strncmp(magic, "RTS", 4)) {
        rts_printf("error: corrupt file: bad magic\n");
        return -EINVAL;
    }

    if (version[0] != RTS_MAJOR || version[1] != RTS_MINOR) {
        rts_printf("error: incompatible version [binfile %d.%d.%d, runtime %d.%d.%d]\n",
            version[0], version[1], version[2], RTS_MAJOR, RTS_MINOR, RTS_PATCH);
        return -EINVAL;
    }

    bundle = rts_ext_alloc(sizeof(*bundle));
    if (!bundle) {
        return -ENOMEM;
    }

    bundle->refcount = 0;
    bundle->generation = __sync_add_and_fetch(&loads, 1);
    bundle->numvars = 0;
    bundle->vars = NULL;
    bundle->code = NULL;
    bundle->dfa.sm = NULL;
    bundle->dfa.sr = NULL;
    bundle->dfa.num_sr = 0;
    bundle->ctab = NULL;
    bundle->ftab = NULL;
    bundle->stab = NULL;
    bundle->trans.t8 = NULL;
    bundle->trans.t4fc = NULL;
    bundle->trans.t4f = NULL;
    bundle->trans.t4c = NULL;
    bundle->trans.t2 = NULL;
    bundle->keylist = NULL;

    for (;;) {
        section = read_section(f, &size);
        if (!section)
            break;

        if (!(data = rts_ext_alloc(size))) {
            res = -ENOMEM;
            goto bundle_cleanup;
        }

        if (rts_read(f, data, size) != size) {
            res = -EINVAL;
            rts_ext_free(data);
            goto bundle_cleanup;
        }

        switch (section) {
            case RTS_SECTION_VARS:
                bundle->vars = data;
                bundle->numvars = size / sizeof(*bundle->vars);
                if ((res = vars_ntoh(data, size)))
                    goto bundle_cleanup;
                break;
            case RTS_SECTION_TEXT:
                bundle->code = data;
                bundle->codelen = size;
                break;
            case RTS_SECTION_AUTM:
                bundle->dfa.sm = data;
                if ((res = auto_map_ntoh(data, size)))
                    goto bundle_cleanup;
                break;
            case RTS_SECTION_AUTR:
                bundle->dfa.sr = data;
                bundle->dfa.num_sr = size / sizeof(*bundle->dfa.sr);
                if ((res = auto_ran_ntoh(data, size)))
                    goto bundle_cleanup;
                break;
            case RTS_SECTION_CTAB:
                bundle->ctab = data;
                if ((res = itab_ntoh(data, size)))
                    goto bundle_cleanup;
                break;
            case RTS_SECTION_FTAB:
                bundle->ftab = data;
                if ((res = itab_ntoh(data, size)))
                    goto bundle_cleanup;
                break;
            case RTS_SECTION_STAB:
                bundle->stab = data;
                if ((res = stab_ntoh(data, size)))
                    goto bundle_cleanup;
                break;
            case RTS_SECTION_TRT0:
                bundle->trans.t8 = data;
                if ((res = tran8_ntoh(data, size)))
                    goto bundle_cleanup;
                break;
            case RTS_SECTION_TRT1:
                bundle->trans.t4fc = data;
                if ((res = tran4fc_ntoh(data, size)))
                    goto bundle_cleanup;
                break;
            case RTS_SECTION_TRT2:
                bundle->trans.t4f = data;
                if ((res = tran4f_ntoh(data, size)))
                    goto bundle_cleanup;
                break;
            case RTS_SECTION_TRT3:
                bundle->trans.t4c = data;
                if ((res = tran4c_ntoh(data, size)))
                    goto bundle_cleanup;
                break;
            case RTS_SECTION_TRT4:
                bundle->trans.t2 = data;
                if ((res = tran2_ntoh(data, size)))
                    goto bundle_cleanup;
                break;
            case RTS_SECTION_KEYS:
                bundle->keylist = data;
                break;
            default:
                res = -EINVAL;
                rts_ext_free(data);
                goto bundle_cleanup;
        }
    }

    if (!set_bundle_var_names(bundle))
    {
        res = -1;
        goto bundle_cleanup;
    }

    *bundlep = bundle;
    return 0;

bundle_cleanup:
    LOGE("%s:%d: error: bundle_load failed", __func__, __LINE__);
    if (bundle->vars)
        rts_ext_free(bundle->vars);
    if (bundle->code)
        rts_ext_free(bundle->code);
    if (bundle->dfa.sm)
        rts_ext_free(bundle->dfa.sm);
    if (bundle->dfa.sr)
        rts_ext_free(bundle->dfa.sr);
    if (bundle->ctab)
        rts_ext_free(bundle->ctab);
    if (bundle->ftab)
        rts_ext_free(bundle->ftab);
    if (bundle->stab)
        rts_ext_free(bundle->stab);
    if (bundle->trans.t8)
        rts_ext_free(bundle->trans.t8);
    if (bundle->trans.t4fc)
        rts_ext_free(bundle->trans.t4fc);
    if (bundle->trans.t4f)
        rts_ext_free(bundle->trans.t4f);
    if (bundle->trans.t4c)
        rts_ext_free(bundle->trans.t4c);
    if (bundle->trans.t2)
        rts_ext_free(bundle->trans.t2);
    if (bundle->keylist)
        rts_ext_free((void *)bundle->keylist);
    rts_ext_free(bundle);
    return res;
}

static void
rts_flow_flush(struct rts_lruhash *flow, struct rts_pool *mp)
{
    if (!rts_list_empty(&flow->lru)) {
        struct rts_lruhash_item *ptr, *tmp;
        rts_list_for_each_entry_safe(ptr, tmp, &flow->lru, lru) {
            rts_lruhash_remove(ptr);
            rts_pool_free(mp, rts_container_of(ptr, struct rts_ftentry, hash));
        }
    }
}

static inline void
rts_flow_check_expiry(struct rts_thread *thread)
{
    struct rts_ftentry *e;
    struct rts_lruhash_item *item;
    struct rts_lruhash *ft = thread->flow;

    while ((item = rts_lruhash_expire(ft, thread->timestamp)) != 0) {
        e = rts_container_of(item, struct rts_ftentry, hash);
        rts_pool_free(&thread->mp, e);
    }
}

struct rts_msg_flow {
    struct rts_mpmc_node node;
    struct rts_ftentry entry;
    uint32_t ttl;
};

void
rts_msg_flow_free(struct rts_mpmc_node *node)
{
    struct rts_msg_flow *msg = rts_container_of(node, struct rts_msg_flow, node);
    rts_ext_free(msg);
}

void
rts_msg_flow_save(const struct rts_mpmc_node *node, void *arg)
{
    struct rts_ftentry *entry;
    struct rts_thread *thread = arg;
    struct rts_msg_flow *msg = rts_container_of(node, struct rts_msg_flow, node);

    rts_flow_check_expiry(thread);

    if (!(entry = rts_pool_alloc(&thread->mp, sizeof(*entry))))
        return;

    __builtin_memcpy(entry, &msg->entry, sizeof(msg->entry));

    rts_ft_save(thread->flow, entry, msg->ttl, thread->timestamp);
}

void
rts_flow_save(struct rts_thread *thread, uint8_t proto,
    const void *sbuf, unsigned slen, uint16_t sport,
    const void *dbuf, unsigned dlen, uint16_t dport, uint32_t pc, int32_t ttl)
{
    struct rts_ftentry *entry;
    struct rts_ipaddr saddr, daddr;

    /*
     * 1.) Trap request must satisfy some minimum non-zero field count requirement.
     * 2.) If an address is specified, it must have a valid ip4 or ip6 length and
     *     must not be a any-address equivalent. (any address is specified with a
     *     zero length)
     */
    if (!sport && !dport && !slen && !dlen)
        return;
    /* invalid ttl */
    else if (ttl < 0)
        return;
    /* default ttl */
    else if (ttl == 0)
        ttl = thread->flow->expiry;
    /* in msecs */
    else
        ttl *= 1000;

    if (slen) {
        if (slen == 4)
            rts_ipaddr_copy_in4(&saddr, sbuf);
        else if (slen == 16)
            rts_ipaddr_copy_in6(&saddr, sbuf);
        else
            return;

        if (rts_ipaddr_unspec(&saddr))
            return;

    } else {
        rts_ipaddr_init(&saddr);
    }

    if (dlen) {
        if (dlen == 4)
            rts_ipaddr_copy_in4(&daddr, dbuf);
        else if (dlen == 16)
            rts_ipaddr_copy_in6(&daddr, dbuf);
        else
            return;

        if (rts_ipaddr_unspec(&daddr))
            return;

    } else {
        rts_ipaddr_init(&daddr);
    }

    rts_flow_check_expiry(thread);

    if (!(entry = rts_pool_alloc(&thread->mp, sizeof(*entry))))
        return;

    rts_ipaddr_copy(&entry->saddr, &saddr);
    rts_ipaddr_copy(&entry->daddr, &daddr);
    entry->sport = sport;
    entry->dport = dport;
    entry->proto = proto;
    entry->pc = pc;

    rts_ft_save(thread->flow, entry, ttl, thread->timestamp);

    if (rts_msg_broadcast(thread)) {
        struct rts_msg_flow *msg = rts_ext_alloc(sizeof(*msg));
        if (msg) {
            msg->ttl = ttl;
            __builtin_memcpy(&msg->entry, entry, sizeof(msg->entry));
            rts_msg_schedule(&mq, &msg->node, thread->pid, thread->group, rts_msg_flow_save, rts_msg_flow_free);
        }
    }
}

struct dict_entry {
    struct rts_lruhash_item hash;
    struct rts_buffer key;
    struct rts_buffer val;
};

static uint32_t
dict_entry_hash(const void *ptr)
{
    uint32_t i, sum = 0;
    const struct rts_buffer *key = ptr;
    for (i = 0; i < rts_buffer_size(key); i++)
        sum += rts_buffer_at(key, i);
    return sum;
}

static bool
dict_entry_equal(const void *lhs, const struct rts_lruhash_item *rhs)
{
    const struct rts_buffer *l = lhs;
    const struct dict_entry *r = rts_container_of(rhs, struct dict_entry, hash);
    return rts_buffer_eql(l, &r->key);
}

static void
__rts_dict_save(struct rts_lruhash *h, struct rts_buffer *key, struct rts_buffer *val, uint32_t ttl,
    uint64_t timestamp, struct rts_pool *mp)
{
    struct dict_entry *e;
    uint32_t hashval = dict_entry_hash(key);
    struct rts_lruhash_item *item = rts_lruhash_find(h, key, hashval, timestamp);
    if (item) {
        item->ttl = ttl;
        e = rts_container_of(item, struct dict_entry, hash);
        rts_buffer_clone(&e->val, val, mp);
    } else if ((e = rts_pool_alloc(mp, sizeof(*e)))) {
        rts_buffer_init(&e->key);
        rts_buffer_init(&e->val);
        rts_buffer_clone(&e->key, key, mp);
        rts_buffer_clone(&e->val, val, mp);
        rts_lruhash_insert(h, &e->hash, hashval, ttl, timestamp);
    }

    while ((item = rts_lruhash_expire(h, timestamp)) != 0) {
        e = rts_container_of(item, struct dict_entry, hash);
        rts_buffer_exit(&e->key, mp);
        rts_buffer_exit(&e->val, mp);
        rts_pool_free(mp, e);
    }
}

struct rts_msg_dict {
    struct rts_mpmc_node node;
    uint32_t ttl;
    unsigned short key;
    unsigned short val;
    unsigned char data[0];
};

void
rts_msg_dict_free(struct rts_mpmc_node *node)
{
    struct rts_msg_dict *msg = rts_container_of(node, struct rts_msg_dict, node);
    rts_ext_free(msg);
}

void
rts_msg_dict_save(const struct rts_mpmc_node *node, void *arg)
{
    struct rts_buffer key, val;
    struct rts_thread *thread = arg;
    struct rts_msg_dict *msg = rts_container_of(node, struct rts_msg_dict, node);

    rts_buffer_init(&key);
    rts_buffer_init(&val);
    rts_buffer_write(&key, &msg->data[0]       , msg->key, &thread->mp);
    rts_buffer_write(&val, &msg->data[msg->key], msg->val, &thread->mp);

    __rts_dict_save(thread->dict, &key, &val, msg->ttl, thread->timestamp, &thread->mp);

    rts_buffer_exit(&key, &thread->mp);
    rts_buffer_exit(&val, &thread->mp);
}

void
rts_dict_save(struct rts_thread *thread, struct rts_buffer *key, struct rts_buffer *val, int32_t ttl)
{
    struct rts_msg_dict *msg;
    int sz;

    if (ttl <= 0)
        return;

    if (rts_msg_broadcast(thread)) {
        sz = sizeof(*msg) + rts_buffer_size(key) + rts_buffer_size(val);
        msg = rts_ext_alloc(sz);
        if (!msg)
            return;
        msg->ttl = ttl;
        msg->key = rts_buffer_size(key);
        msg->val = rts_buffer_size(val);
        __builtin_memcpy(&msg->data[0]       , rts_buffer_data(key, 0), msg->key);
        __builtin_memcpy(&msg->data[msg->key], rts_buffer_data(val, 0), msg->val);
        rts_msg_schedule(&mq, &msg->node, thread->pid, thread->group, rts_msg_dict_save, rts_msg_dict_free);
    }
    __rts_dict_save(thread->dict, key, val, ttl, thread->timestamp, &thread->mp);
}

void
rts_dict_find(struct rts_thread *thread, struct rts_buffer *key, struct rts_buffer *val)
{
    struct rts_lruhash *h = thread->dict;
    struct dict_entry *e;
    struct rts_lruhash_item *item;

    if ((item = rts_lruhash_expire(h, thread->timestamp)) != 0) {
        e = rts_container_of(item, struct dict_entry, hash);
        rts_buffer_exit(&e->key, &thread->mp);
        rts_buffer_exit(&e->val, &thread->mp);
        rts_pool_free(&thread->mp, e);
    }

    item = rts_lruhash_find(h, key, dict_entry_hash(key), thread->timestamp);
    if (item) {
        e = rts_container_of(item, struct dict_entry, hash);
        rts_buffer_copy(val, &e->val, &thread->mp);
    } else {
        rts_buffer_clear(val, &thread->mp);
    }

}

static inline void
rts_bundle_get(struct rts_bundle *bundle)
{
    __sync_add_and_fetch(&bundle->refcount, 1);
}

static inline void
rts_bundle_put(struct rts_bundle *bundle)
{
    if (__sync_sub_and_fetch(&bundle->refcount, 1) == 0) {
        rts_ext_free(bundle->dfa.sm);
        rts_ext_free(bundle->dfa.sr);
        rts_ext_free(bundle->trans.t8);
        rts_ext_free(bundle->trans.t4fc);
        rts_ext_free(bundle->trans.t4f);
        rts_ext_free(bundle->trans.t4c);
        rts_ext_free(bundle->trans.t2);
        rts_ext_free(bundle->vars);
        rts_ext_free(bundle->ctab);
        rts_ext_free(bundle->ftab);
        rts_ext_free(bundle->stab);
        rts_ext_free(bundle->code);
        rts_ext_free((void *)bundle->keylist);
        rts_ext_free(bundle);
    }
}

/*
 * Helper utility to parse the keylist. Keylist entries are in the form
 *
 *     some.value1\01234\02\0some.value2\02334\01...
 *
 * The top-name is the signature that declares the value. The digits that follow
 * reflect the guid assigned to that value. Only exported values are encoded in
 * the keylist, however all values in rtsl have a guid, therefore these digits
 * are not sequential, although they will be in ascending order. The next digits
 * represent value type. The value type can be string, number, binary and etc.
 */
static int
resolve_key_id(const struct rts_bundle *bundle, const char *key)
{
    const char *ke, *kp;
    int i, keylen = rts_strlen(key);

    ke = kp = bundle->keylist;
    while (*kp != '\0') {
        while (*ke != '\0')
            ke++;
        if (ke - kp == keylen && !rts_strncmp(key, kp, ke - kp)) {
            return rts_atoi(++ke);
        }
        i = 0;
        while (i < 2) {
            if (*++ke == '\0')
                ++i;
        }

        kp = ++ke;
    }

    return 0;
}

struct rts_msg_bundle {
    struct rts_mpmc_node node;
    struct rts_bundle *bundle;
};

void
rts_msg_bundle_free(struct rts_mpmc_node *node)
{
    struct rts_msg_bundle *msg = rts_container_of(node, struct rts_msg_bundle, node);
    rts_bundle_put(msg->bundle);
    rts_ext_free(msg);
}

void
rts_msg_bundle_save(const struct rts_mpmc_node *node, void *arg)
{
    struct rts_thread *thread = arg;
    struct rts_msg_bundle *msg = rts_container_of(node, struct rts_msg_bundle, node);

    if (thread->bundle) {
        /* release the old */
        rts_bundle_put(thread->bundle);
        /* flush the flow table */
        rts_flow_flush(thread->flow, &thread->mp);
        /* the thread is without a bundle */
        thread->bundle = NULL;
    }

    if (msg->bundle) {
        rts_bundle_get(msg->bundle);
        thread->bundle = msg->bundle;
    }
}

void
rts_msg_empty_free(struct rts_mpmc_node *node)
{
    rts_ext_free(node);
}

void
rts_msg_empty(const struct rts_mpmc_node *node, void *arg)
{
    (void) node;
    (void) arg;
}

/* This is a new approach where we have a single bundle for multiple groups.
 * To handle this, we reserve the special group 0 to for mpmc multi-group
 * broadcast. This means subscriptions are shared among groups.
 */
static struct rts_bundle *g_bundle;

static int
bundle_release(void)
{
    struct rts_msg_bundle *msg;

    /* Safely dispose of the bundle */
    spinlock_lock(&mq.spinlock);

    if (g_bundle == NULL) {
        spinlock_unlock(&mq.spinlock);
        return -EINVAL;
    }

    /* Broadcast the bundle is going away */
    if (mq.consumer) {
        if (!(msg = rts_ext_alloc(sizeof(*msg)))) {
            spinlock_unlock(&mq.spinlock);
            return -ENOMEM;
        }
        msg->bundle = NULL;
        rts_msg_schedule(&mq, &msg->node, 0, 0, rts_msg_bundle_save, rts_msg_bundle_free);
    }
    rts_bundle_put(g_bundle);
    g_bundle = NULL;
    spinlock_unlock(&mq.spinlock);
    return 0;
}

EXPORT int
rts_load(const void *sig, size_t siglen)
{
    int res;
    struct rts_msg_bundle *msg;
    struct rts_mpmc_node *empty;
    struct rts_bundle *next;

    if (!sig || !siglen)
        return bundle_release();

    if ((res = bundle_load(&next, sig, siglen)) != 0)
        return res;

    if (next->refcount != 0)
    {
        LOGE("%s:%d: Unable to read the signature", __func__, __LINE__);
        return -1;
    }

    rts_bundle_get(next);

    /* The queue must not be pushed to unless there are active consumers, and we
     * cannot update g_bundle without this lock */
    spinlock_lock(&mq.spinlock);

    /* Now if there are threads running, we need to tell them about the update */
    if (mq.consumer) {
        if (!(msg = rts_ext_alloc(sizeof(*msg)))) {
            spinlock_unlock(&mq.spinlock);
            rts_bundle_put(next);
            return -ENOMEM;
        }
        if (!(empty = rts_ext_alloc(sizeof(*empty)))) {
            spinlock_unlock(&mq.spinlock);
            rts_ext_free(msg);
            rts_bundle_put(next);
            return -ENOMEM;
        }

        /* broadcast update to all threads */
        rts_bundle_get(next);
        msg->bundle = next;
        rts_msg_schedule(&mq, &msg->node, 0, 0, rts_msg_bundle_save, rts_msg_bundle_free);

        /* work-around: pump the queue so that the bundle is freed once read */
        rts_msg_schedule(&mq, empty, 0, 0, rts_msg_empty, rts_msg_empty_free);
    }

    if (g_bundle)
        rts_bundle_put(g_bundle);
    g_bundle = next;

    spinlock_unlock(&mq.spinlock);
    return 0;
}

static bool
rts_ftentry_equal(const void *lhs, const struct rts_lruhash_item *rhs)
{
    (void)lhs;
    (void)rhs;
    rts_assert(0);
    return true;
}

static int
rts_thread_rusage(struct rts_thread *thread, struct rts_rusage *rusage)
{
    rusage->curr_alloc = thread->mp.stats.curr_alloc;
    rusage->peak_alloc = thread->mp.stats.peak_alloc; thread->mp.stats.peak_alloc = thread->mp.stats.curr_alloc;
    rusage->fail_alloc += thread->mp.stats.fail_alloc; thread->mp.stats.fail_alloc = 0;
    rusage->mpmc_events += thread->mqh.events; thread->mqh.events = 0;
    rusage->scan_started += thread->scan_started; thread->scan_started = 0;
    rusage->scan_stopped += thread->scan_stopped; thread->scan_stopped = 0;
    rusage->scan_bytes += thread->scan_bytes; thread->scan_bytes = 0;
    return 0;
}

static uint32_t
clp2(uint32_t n)
{
    --n;
    n |= (n >> 1);
    n |= (n >> 2);
    n |= (n >> 4);
    n |= (n >> 8);
    n |= (n >> 16);
    return ++n;
}

static int
rts_thread_create(struct rts_thread **threadp, unsigned group)
{
    static unsigned pid;
    struct rts_thread *thread;
    uint8_t *mem;
    uint32_t dict_buckets, flow_buckets;
    int poolsz;

    if (!threadp)
        return -EINVAL;

    if (rts_handle_dict_hash_bucket <= 0 ||
        rts_handle_flow_hash_bucket <= 0 ||
        rts_handle_memory_size <= 0)
        return -EINVAL;

    dict_buckets = clp2(rts_handle_dict_hash_bucket);
    flow_buckets = clp2(rts_handle_flow_hash_bucket);

    if (sizeof_rts_lruhash(dict_buckets) > INT32_MAX ||
        sizeof_rts_lruhash(flow_buckets) > INT32_MAX)
        return -EINVAL;

    if ((int)sizeof_rts_lruhash(dict_buckets) >= rts_handle_memory_size ||
        (int)sizeof_rts_lruhash(flow_buckets) >= rts_handle_memory_size)
        return -ENOMEM;

    poolsz = rts_handle_memory_size -
        sizeof(*thread) -
        sizeof_rts_lruhash(dict_buckets) -
        sizeof_rts_lruhash(flow_buckets);

    if (poolsz < 0)
        return -ENOMEM;

    if (!(mem = rts_ext_alloc(rts_handle_memory_size)))
        return -ENOMEM;

    thread = (void *)mem;
    mem += sizeof(*thread);

    thread->timestamp = 0;
    thread->scan_bytes = 0;
    thread->scan_started = 0;
    thread->scan_stopped = 0;

    thread->group = group;

    rts_pool_init(&thread->mp, mem, poolsz);
    mem += poolsz;

    thread->dict = (void *)mem;
    mem += sizeof_rts_lruhash(dict_buckets);

    thread->flow = (void *)mem;

    if (!rts_mpmc_handle_init(&mq, &thread->mqh)) {
        rts_ext_free(thread);
        return -ENOMEM;
    }

    rts_lruhash_init(thread->dict, dict_entry_equal,
        rts_handle_dict_hash_expiry, dict_buckets);
    rts_lruhash_init(thread->flow, rts_ftentry_equal,
        rts_handle_flow_hash_expiry, flow_buckets);

    /* Attempt to acquire a bundle if one is loaded */
    spinlock_lock(&mq.spinlock);
    thread->bundle = g_bundle;
    if (thread->bundle)
        rts_bundle_get(thread->bundle);
    spinlock_unlock(&mq.spinlock);

    /* just need an incrementing number */
    thread->pid = __sync_add_and_fetch(&pid, 1);

    rts_msg_dispatch(thread);
    *threadp = thread;
    return 0;
}

static int
rts_thread_destroy(struct rts_thread *thread)
{
    rts_assert(thread);

    rts_mpmc_handle_exit(&mq, &thread->mqh);

    if (thread->bundle)
        rts_bundle_put(thread->bundle);

    if (!rts_list_empty(&thread->dict->lru)) {
        struct rts_lruhash_item *ptr, *tmp;
        rts_list_for_each_entry_safe(ptr, tmp, &thread->dict->lru, lru) {
            struct dict_entry *e = rts_container_of(ptr, struct dict_entry, hash);
            rts_lruhash_remove(ptr);
            rts_buffer_exit(&e->key, &thread->mp);
            rts_buffer_exit(&e->val, &thread->mp);
            rts_pool_free(&thread->mp, e);
        }
    }

    rts_flow_flush(thread->flow, &thread->mp);

    rts_ext_free(thread);

    return 0;
}

EXPORT int
rts_handle_create(rts_handle_t *handle)
{
    int res;
    struct rts_thread *thread;

    if (!handle)
        return -EINVAL;

    if ((res = rts_thread_create(&thread, 0)) == 0)
        *handle = &thread->handle;
    return res;
}

EXPORT int
rts_handle_destroy(rts_handle_t handle)
{
    if (!handle)
        return -EINVAL;
    return rts_thread_destroy(rts_container_of(handle, struct rts_thread, handle));
}

EXPORT int
rts_handle_rusage(rts_handle_t handle, struct rts_rusage *rusage)
{
    if (!handle || !rusage)
        return -EINVAL;
    return rts_thread_rusage(rts_container_of(handle, struct rts_thread, handle), rusage);
}

EXPORT int
rts_stream_create(rts_stream_t *state, rts_handle_t handle, uint8_t domain, uint8_t proto,
    const void *saddr, uint16_t sport, const void *daddr, uint16_t dport, void *user)
{
    unsigned char *shm;
    struct rts_ipaddr addr[2];
    struct rts_ftentry *e;
    uint32_t addrlen;
    uint16_t port[2];
    size_t off;
    struct rts_thread *thread;
    struct rts_stream *stream;

    if (!state || !handle)
        return -EINVAL;

    thread = rts_container_of(handle, struct rts_thread, handle);
    addrlen = (domain == RTS_AF_INET ? 4 : (domain == RTS_AF_INET6 ? 16 : 0));

    port[0] = be16toh(sport);
    port[1] = be16toh(dport);

    if (addrlen == 4) {
        rts_ipaddr_copy_in4(&addr[0], saddr);
        rts_ipaddr_copy_in4(&addr[1], daddr);
    } else if (addrlen == 16) {
        rts_ipaddr_copy_in6(&addr[0], saddr);
        rts_ipaddr_copy_in6(&addr[1], daddr);
    } else {
        rts_ipaddr_init(&addr[0]);
        rts_ipaddr_init(&addr[1]);
    }

    rts_msg_dispatch(thread);

    if (!thread->bundle)
        return -EINVAL;

    if (proto >= thread->bundle->dfa.num_sr)
        return -EINVAL;

    stream = rts_pool_alloc(&thread->mp, sizeof(*stream));
    if (!stream)
        return -ENOMEM;

    *state = stream;

    stream->user = user;
    stream->vm.generation = thread->bundle->generation;

    thread->scan_started++;

    rts_data_init(&stream->data, proto);
    rts_vm_init(&stream->vm, thread);

    /* Check flow table expiry for this thread */
    rts_flow_check_expiry(thread);

    /* Check flow table for entry */
    e = rts_ft_find(thread->flow, proto, &addr[0], port[0], &addr[1], port[1],
            thread->timestamp);

    /* Shared Memory */
    shm = rts_vm_shm_get(&stream->vm, 0, (addrlen * 2) + 4);
    if (shm) {
        /* Direct memcpy into single shared buffer region 0 */
        off = 0;
        __builtin_memcpy(shm + off, saddr, addrlen); off += addrlen;
        __builtin_memcpy(shm + off, daddr, addrlen); off += addrlen;
        __builtin_memcpy(shm + off, &sport, sizeof(sport)); off += sizeof(sport);
        __builtin_memcpy(shm + off, &dport, sizeof(dport)); off += sizeof(dport);
        /* Allocate 4 new shared regions pointing into region 0 */
        off = 0;
        rts_vm_shm_zcopy(&stream->vm, shm, addrlen,       1, &off);
        rts_vm_shm_zcopy(&stream->vm, shm, addrlen,       2, &off);
        rts_vm_shm_zcopy(&stream->vm, shm, sizeof(sport), 3, &off);
        rts_vm_shm_zcopy(&stream->vm, shm, sizeof(dport), 4, &off);
    }

    /* Run main */
    rts_vm_exec(&stream->vm, 0, &stream->data, NULL);

    /* Flow entry indicates function */
    if (e && e->pc) {
        struct rts_value ttl;
        rts_vm_exec(&stream->vm, e->pc, &stream->data, NULL);
        /* This should succeed */
        if (!rts_vm_pop(&stream->vm, &ttl)) {
            /* Compiler bug */
            if (ttl.type != RTS_VALUE_TYPE_NUMBER) {
                ;
            /* Remove */
            } else if (ttl.number.data < 0) {
                rts_lruhash_remove(&e->hash);
                rts_pool_free(&thread->mp, e);
            /* Update */
            } else if (ttl.number.data > 0) {
                e->hash.ttl = (uint32_t)ttl.number.data * 1000;
                e->hash.touched = thread->timestamp;
            }
        }
    }

    return 0;
}

EXPORT int
rts_stream_matching(rts_stream_t stream)
{
    if (!stream)
        return -EINVAL;
    return !stream->data.state ? 0 : 1;
}

EXPORT int
rts_stream_destroy(rts_stream_t stream)
{
    struct rts_thread *thread;

    if (!stream)
        return -EINVAL;

    thread = stream->vm.thread;
    thread->scan_stopped++;

    rts_vm_exit(&stream->vm);
    stream->vm.thread = 0;
    stream->data.state = 0;
    rts_pool_free(&thread->mp, stream);
    return 0;
}

EXPORT int
rts_stream_scan(rts_stream_t stream, const void *ptr, uint16_t len, int dir, uint64_t timestamp)
{
    int res;
    struct rts_vm *vm;
    struct rts_buffer buffer;
    struct rts_buffer_data buffer_data = {
        .ref = 1,
        .data = (unsigned char *)ptr
    };

    /* Invalid input */
    if (!stream)
        return -EINVAL;

    vm = &stream->vm;

    /* Uptime timestamp & user data */
    vm->thread->timestamp = timestamp;

    /* Process queue */
    rts_msg_dispatch(stream->vm.thread);

    /* Bad luck! */
    if (!vm->thread->bundle)
        return -EINVAL;

    /* Exit early if not matching */
    if (!stream->data.state)
        return 0;

    /* Bundle was updated */
    if (vm->generation != vm->thread->bundle->generation) {
        rts_vm_exit(vm);
        stream->data.state = 0;
        return 0;
    }

    /* Setup direction */
    if (dir)
        stream->data.flags |= DATA_FLAG_EXT;
    else
        stream->data.flags &= ~DATA_FLAG_EXT;

    /* Setup data */
    rts_buffer_init_data(&buffer, &buffer_data, 0, len);

    /* Scan buffer */
    res = rts_vm_scan_buffer(vm, &stream->data, &buffer);

    /* Release */
    rts_buffer_exit(&buffer, NULL);

    /* Sync transient buffers */
    if (buffer_data.ref > 1 && !rts_vm_sync(vm)) {
        res = -1;
    } else {
        /* A dangling reference to the pkt data is a BUG */
        rts_assert(buffer_data.ref == 1);
    }

    /* Cleanup */
    if (!stream->data.state || res == -1) {
        rts_vm_exit(vm);
        stream->data.state = 0;

        if (res == -1) {
            res = -ENOMEM;
        }
    }

    /* Update bytes scanned */
    if (res > 0)
        vm->thread->scan_bytes += res;

    return res;
}

EXPORT int
rts_lookup(int service, const char **name, rts_stream_t stream)
{
    struct rts_bundle * bundle;
    int res;

    spinlock_lock(&mq.spinlock);
    if (!(bundle = g_bundle) ||
            (stream && stream->vm.generation != bundle->generation)) {
        spinlock_unlock(&mq.spinlock);
        return -EINVAL;
    }
    rts_bundle_get(bundle);
    spinlock_unlock(&mq.spinlock);

    if (service < 0) {
        res = bundle->stab->size;
        goto out;
    }

    if ((unsigned)service >= bundle->stab->size || !name) {
        res = -EINVAL;
        goto out;
    }

    *name = &bundle->stab->data[service * RTS_STAB_MAXLEN];
    res = 0;
out:
    rts_bundle_put(bundle);
    return res;
}

EXPORT int
rts_subscribe(const char *key,
    void (*callback)(rts_stream_t stream, void *user, const char *name, uint8_t type, uint16_t length, const void *value))
{
    int res;
    unsigned index;

    if (!key)
        return -EINVAL;

    spinlock_lock(&mq.spinlock);
    if (!g_bundle) {
        res = -EINVAL;
        goto out;
    }

    if (!(index = resolve_key_id(g_bundle, key))) {
        res = -EINVAL;
        goto out;
    }

    g_bundle->vars[index].func = callback;
    res = 0;
out:
    spinlock_unlock(&mq.spinlock);
    return res;
}

#ifndef KERNEL
EXPORT __attribute__((weak)) void *
rts_ext_alloc(size_t sz) {
    return MALLOC(sz);
}

EXPORT __attribute__((weak)) void
rts_ext_free(void *p) {
    FREE(p);
}
#endif
