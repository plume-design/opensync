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

/* libc */
#include <unistd.h>

/* 3rd party */
#include <jansson.h>

/* opensync */
#include <ds_dlist.h>
#include <log.h>
#include <memutil.h>
#include <os.h>
#include <util.h>

#define OVSDB_STREAM_RECV_BUDGET 64
#define OVSDB_STREAM_CHUNK_SIZE  (16 * 1024)

struct ovsdb_stream_chunk
{
    struct ds_dlist_node node;
    void *buf;
    void *pos;
    size_t remaining;
};

struct ovsdb_stream
{
    struct ds_dlist chunks;
    struct ovsdb_stream_chunk *iter;
    size_t position;
};

static void ovsdb_stream_init(struct ovsdb_stream *st)
{
    ds_dlist_init(&st->chunks, struct ovsdb_stream_chunk, node);
    st->iter = NULL;
    st->position = 0;
}

static void ovsdb_stream_chunk_free(struct ovsdb_stream_chunk *c)
{
    FREE(c->buf);
    FREE(c);
}

static struct ovsdb_stream_chunk *ovsdb_stream_chunk_new(void *buf, size_t len)
{
    struct ovsdb_stream_chunk *c = CALLOC(1, sizeof(*c));
    c->buf = buf;
    c->pos = buf;
    c->remaining = len;
    return c;
}

static struct ovsdb_stream_chunk *ovsdb_stream_chunk_from_fd(int fd, size_t max_len)
{
    void *buf = MALLOC(max_len);
    const ssize_t rv = recv(fd, buf, max_len, 0);
    if (rv <= 0)
    {
        FREE(buf);
        return NULL;
    }
    return ovsdb_stream_chunk_new(buf, (size_t)rv);
}

int ovsdb_stream_recv(struct ovsdb_stream *st, int fd)
{
    int budget = OVSDB_STREAM_RECV_BUDGET;
    for (; budget > 0; budget--)
    {
        const size_t max_len = OVSDB_STREAM_CHUNK_SIZE;
        struct ovsdb_stream_chunk *c = ovsdb_stream_chunk_from_fd(fd, max_len);
        const bool eof = (c == NULL) || (c->remaining == 0);
        if (eof) return -1;
        ds_dlist_insert_tail(&st->chunks, c);
        if (c->remaining != max_len) break;
    }
    return 0;
}

static void ovsdb_stream_iter_init(struct ovsdb_stream *st)
{
    st->iter = ds_dlist_head(&st->chunks);
    st->position = 0;
}

static void ovsdb_stream_iter_next(struct ovsdb_stream *st, struct ovsdb_stream_chunk *c)
{
    if (st->position < c->remaining) return;
    st->iter = ds_dlist_next(&st->chunks, c);
    st->position = 0;
}

static size_t ovsdb_stream_next_json_cb(void *buf, size_t len, void *priv)
{
    struct ovsdb_stream *st = priv;
    struct ovsdb_stream_chunk *c = st->iter;
    if (c == NULL) return 0;

    const size_t remaining = c->remaining - st->position;
    len = MIN(len, remaining);
    memcpy(buf, c->pos + st->position, len);
    st->position += len;

    ovsdb_stream_iter_next(st, c);
    return len;
}

static void ovsdb_stream_chunk_gc(struct ovsdb_stream *st, struct ovsdb_stream_chunk *c)
{
    if (c->remaining > 0) return;
    ds_dlist_remove(&st->chunks, c);
    ovsdb_stream_chunk_free(c);
}

static void ovsdb_stream_log(const char *pos, size_t bytes)
{
    /* This size limit is conservative and intends to avoid
     * truncating lines in syslog.
     */
    const size_t max = 1024 - 100;
    char buf[max + 1];
    size_t from = 0;
    size_t to = 0;
    while (bytes > 0)
    {
        const size_t len = MIN(bytes, max);
        memcpy(buf, pos, len);
        bytes -= len;
        pos += len;
        to += len;
        buf[len] = 0;
        LOG(DEBUG, "JSON RECV[%zu..%zu]: %s\n", from, to, buf);
        from += len;
    }
}

static void ovsdb_stream_advance(struct ovsdb_stream *st, size_t bytes)
{
    while (bytes > 0)
    {
        struct ovsdb_stream_chunk *c = ds_dlist_head(&st->chunks);
        if (c == NULL) break;

        const size_t len = MIN(bytes, c->remaining);

        ovsdb_stream_log(c->pos, len);
        c->pos += len;
        c->remaining -= len;
        bytes -= len;

        ovsdb_stream_chunk_gc(st, c);
    }
}

json_t *ovsdb_stream_next_json(struct ovsdb_stream *st)
{
    const size_t flags = JSON_DISABLE_EOF_CHECK;
    json_error_t error;
    MEMZERO(error);

    ovsdb_stream_iter_init(st);
    json_t *json = json_load_callback(ovsdb_stream_next_json_cb, st, flags, &error);
    if (json != NULL)
    {
        ovsdb_stream_advance(st, error.position);
    }
    return json;
}

static void ovsdb_stream_consume(struct ovsdb_stream *st, bool (*fn)(json_t *))
{
    for (;;)
    {
        json_t *json = ovsdb_stream_next_json(st);
        if (json == NULL) break;
        const bool handled = fn(json);
        WARN_ON(handled == false);
        json_decref(json);
    }
}

int ovsdb_stream_run(struct ovsdb_stream *st, int fd, bool (*fn)(json_t *))
{
    const int err = ovsdb_stream_recv(st, fd);
    if (err) return err;
    ovsdb_stream_consume(st, fn);
    return 0;
}

struct ovsdb_stream *ovsdb_stream_alloc(void)
{
    struct ovsdb_stream *st = MALLOC(sizeof(*st));
    ovsdb_stream_init(st);
    return st;
}

void ovsdb_stream_free_chunks(struct ds_dlist *chunks)
{
    struct ovsdb_stream_chunk *c;
    while ((c = ds_dlist_remove_head(chunks)) != NULL)
    {
        ovsdb_stream_chunk_free(c);
    }
}

void ovsdb_stream_free(struct ovsdb_stream *st)
{
    if (st == NULL) return;
    ovsdb_stream_free_chunks(&st->chunks);
    FREE(st);
}
