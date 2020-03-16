/*
 * Copyright (c) 2003  Jeremie Miller <jer@jabber.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "mdnsd.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

#define SPRIME 108      /* Size of query/publish hashes */
#define LPRIME 1009     /* Size of cache hash */

#define GC 86400                /* Brute force garbage cleanup
                 * frequency, rarely needed (daily
                 * default) */

/**
 * Messy, but it's the best/simplest balance I can find at the moment
 *
 * Some internal data types, and a few hashes: querys, answers, cached,
 * and records (published, unique and shared).  Each type has different
 * semantics for processing, both for timeouts, incoming, and outgoing
 * I/O.  They inter-relate too, like records affect the querys they are
 * relevant to.  Nice things about MDNS: we only publish once (and then
 * ask asked), and only query once, then just expire records we've got
 * cached
*/

struct query {
    char *name;
    int type;
    unsigned long int nexttry;
    int tries;
    int (*answer)(mdns_answer_t *, void *);
    void *arg;
    struct query *next, *list;
};

struct unicast {
    int id;
    unsigned long int to;
    unsigned short int port;
    mdns_record_t *r;
    struct unicast *next;
};

struct cached {
    struct mdns_answer rr;
    struct query *q;
    struct cached *next;
};

struct mdns_record {
    struct mdns_answer rr;
    char unique;        /* # of checks performed to ensure */
    int modified;       /* Ignore conflicts after update at runtime */
    int tries;
    void (*conflict)(char *, int, void *);
    void *arg;
    struct timeval last_sent;
    struct mdns_record *next, *list;
};

struct mdns_daemon {
    char shutdown, disco;
    unsigned long int expireall, checkqlist;
    struct timeval now, sleep, pause, probe, publish;
    int class, frame;
    struct cached *cache[LPRIME];
    struct mdns_record *published[SPRIME], *probing, *a_now, *a_pause, *a_publish;
    struct unicast *uanswers;
    struct query *queries[SPRIME], *qlist;

    struct in_addr addr;

    mdnsd_record_received_callback received_callback;
    void *received_callback_data;
};

static int _namehash(const char *s)
{
    const unsigned char *name = (const unsigned char *)s;
    unsigned long h = 0, g;

    while (*name) {     /* do some fancy bitwanking on the string */
        h = (h << 4) + (unsigned long)(*name++);
        if ((g = (h & 0xF0000000UL)) != 0)
            h ^= (g >> 24);
        h &= ~g;
    }

    return (int)h;
}

/* Basic linked list and hash primitives */
static struct query *_q_next(mdns_daemon_t *d, struct query *q, const char *host, int type)
{
    if (q == 0)
        q = d->queries[_namehash(host) % SPRIME];
    else
        q = q->next;

    for (; q != 0; q = q->next) {
        if (q->type == type && strcmp(q->name, host) == 0)
            return q;
    }

    return 0;
}

static struct cached *_c_next(mdns_daemon_t *d, struct cached *c,const char *host, int type)
{
    if (c == 0)
        c = d->cache[_namehash(host) % LPRIME];
    else
        c = c->next;

    for (; c != 0; c = c->next) {
        if ((type == c->rr.type || type == 255) && strcmp(c->rr.name, host) == 0)
            return c;
    }

    return 0;
}

static mdns_record_t *_r_next(mdns_daemon_t *d, mdns_record_t *r, const char *host, int type)
{
    if (r == NULL)
        r = d->published[_namehash(host) % SPRIME];
    else
        r = r->next;

    for (; r != NULL; r = r->next) {
        if (type == r->rr.type && strcmp(r->rr.name, host) == 0)
            return r;
    }

    return 0;
}

static int _rr_len(mdns_answer_t *rr)
{
    int len = 12;       /* name is always compressed (dup of earlier), plus normal stuff */

    if (rr->rdata)
        len += rr->rdlen;
    if (rr->rdname)
        len += strlen(rr->rdname); /* worst case */
    if (rr->ip.s_addr)
        len += 4;
    if (rr->type == QTYPE_PTR)
        len += 6;   /* srv record stuff */

    return len;
}

/* Compares new rdata with known a, painfully */
static int _a_match(struct resource *r, mdns_answer_t *a)
{
    if (!a->name)
        return 0;
    if (strcmp(r->name, a->name) || r->type != a->type)
        return 0;

    if (r->type == QTYPE_SRV && !strcmp(r->known.srv.name, a->rdname) && a->srv.port == r->known.srv.port &&
        a->srv.weight == r->known.srv.weight && a->srv.priority == r->known.srv.priority)
        return 1;

    if ((r->type == QTYPE_PTR || r->type == QTYPE_NS || r->type == QTYPE_CNAME) && !strcmp(a->rdname, r->known.ns.name))
        return 1;

    if (r->type == QTYPE_A || !memcmp(&r->known.a.ip, &a->ip, 4))
        return 1;

    if (r->rdlength == a->rdlen && !memcmp(r->rdata, a->rdata, r->rdlength))
        return 1;

    return 0;
}

/* Compare time values easily */
static int _tvdiff(struct timeval old, struct timeval new)
{
    int udiff = 0;

    if (old.tv_sec != new.tv_sec)
        udiff = (new.tv_sec - old.tv_sec) * 1000000;

    return (new.tv_usec - old.tv_usec) + udiff;
}

static void _r_remove_list(mdns_record_t **list, mdns_record_t *r)
{
    mdns_record_t *tmp;

    if (*list == r) {
        *list = r->list;
        r->list = NULL;
        return;
    }

    for (tmp = *list; tmp; tmp = tmp->list) {
        if (tmp->list == r) {
            tmp->list = r->list;
            r->list = NULL;
            break;
        }
        if (tmp == tmp->list)
            break;
    }
}

static void _r_remove_lists(mdns_daemon_t *d, mdns_record_t *r, mdns_record_t **skip)
{
    if (d->probing && &d->probing != skip)
        _r_remove_list(&d->probing, r);

    if (d->a_now && &d->a_now != skip)
        _r_remove_list(&d->a_now, r);

    if (d->a_pause && &d->a_pause != skip)
        _r_remove_list(&d->a_pause, r);

    if (d->a_publish && &d->a_publish != skip)
        _r_remove_list(&d->a_publish, r);
}

/* Make sure not already on the list, then insert */
static void _r_push(mdns_record_t **list, mdns_record_t *r)
{
    mdns_record_t *cur;

    for (cur = *list; cur != 0; cur = cur->list) {
        if (cur == r)
            return;
    }

    r->list = *list;
    *list = r;
}

/* Force any r out right away, if valid */
static void _r_publish(mdns_daemon_t *d, mdns_record_t *r)
{
    r->modified = 1;

    if (r->unique && r->unique < 5)
        return;     /* Probing already */

    r->tries = 0;
    d->publish.tv_sec = d->now.tv_sec;
    d->publish.tv_usec = d->now.tv_usec;

    /* check if r already in other lists. If yes, remove it from there */
    _r_remove_lists(d, r, &d->a_publish);
    _r_push(&d->a_publish, r);
}

/* send r out asap */
static void _r_send(mdns_daemon_t *d, mdns_record_t *r)
{
    /* Being published, make sure that happens soon */
    if (r->tries < 4) {
        d->publish.tv_sec = d->now.tv_sec;
        d->publish.tv_usec = d->now.tv_usec;
        return;
    }

    /* Known unique ones can be sent asap */
    if (r->unique) {
        /* check if r already in other lists. If yes, remove it from there */
        _r_remove_lists(d, r, &d->a_now);
        _r_push(&d->a_now, r);
        return;
    }

    /* Set d->pause.tv_usec to random 20-120 msec */
    d->pause.tv_sec = d->now.tv_sec;
    d->pause.tv_usec = d->now.tv_usec + (d->now.tv_usec % 100) + 20;

    /* check if r already in other lists. If yes, remove it from there */
    _r_remove_lists(d, r, &d->a_pause);
    _r_push(&d->a_pause, r);
}

/* Create generic unicast response struct */
static void _u_push(mdns_daemon_t *d, mdns_record_t *r, int id, unsigned long int to, unsigned short int port)
{
    struct unicast *u;

    u = calloc(1, sizeof(struct unicast));
    if (!u)
        return;

    u->r = r;
    u->id = id;
    u->to = to;
    u->port = port;
    u->next = d->uanswers;
    d->uanswers = u;
}

static void _q_reset(mdns_daemon_t *d, struct query *q)
{
    struct cached *cur = 0;

    q->nexttry = 0;
    q->tries = 0;

    while ((cur = _c_next(d, cur, q->name, q->type))) {
        if (q->nexttry == 0 || cur->rr.ttl - 7 < q->nexttry)
            q->nexttry = cur->rr.ttl - 7;
    }

    if (q->nexttry != 0 && q->nexttry < d->checkqlist)
        d->checkqlist = q->nexttry;
}

/* No more queries, update all its cached entries, remove from lists */
static void _q_done(mdns_daemon_t *d, struct query *q)
{
    struct cached *c = 0;
    struct query *cur;
    int i = _namehash(q->name) % LPRIME;

    while ((c = _c_next(d, c, q->name, q->type)))
        c->q = 0;

    if (d->qlist == q) {
        d->qlist = q->list;
    } else {
        for (cur = d->qlist; cur->list != q; cur = cur->list)
            ;
        cur->list = q->list;
    }

    if (d->queries[i] == q) {
        d->queries[i] = q->next;
    } else {
        for (cur = d->queries[i]; cur->next != q; cur = cur->next)
            ;
        cur->next = q->next;
    }

    free(q->name);
    free(q);
}

static void _free_cached(struct cached *c)
{
    if (!c)
        return;

    if (c->rr.name)
        free(c->rr.name);
    if (c->rr.rdata)
        free(c->rr.rdata);
    if (c->rr.rdname)
        free(c->rr.rdname);
    free(c);
}

static void _free_record(mdns_record_t *r)
{
    if (!r)
        return;

    if (r->rr.name)
        free(r->rr.name);
    if (r->rr.rdata)
        free(r->rr.rdata);
    if (r->rr.rdname)
        free(r->rr.rdname);
    free(r);
}

/* buh-bye, remove from hash and free */
static void _r_done(mdns_daemon_t *d, mdns_record_t *r)
{
    mdns_record_t *cur = 0;
    int i;

    if (!r || !r->rr.name)
        return;

    i = _namehash(r->rr.name) % SPRIME;
    if (d->published[i] == r) {
        d->published[i] = r->next;
    } else {
        for (cur = d->published[i]; cur && cur->next != r; cur = cur->next)
            ;
        if (cur)
            cur->next = r->next;
    }

    _free_record(r);
}

/* Call the answer function with this cached entry */
static void _q_answer(mdns_daemon_t *d, struct cached *c)
{
    if (c->rr.ttl <= (unsigned long)d->now.tv_sec)
        c->rr.ttl = 0;
    if (c->q->answer(&c->rr, c->q->arg) == -1)
        _q_done(d, c->q);
}

static void _conflict(mdns_daemon_t *d, mdns_record_t *r)
{
    r->conflict(r->rr.name, r->rr.type, r->arg);
    mdnsd_done(d, r);
}

/* Expire any old entries in this list */
static void _c_expire(mdns_daemon_t *d, struct cached **list)
{
    struct cached *cur  = *list;
    struct cached *last = NULL;
    struct cached *next;

    while (cur != NULL) {
        next = cur->next;

        if ((unsigned long)d->now.tv_sec >= cur->rr.ttl) {
            if (last)
                last->next = next;

            /* Update list pointer if the first one expired */
            if (*list == cur)
                *list = next;

            if (cur->q)
                _q_answer(d, cur);

            _free_cached(cur);
        } else {
            last = cur;
        }
        cur = next;
    }
}

/* Brute force expire any old cached records */
static void _gc(mdns_daemon_t *d)
{
    int i;

    for (i = 0; i < LPRIME; i++) {
        if (d->cache[i])
            _c_expire(d, &d->cache[i]);
    }

    d->expireall = (unsigned long)(d->now.tv_sec + GC);
}

static int _cache(mdns_daemon_t *d, struct resource *r)
{
    struct cached *c = 0;
    int i = _namehash(r->name) % LPRIME;

    /* Cache flush for unique entries */
    if (r->class == 32768 + d->class) {
        while ((c = _c_next(d, c, r->name, r->type)))
            c->rr.ttl = 0;
        _c_expire(d, &d->cache[i]);
    }

    /* Process deletes */
    if (r->ttl == 0) {
        while ((c = _c_next(d, c, r->name, r->type))) {
            if (_a_match(r, &c->rr)) {
                c->rr.ttl = 0;
                _c_expire(d, &d->cache[i]);
                c = NULL;
            }
        }

        return 0;
    }

    /*
     * XXX: The c->rr.ttl is a hack for now, BAD SPEC, start
     *      retrying just after half-waypoint, then expire
     */
    c = calloc(1, sizeof(struct cached));
    if (!c)
        return 1;

    c->rr.name = strdup(r->name);
    if (!c->rr.name) {
        free(c);
        return 1;
    }
    c->rr.type = r->type;
    c->rr.ttl = (unsigned long)d->now.tv_sec + (r->ttl / 2) + 8;
    c->rr.rdlen = r->rdlength;
    if (r->rdlength && !r->rdata) {
        //ERR("rdlength is %d but rdata is NULL for domain name %s, type: %d, ttl: %ld", r->rdlength, r->name, r->type, r->ttl);
        free(c->rr.name);
        free(c);
        return 1;
    }
    if (r->rdlength) {
        c->rr.rdata = malloc(r->rdlength);
        memcpy(c->rr.rdata, r->rdata, r->rdlength);
    } else {
        c->rr.rdata = NULL;
    }

    switch (r->type) {
    case QTYPE_A:
        c->rr.ip = r->known.a.ip;
        break;

    case QTYPE_NS:
    case QTYPE_CNAME:
    case QTYPE_PTR:
        c->rr.rdname = strdup(r->known.ns.name);
        break;

    case QTYPE_SRV:
        c->rr.rdname = strdup(r->known.srv.name);
        c->rr.srv.port = r->known.srv.port;
        c->rr.srv.weight = r->known.srv.weight;
        c->rr.srv.priority = r->known.srv.priority;
        break;
    }

    c->next = d->cache[i];
    d->cache[i] = c;

    if ((c->q = _q_next(d, 0, r->name, r->type)))
        _q_answer(d, c);

    return 0;
}

/* Copy the data bits only */
static void _a_copy(struct message *m, mdns_answer_t *a)
{
    if (a->rdata) {
        message_rdata_raw(m, a->rdata, a->rdlen);
        return;
    }

    if (a->ip.s_addr)
        message_rdata_raw(m, (unsigned char *)&a->ip, 4);
    if (a->type == QTYPE_SRV)
        message_rdata_srv(m, a->srv.priority, a->srv.weight, a->srv.port, a->rdname);
    else if (a->rdname)
        message_rdata_name(m, a->rdname);
}

/* Copy a published record into an outgoing message */
static int _r_out(mdns_daemon_t *d, struct message *m, mdns_record_t **list)
{
    mdns_record_t *r;
    int ret = 0;

    while ((r = *list) != NULL && message_packet_len(m) + _rr_len(&r->rr) < d->frame) {
        if (r != r->list)
            *list = r->list;
        else
            *list = NULL;

        /* Service enumeration/discovery, drop non-PTR replies */
        if (d->disco) {
            if (r->rr.type != QTYPE_PTR)
                continue;

            if (strcmp(r->rr.name, DISCO_NAME))
                continue;
        }

        INFO("Appending name: %s, type %d to outbound message ...", r->rr.name, r->rr.type);
        ret++;

        if (r->unique)
            message_an(m, r->rr.name, r->rr.type, d->class + 32768, r->rr.ttl);
        else
            message_an(m, r->rr.name, r->rr.type, d->class, r->rr.ttl);
        r->last_sent = d->now;

        _a_copy(m, &r->rr);

        r->modified = 0; /* If updated we've now sent the update. */
        if (r->rr.ttl == 0) {
            /*
             * also remove from other lists, because record
             * may be in multiple lists at the same time
             */
            _r_remove_lists(d, r, list);
            _r_done(d, r);
        }
    }

    return ret;
}


mdns_daemon_t *mdnsd_new(int class, int frame)
{
    mdns_daemon_t *d;

    d = calloc(1, sizeof(struct mdns_daemon));
    if (!d)
        return NULL;

    gettimeofday(&d->now, 0);
    d->expireall = (unsigned long)d->now.tv_sec + GC;
    d->class = class;
    d->frame = frame;
    d->received_callback = NULL;

    return d;
}

void mdnsd_set_address(mdns_daemon_t *d, struct in_addr addr)
{
    int i;

    if (!memcmp(&d->addr, &addr, sizeof(d->addr)))
        return;     /* No change */

    for (i = 0; i < SPRIME; i++) {
        mdns_record_t *r, *next;

        r = d->published[i];
        while (r != NULL) {
            next = r->next;

            if (r->rr.type == QTYPE_A)
                mdnsd_set_ip(d, r, addr);

            r = next;
        }
    }

    d->addr = addr;
}

struct in_addr mdnsd_get_address(mdns_daemon_t *d)
{
    return d->addr;
}

/* Shutting down, zero out ttl and push out all records */
void mdnsd_shutdown(mdns_daemon_t *d)
{
    int i;
    mdns_record_t *cur, *next;

    d->a_now = 0;
    for (i = 0; i < SPRIME; i++) {
        for (cur = d->published[i]; cur != 0;) {
            next = cur->next;
            cur->rr.ttl = 0;
            cur->list = d->a_now;
            d->a_now = cur;
            cur = next;
        }
    }

    d->shutdown = 1;
}

void mdnsd_flush(mdns_daemon_t *d)
{
    (void)d;
    /* - Set all querys to 0 tries
     * - Free whole cache
     * - Set all mdns_record_t *to probing
     * - Reset all answer lists
     */
}

void mdnsd_free(mdns_daemon_t *d)
{
    struct unicast *u;
    int i;

    for (i = 0; i< LPRIME; i++) {
        struct cached *cur = d->cache[i];

        while (cur) {
            struct cached *next = cur->next;

            _free_cached(cur);
            cur = next;
        }
    }

    for (i = 0; i< SPRIME; i++) {
        struct mdns_record *cur = d->published[i];
        struct query *curq;

        while (cur) {
            struct mdns_record *next = cur->next;

            _free_record(cur);
            cur = next;
        }

        curq = d->queries[i];
        while (curq) {
            struct query *next = curq->next;

            free(curq->name);
            free(curq);
            curq = next;
        }
    }

    u = d->uanswers;
    while (u) {
        struct unicast *next = u->next;

        free(u);
        u = next;
    }

    free(d);
}


void mdnsd_register_receive_callback(mdns_daemon_t *d, mdnsd_record_received_callback cb, void* data)
{
    d->received_callback = cb;
    d->received_callback_data = data;
}

int mdnsd_in(mdns_daemon_t *d, struct message *m, unsigned long int ip, unsigned short int port)
{
    int i, j;
    mdns_record_t *r = NULL;

    if (d->shutdown)
        return 1;

    gettimeofday(&d->now, 0);

    if (m->header.qr == 0) {
        /* Process each query */
        for (i = 0; i < m->qdcount; i++) {
            mdns_record_t *r_start, *r_next;
            bool has_conflict = false;

            if (m->qd[i].class != d->class)
                continue;

            INFO("Query for %s of type %d ...", m->qd[i].name, m->qd[i].type);
            if ((r = _r_next(d, NULL, m->qd[i].name, m->qd[i].type)) == NULL)
                continue;

            /* Service enumeratio/discovery prepeare to send all matching records */
            if (!strcmp(m->qd[i].name, DISCO_NAME)) {
                d->disco = 1;
                while (r != NULL) {
                    if (!strcmp(r->rr.name, DISCO_NAME))
                        _r_send(d, r);
                    r = _r_next(d, r, m->qd[i].name, m->qd[i].type);
                }

                continue;
            }

            /* Check all of our potential answers */
            for (r_start = r; r != NULL; r = r_next) {
                INFO("Local record: %s, type: %d, rdname: %s", r->rr.name, r->rr.type, r->rr.rdname);

                /* Fetch next here, because _conflict() might delete r, invalidating next */
                r_next = _r_next(d, r, m->qd[i].name, m->qd[i].type);

                /* probing state, check for conflicts */
                if (r->unique && r->unique < 5 && !r->modified) {
                    /* Check all to-be answers against our own */
                    for (j = 0; j < m->nscount; j++) {
                        if (m->qd[i].type != m->an[j].type || strcmp(m->qd[i].name, m->an[j].name))
                            continue;

                        /* This answer isn't ours, conflict! */
                        if (!_a_match(&m->an[j], &r->rr)) {
                            _conflict(d, r);
                            has_conflict = true;
                            break;
                        }
                    }
                    continue;
                }

                /* Check the known answers for this question */
                for (j = 0; j < m->ancount; j++) {
                    if (m->qd[i].type != m->an[j].type || strcmp(m->qd[i].name, m->an[j].name))
                        continue;

                    if (d->received_callback)
                        d->received_callback(&m->an[j], d->received_callback_data);

                    /* Do they already have this answer? */
                    if (_a_match(&m->an[j], &r->rr))
                        break;
                }

                INFO("Should we send answer? j: %d, m->ancount: %d", j, m->ancount);
                if (j == m->ancount) {
                    INFO("Yes we should, enquing %s for outbound", r->rr.name);
                    _r_send(d, r);
                }
            }

            /* Send the matching unicast reply */
            if (!has_conflict && port != 5353)
                _u_push(d, r_start, m->id, ip, port);
        }

        return 0;
    }

    /* Process each answer, check for a conflict, and cache */
    for (i = 0; i < m->ancount; i++) {
        if (m->an[i].name == NULL) {
            ERR("Got answer with NULL name at %p. Type: %d, TTL: %ld, skipping",
                (void*)&m->an[i], m->an[i].type, m->an[i].ttl);
            continue;
        }

        INFO("Got Answer: Name: %s, Type: %d", m->an[i].name, m->an[i].type);
        r = _r_next(d, 0, m->an[i].name, m->an[i].type);
        if (r != 0 && r->unique && !r->modified && _a_match(&m->an[i], &r->rr) == 0)
            _conflict(d, r);

        if (d->received_callback)
            d->received_callback(&m->an[i], d->received_callback_data);

        if (_cache(d, &m->an[i]) != 0) {
            ERR("Failed caching answer, possibly too long packet, skipping.");
            continue;
        }
    }

    return 0;
}

int mdnsd_out(mdns_daemon_t *d, struct message *m, unsigned long int *ip, unsigned short int *port)
{
    mdns_record_t *r;
    int ret = 0;

    gettimeofday(&d->now, 0);
    memset(m, 0, sizeof(struct message));

    /* Defaults, multicast */
    *port = htons(5353);
    *ip = inet_addr("224.0.0.251");
    m->header.qr = 1;
    m->header.aa = 1;

    /* Send out individual unicast answers */
    if (d->uanswers) {
        struct unicast *u = d->uanswers;

        INFO("Send Unicast Answer: Name: %s, Type: %d", u->r->rr.name, u->r->rr.type);

        d->uanswers = u->next;
        *port = u->port;
        *ip = u->to;
        m->id = u->id;
        message_qd(m, u->r->rr.name, u->r->rr.type, d->class);
        message_an(m, u->r->rr.name, u->r->rr.type, d->class, u->r->rr.ttl);
        u->r->last_sent = d->now;
        _a_copy(m, &u->r->rr);
        free(u);

        return 1;
    }

    /* Accumulate any immediate responses */
    if (d->a_now)
        ret += _r_out(d, m, &d->a_now);

    /* Check if it's time to send the publish retries (unlink if done) */
    if (d->a_publish && _tvdiff(d->now, d->publish) <= 0) {
        mdns_record_t *cur = d->a_publish;
        mdns_record_t *last = NULL;
        mdns_record_t *next;

        while (cur && message_packet_len(m) + _rr_len(&cur->rr) < d->frame) {
            if (cur->rr.type == QTYPE_PTR) {
                INFO("Send Publish PTR: Name: %s, rdlen: %d, rdata: %s, rdname: %s", cur->rr.name,cur->rr.rdlen, cur->rr.rdata, cur->rr.rdname);
            } else if (cur->rr.type == QTYPE_SRV) {
                INFO("Send Publish SRV: Name: %s, rdlen: %d, rdata: %s, rdname: %s, port: %d, prio: %d, weight: %d", cur->rr.name,cur->rr.rdlen, cur->rr.rdname, cur->rr.rdata, cur->rr.srv.port, cur->rr.srv.priority, cur->rr.srv.weight);
            } else {
                INFO("Send Publish: Name: %s, Type: %d, rdname: %s", cur->rr.name, cur->rr.type, cur->rr.rdname);
            }

            next = cur->list;
            ret++;
            cur->tries++;

            if (cur->unique)
                message_an(m, cur->rr.name, cur->rr.type, d->class + 32768, cur->rr.ttl);
            else
                message_an(m, cur->rr.name, cur->rr.type, d->class, cur->rr.ttl);
            _a_copy(m, &cur->rr);
            cur->last_sent = d->now;

            if (cur->rr.ttl != 0 && cur->tries < 4) {
                last = cur;
                cur = next;
                continue;
            }

            cur->list = NULL;
            if (d->a_publish == cur)
                d->a_publish = next;
            if (last)
                last->list = next;
            if (cur->rr.ttl == 0)
                _r_done(d, cur);
            cur = next;
        }

        if (d->a_publish) {
            d->publish.tv_sec = d->now.tv_sec + 2;
            d->publish.tv_usec = d->now.tv_usec;
        }
    }

    /* If we're in shutdown, we're done */
    if (d->shutdown)
        return ret;

    /* Check if a_pause is ready */
    if (d->a_pause && _tvdiff(d->now, d->pause) <= 0)
        ret += _r_out(d, m, &d->a_pause);

    /* Now process questions */
    if (ret)
        return ret;

    m->header.qr = 0;
    m->header.aa = 0;

    if (d->probing && _tvdiff(d->now, d->probe) <= 0) {
        mdns_record_t *last = 0;

        /* Scan probe list to ask questions and process published */
        for (r = d->probing; r != NULL;) {
            /* Done probing, publish */
            if (r->unique == 4) {
                mdns_record_t *next = r->list;

                if (d->probing == r)
                    d->probing = r->list;
                else
                    last->list = r->list;

                r->list = 0;
                r->unique = 5;
                _r_publish(d, r);
                r = next;
                continue;
            }

            INFO("Send Probing: Name: %s, Type: %d", r->rr.name, r->rr.type);

            message_qd(m, r->rr.name, r->rr.type, (unsigned short)d->class);
            r->last_sent = d->now;
            last = r;
            r = r->list;
        }

        /* Scan probe list again to append our to-be answers */
        for (r = d->probing; r != 0; last = r, r = r->list) {
            r->unique++;

            INFO("Send Answer in Probe: Name: %s, Type: %d", r->rr.name, r->rr.type);
            message_ns(m, r->rr.name, r->rr.type, (unsigned short)d->class, r->rr.ttl);
            _a_copy(m, &r->rr);
            r->last_sent = d->now;
            ret++;
        }

        /* Process probes again in the future */
        if (ret) {
            d->probe.tv_sec = d->now.tv_sec;
            d->probe.tv_usec = d->now.tv_usec + 250000;
            return ret;
        }
    }

    /* Process qlist for retries or expirations */
    if (d->checkqlist && (unsigned long)d->now.tv_sec >= d->checkqlist) {
        struct query *q;
        struct cached *c;
        unsigned long int nextbest = 0;

        /* Ask questions first, track nextbest time */
        for (q = d->qlist; q != 0; q = q->list) {
            if (q->nexttry > 0 && q->nexttry <= (unsigned long)d->now.tv_sec && q->tries < 3)
                message_qd(m, q->name, q->type, d->class);
            else if (q->nexttry > 0 && (nextbest == 0 || q->nexttry < nextbest))
                nextbest = q->nexttry;
        }

        /* Include known answers, update questions */
        for (q = d->qlist; q != 0; q = q->list) {
            if (q->nexttry == 0 || q->nexttry > (unsigned long)d->now.tv_sec)
                continue;

            /* Done retrying, expire and reset */
            if (q->tries == 3) {
                _c_expire(d, &d->cache[_namehash(q->name) % LPRIME]);
                _q_reset(d, q);
                continue;
            }

            ret++;
            q->nexttry = d->now.tv_sec + ++q->tries;
            if (nextbest == 0 || q->nexttry < nextbest)
                nextbest = q->nexttry;

            /* If room, add all known good entries */
            c = 0;
            while ((c = _c_next(d, c, q->name, q->type)) != 0 && c->rr.ttl > (unsigned long)d->now.tv_sec + 8 &&
                   message_packet_len(m) + _rr_len(&c->rr) < d->frame) {

                INFO("Add known answer: Name: %s, Type: %d", c->rr.name, c->rr.type);
                message_an(m, q->name, (unsigned short)q->type, (unsigned short)d->class, c->rr.ttl - (unsigned long)d->now.tv_sec);
                _a_copy(m, &c->rr);
            }
        }
        d->checkqlist = nextbest;
    }

    if ((unsigned long)d->now.tv_sec > d->expireall)
        _gc(d);

    return ret;
}


#define RET do {                \
    while (d->sleep.tv_usec > 1000000) {    \
        d->sleep.tv_sec++;      \
        d->sleep.tv_usec -= 1000000;    \
    }                   \
    return &d->sleep;           \
} while (0)

struct timeval *mdnsd_sleep(mdns_daemon_t *d)
{
    time_t expire;
    int sec, usec;
    int i;

    d->sleep.tv_sec = d->sleep.tv_usec = 0;

    /* First check for any immediate items to handle */
    if (d->uanswers || d->a_now)
        return &d->sleep;

    gettimeofday(&d->now, 0);

    /* Then check for paused answers or nearly expired records */
    if (d->a_pause) {
        if ((usec = _tvdiff(d->now, d->pause)) > 0)
            d->sleep.tv_usec = usec;
        RET;
    }

    /* Now check for probe retries */
    if (d->probing) {
        if ((usec = _tvdiff(d->now, d->probe)) > 0)
            d->sleep.tv_usec = usec;
        RET;
    }

    /* Now check for publish retries */
    if (d->a_publish) {
        if ((usec = _tvdiff(d->now, d->publish)) > 0)
            d->sleep.tv_usec = usec;
        RET;
    }

    /* Also check for queries with known answer expiration/retry */
    if (d->checkqlist) {
        if ((sec = d->checkqlist - d->now.tv_sec) > 0)
            d->sleep.tv_sec = sec;
        RET;
    }

    /* Resend published records before TTL expires */
    expire = (long)d->expireall - d->now.tv_sec;
    if (expire < 0)
        RET;

    for (i = 0; i < SPRIME; i++) {
        mdns_record_t *r;
        time_t next;

        r = d->published[i];
        if (!r)
            continue;

        /* Publish 2 seconds before expiration */
        next = r->last_sent.tv_sec + (long)r->rr.ttl - d->now.tv_sec;
        if (next <= 2) {
            INFO("Republish %s before TTL expires ...", r->rr.name);
            _r_push(&d->a_pause, r);
        }

        if (next < expire)
            expire = next;
    }

    d->sleep.tv_sec = expire > 2 ? expire - 2 : 0;
    d->pause.tv_sec = d->now.tv_sec + d->sleep.tv_sec;

    RET;
}

void mdnsd_query(mdns_daemon_t *d, const char *host, int type, int (*answer)(mdns_answer_t *a, void *arg), void *arg)
{
    struct query *q;
    struct cached *cur = 0;
    int i = _namehash(host) % SPRIME;

    if (!(q = _q_next(d, 0, host, type))) {
        if (!answer)
            return;

        q = calloc(1, sizeof(struct query));
        q->name = strdup(host);
        q->type = type;
        q->next = d->queries[i];
        q->list = d->qlist;
        d->qlist = d->queries[i] = q;

        /* Any cached entries should be associated */
        while ((cur = _c_next(d, cur, q->name, q->type)))
            cur->q = q;
        _q_reset(d, q);

        /* New question, immediately send out */
        q->nexttry = d->checkqlist = d->now.tv_sec;
    }

    /* No answer means we don't care anymore */
    if (!answer) {
        _q_done(d, q);
        return;
    }

    q->answer = answer;
    q->arg = arg;
}

mdns_answer_t *mdnsd_list(mdns_daemon_t *d,const char *host, int type, mdns_answer_t *last)
{
    return (mdns_answer_t *)_c_next(d, (struct cached *)last, host, type);
}

mdns_record_t *mdnsd_record_next(const mdns_record_t* r)
{
    return r ? r->next : NULL;
}

const mdns_answer_t *mdnsd_record_data(const mdns_record_t* r)
{
    return &r->rr;
}

mdns_record_t *mdnsd_shared(mdns_daemon_t *d, const char *host, unsigned short type, unsigned long ttl)
{
    int i = _namehash(host) % SPRIME;
    mdns_record_t *r;

    r = calloc(1, sizeof(struct mdns_record));
    r->rr.name = strdup(host);
    r->rr.type = type;
    r->rr.ttl = ttl;
    r->next = d->published[i];
    d->published[i] = r;

    return r;
}

mdns_record_t *mdnsd_unique(mdns_daemon_t *d, const char *host, unsigned short type, unsigned long ttl,
                void (*conflict)(char *host, int type, void *arg), void *arg)
{
    mdns_record_t *r;

    r = mdnsd_shared(d, host, type, ttl);
    r->conflict = conflict;
    r->arg = arg;
    r->unique = 1;

    /* check if r already in other lists. If yes, remove it from there */
    _r_remove_lists(d, r, &d->probing);
    _r_push(&d->probing, r);

    d->probe.tv_sec = d->now.tv_sec;
    d->probe.tv_usec = d->now.tv_usec;

    return r;
}

mdns_record_t *mdnsd_get_published(mdns_daemon_t *d, const char *host)
{
    return d->published[_namehash(host) % SPRIME];
}

int mdnsd_has_query(mdns_daemon_t *d, const char *host)
{
    return d->queries[_namehash(host) % SPRIME] != NULL;
}

mdns_record_t *mdnsd_find(mdns_daemon_t *d, const char *name, unsigned short type)
{
    mdns_record_t *r;

    r = mdnsd_get_published(d, name);
    while (r) {
        const mdns_answer_t *data;

        data = mdnsd_record_data(r);
        if (data && data->type == type)
            return r;

        r = mdnsd_record_next(r);
    }

    return NULL;
}

void mdnsd_done(mdns_daemon_t *d, mdns_record_t *r)
{
    mdns_record_t *cur;

    if (r->unique && r->unique < 5) {
        /* Probing yet, zap from that list first! */
        if (d->probing == r) {
            d->probing = r->list;
        } else {
            for (cur = d->probing; cur->list != r; cur = cur->list)
                ;
            cur->list = r->list;
        }

        _r_done(d, r);
        return;
    }

    r->rr.ttl = 0;
    _r_send(d, r);
}

void mdnsd_set_raw(mdns_daemon_t *d, mdns_record_t *r, const char *data, unsigned short len)
{
    if (r->rr.rdata)
        free(r->rr.rdata);

    r->rr.rdata = malloc(len);
    if (r->rr.rdata) {
        memcpy(r->rr.rdata, data, len);
        r->rr.rdlen = len;
    }
    _r_publish(d, r);
}

void mdnsd_set_host(mdns_daemon_t *d, mdns_record_t *r, const char *name)
{
    if (r->rr.rdname)
        free(r->rr.rdname);
    r->rr.rdname = strdup(name);
    _r_publish(d, r);
}

void mdnsd_set_ip(mdns_daemon_t *d, mdns_record_t *r, struct in_addr ip)
{
    r->rr.ip = ip;
    _r_publish(d, r);
}

void mdnsd_set_srv(mdns_daemon_t *d, mdns_record_t *r, unsigned short priority, unsigned short weight, unsigned short port, char *name)
{
    r->rr.srv.priority = priority;
    r->rr.srv.weight = weight;
    r->rr.srv.port = port;
    mdnsd_set_host(d, r, name);
}

static int process_in(mdns_daemon_t *d, int sd)
{
    struct sockaddr_in from;
    unsigned char buf[MAX_PACKET_LEN];
    socklen_t ssize = sizeof(struct sockaddr_in);
    ssize_t bsize;

    while ((bsize = recvfrom(sd, buf, MAX_PACKET_LEN, 0, (struct sockaddr *)&from, &ssize)) > 0) {
        struct message m;
        int rc;

        mdnsd_log_hex("Got Data:", buf, bsize);

        memset(&m, 0, sizeof(m));
        message_parse(&m, buf);
        rc = mdnsd_in(d, &m, (unsigned long int)from.sin_addr.s_addr, ntohs(from.sin_port));
        if (rc)
            return 1;
    }

    if (bsize < 0 && errno != EAGAIN)
        return 1;

    return 0;
}

static int process_out(mdns_daemon_t *d, int sd)
{
    unsigned short int port;
    struct sockaddr_in to;
    struct in_addr ip;
    struct message m;

    while (mdnsd_out(d, &m, (long unsigned int *)&ip, &port)) {
        unsigned char *buf;
        ssize_t len;

        memset(&to, 0, sizeof(to));
        to.sin_family = AF_INET;
        to.sin_port = port;
        to.sin_addr = ip;

        len = message_packet_len(&m);
        buf = message_packet(&m);
        mdnsd_log_hex("Send Data:", buf, len);

        if (sendto(sd, buf, len, 0, (struct sockaddr *)&to,
               sizeof(struct sockaddr_in)) != len)
            return 2;
    }

    return 0;
}

int mdnsd_step(mdns_daemon_t *d, int sd, bool in, bool out, struct timeval *tv)
{
    int rc = 0;

    if (in)
        rc = process_in(d, sd);
    if (!rc && out)
        rc = process_out(d, sd);

    if (!rc && tv) {
        struct timeval *delay;

        delay = mdnsd_sleep(d);
        memcpy(tv, delay, sizeof(*tv));
    }

    /* Service Enumeration/Discovery completed */
    if (d->disco)
        d->disco = 0;

    return rc;
}

/* Create a new record, or update an existing one */
mdns_record_t *mdnsd_set_record(mdns_daemon_t *d, int shared, char *host,
                                const char *name, unsigned short type,
                                unsigned long ttl,
                                void (*conflict)(char *host, int type, void *arg),
                                void *arg)
{
    mdns_record_t *r;

    r = mdnsd_find(d, name, type);
    while (r != NULL) {
        const mdns_answer_t *a;

        if (!host)
            break;

        a = mdnsd_record_data(r);
        if (!a) {
            r = mdnsd_record_next(r);
            continue;
        }

        if (a->rdname && strcmp(a->rdname, host)) {
            r = mdnsd_record_next(r);
            continue;
        }

        return r;
    }

    if (!r) {
        if (shared)
            r = mdnsd_shared(d, name, type, ttl);
        else
            r = mdnsd_unique(d, name, type, ttl, conflict, arg);

        if (host)
            mdnsd_set_host(d, r, host);
    }

    return r;
}

