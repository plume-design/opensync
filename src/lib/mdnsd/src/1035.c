/* Standalone DNS parsing, RFC10350
 *
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

#include "1035.h"
#include <string.h>
#include <stdio.h>

unsigned short int net2short(unsigned char **bufp)
{
    short int i;

    i = **bufp;
    i <<= 8;
    i |= *(*bufp + 1);
    *bufp += 2;

    return i;
}

unsigned long int net2long(unsigned char **bufp)
{
    long int l;

    l = **bufp;
    l <<= 8;
    l |= *(*bufp + 1);
    l <<= 8;
    l |= *(*bufp + 2);
    l <<= 8;
    l |= *(*bufp + 3);
    *bufp += 4;

    return l;
}

void short2net(unsigned short int i, unsigned char **bufp)
{
    *(*bufp + 1) = (unsigned char)i;
    i >>= 8;
    **bufp = (unsigned char)i;
    *bufp += 2;
}

void long2net(unsigned long int l, unsigned char **bufp)
{
    *(*bufp + 3) = (unsigned char)l;
    l >>= 8;
    *(*bufp + 2) = (unsigned char)l;
    l >>= 8;
    *(*bufp + 1) = (unsigned char)l;
    l >>= 8;
    **bufp = (unsigned char)l;
    *bufp += 4;
}

static unsigned short int _ldecomp(char *ptr)
{
    unsigned short int i;

    i = 0xc0 ^ ptr[0];
    i <<= 8;
    i |= ptr[1];
    if (i >= 4096)
        i = 4095;

    return i;
}

static void _label(struct message *m, unsigned char **bufp, char **namep)
{
    int x;
    char *label, *name;

    /* Set namep to the end of the block */
    *namep = name = (char *)m->_packet + m->_len;

    /* Loop storing label in the block */
    for (label = (char *)*bufp; *label != 0; name += *label + 1, label += *label + 1) {
        /* Skip past any compression pointers, kick out if end encountered (bad data prolly) */
        while (*label & 0xc0) {
            unsigned short int offset = _ldecomp(label);
            if (offset > m->_len)
                return;
            if (*(label = (char *)m->_buf + offset) == 0)
                break;
        }

        /* Make sure we're not over the limits */
        if ((name + *label) - *namep > 255 || m->_len + ((name + *label) - *namep) > 4096)
            return;

        /* Copy chars for this label */
        memcpy(name, label + 1, (size_t)*label);
        name[(size_t)*label] = '.';
    }

    /* Advance buffer */
    for (label = (char *)*bufp; *label != 0 && !(*label & 0xc0 && label++); label += *label + 1)
        ;
    *bufp = (unsigned char *)(label + 1);

    /* Terminate name and check for cache or cache it */
    *name = '\0';
    for (x = 0; x < MAX_NUM_LABELS && m->_labels[x]; x++) {
        if (strcmp(*namep, m->_labels[x]))
            continue;

        *namep = m->_labels[x];
        return;
    }

    /* No cache, so cache it if room */
    if (x < MAX_NUM_LABELS && m->_labels[x] == 0)
        m->_labels[x] = *namep;
    m->_len += (name - *namep) + 1;
}

/* Internal label matching */
static int _lmatch(struct message *m, char *l1, char *l2)
{
    int len;

    /* Always ensure we get called w/o a pointer */
    if (*l1 & 0xc0)
        return _lmatch(m, (char *)m->_buf + _ldecomp(l1), l2);
    if (*l2 & 0xc0)
        return _lmatch(m, l1, (char *)m->_buf + _ldecomp(l2));

    /* Same already? */
    if (l1 == l2)
        return 1;

    /* Compare all label characters */
    if (*l1 != *l2)
        return 0;
    for (len = 1; len <= *l1; len++) {
        if (l1[len] != l2[len])
            return 0;
    }

    /* Get new labels */
    l1 += *l1 + 1;
    l2 += *l2 + 1;

    /* At the end, all matched */
    if (*l1 == 0 && *l2 == 0)
        return 1;

    /* Try next labels */
    return _lmatch(m, l1, l2);
}

/* Nasty, convert host into label using compression */
static int _host(struct message *m, unsigned char **bufp, char *name)
{
    char label[256], *l;
    int len = 0, x = 1, y = 0, last = 0;

    if (name == 0)
        return 0;

    /* Make our label */
    while (name[y]) {
        if (name[y] == '.') {
            if (!name[y + 1])
                break;
            label[last] = x - (last + 1);
            last = x;
        } else {
            label[x] = name[y];
        }

        if (x++ == 255)
            return 0;

        y++;
    }

    label[last] = x - (last + 1);
    if (x == 1)
        x--;        /* Special case, bad names, but handle correctly */
    len = x + 1;
    label[x] = 0;       /* Always terminate w/ a 0 */

    /* Double-loop checking each label against all m->_labels for match */
    for (x = 0; label[x]; x += label[x] + 1) {
        for (y = 0; y < MAX_NUM_LABELS && m->_labels[y]; y++) {
            if (_lmatch(m, label + x, m->_labels[y])) {
                /* Matching label, set up pointer */
                l = label + x;
                short2net((unsigned char *)m->_labels[y] - m->_packet, (unsigned char **)&l);
                label[x] |= 0xc0;
                len = x + 2;
                break;
            }
        }
    
        if (label[x] & 0xc0)
            break;
    }

    /* Copy into buffer, point there now */
    memcpy(*bufp, label, len);
    l = (char *)*bufp;
    *bufp += len;

    /* For each new label, store it's location for future compression */
    for (x = 0; l[x] && m->_label < MAX_NUM_LABELS; x += l[x] + 1) {
        if (l[x] & 0xc0)
            break;

        m->_labels[m->_label++] = l + x;
    }

    return len;
}

static int _rrparse(struct message *m, struct resource *rr, int count, unsigned char **bufp)
{
    int i;

    for (i = 0; i < count; i++) {
        _label(m, bufp, &(rr[i].name));
        rr[i].type     = net2short(bufp);
        rr[i].class    = net2short(bufp);
        rr[i].ttl      = net2long(bufp);
        rr[i].rdlength = net2short(bufp);
//      fprintf(stderr, "Record type %d class 0x%2x ttl %lu len %d\n", rr[i].type, rr[i].class, rr[i].ttl, rr[i].rdlength);

        /* If not going to overflow, make copy of source rdata */
        if (rr[i].rdlength + (*bufp - m->_buf) > MAX_PACKET_LEN || m->_len + rr[i].rdlength > MAX_PACKET_LEN)
            return 1;

        /* For the following records the rdata will be parsed later. So don't set it here:
         * NS, CNAME, PTR, DNAME, SOA, MX, AFSDB, RT, KX, RP, PX, SRV, NSEC
         * See 18.14 of https://tools.ietf.org/html/rfc6762#page-47 */
        if (rr[i].type == QTYPE_NS || rr[i].type == QTYPE_CNAME || rr[i].type == QTYPE_PTR || rr[i].type == QTYPE_SRV) {
            rr[i].rdlength = 0;
        } else {
            rr[i].rdata = m->_packet + m->_len;
            m->_len += rr[i].rdlength;
            memcpy(rr[i].rdata, *bufp, rr[i].rdlength);
        }


        /* Parse commonly known ones */
        switch (rr[i].type) {
        case QTYPE_A:
            if (m->_len + 16 > MAX_PACKET_LEN)
                return 1;
            rr[i].known.a.name = (char *)m->_packet + m->_len;
            m->_len += 16;
            sprintf(rr[i].known.a.name, "%d.%d.%d.%d", (*bufp)[0], (*bufp)[1], (*bufp)[2], (*bufp)[3]);
            rr[i].known.a.ip.s_addr = net2long(bufp);
            break;

        case QTYPE_NS:
            _label(m, bufp, &(rr[i].known.ns.name));
            break;

        case QTYPE_CNAME:
            _label(m, bufp, &(rr[i].known.cname.name));
            break;

        case QTYPE_PTR:
            _label(m, bufp, &(rr[i].known.ptr.name));
            break;

        case QTYPE_SRV:
            rr[i].known.srv.priority = net2short(bufp);
            rr[i].known.srv.weight = net2short(bufp);
            rr[i].known.srv.port = net2short(bufp);
            _label(m, bufp, &(rr[i].known.srv.name));
            break;

        case QTYPE_TXT:
        default:
            *bufp += rr[i].rdlength;
        }
    }

    return 0;
}

/* Keep all our mem in one (aligned) block for easy freeing */
#define my(x,y)                 \
    while (m->_len & 7)         \
        m->_len++;          \
    x = (void *)(m->_packet + m->_len); \
    m->_len += y;

void message_parse(struct message *m, unsigned char *packet)
{
    int i;
    unsigned char *buf;

    if (packet == 0 || m == 0)
        return;

    /* Header stuff bit crap */
    m->_buf = buf = packet;
    m->id = net2short(&buf);
    if (buf[0] & 0x80)
        m->header.qr = 1;
    m->header.opcode = (buf[0] & 0x78) >> 3;
    if (buf[0] & 0x04)
        m->header.aa = 1;
    if (buf[0] & 0x02)
        m->header.tc = 1;
    if (buf[0] & 0x01)
        m->header.rd = 1;
    if (buf[1] & 0x80)
        m->header.ra = 1;
    m->header.z = (buf[1] & 0x70) >> 4;
    m->header.rcode = buf[1] & 0x0F;
    buf += 2;

    m->qdcount = net2short(&buf);
    if (m->_len + (sizeof(struct question) * m->qdcount) > MAX_PACKET_LEN - 8) {
        m->qdcount = 0;
        return;
    }

    m->ancount = net2short(&buf);
    if (m->_len + (sizeof(struct resource) * m->ancount) > MAX_PACKET_LEN - 8) {
        m->ancount = 0;
        return;
    }

    m->nscount = net2short(&buf);
    if (m->_len + (sizeof(struct resource) * m->nscount) > MAX_PACKET_LEN - 8) {
        m->nscount = 0;
        return;
    }

    m->arcount = net2short(&buf);
    if (m->_len + (sizeof(struct resource) * m->arcount) > MAX_PACKET_LEN - 8) {
        m->arcount = 0;
        return;
    }

    /* Process questions */
    my(m->qd, sizeof(struct question) * m->qdcount);
    for (i = 0; i < m->qdcount; i++) {
        _label(m, &buf, &(m->qd[i].name));
        m->qd[i].type  = net2short(&buf);
        m->qd[i].class = net2short(&buf);
    }

    /* Process rrs */
    my(m->an, sizeof(struct resource) * m->ancount);
    my(m->ns, sizeof(struct resource) * m->nscount);
    my(m->ar, sizeof(struct resource) * m->arcount);
    if (_rrparse(m, m->an, m->ancount, &buf))
        return;
    if (_rrparse(m, m->ns, m->nscount, &buf))
        return;
    if (_rrparse(m, m->ar, m->arcount, &buf))
        return;
}

void message_qd(struct message *m, char *name, unsigned short int type, unsigned short int class)
{
    m->qdcount++;
    if (m->_buf == 0)
        m->_buf = m->_packet + 12;
    _host(m, &(m->_buf), name);
    short2net(type, &(m->_buf));
    short2net(class, &(m->_buf));
}

static void _rrappend(struct message *m, char *name, unsigned short int type, unsigned short int class, unsigned long int ttl)
{
    if (m->_buf == 0)
        m->_buf = m->_packet + 12;
    _host(m, &(m->_buf), name);
    short2net(type, &(m->_buf));
    short2net(class, &(m->_buf));
    long2net(ttl, &(m->_buf));
}

void message_an(struct message *m, char *name, unsigned short int type, unsigned short int class, unsigned long int ttl)
{
    m->ancount++;
    _rrappend(m, name, type, class, ttl);
}

void message_ns(struct message *m, char *name, unsigned short int type, unsigned short int class, unsigned long int ttl)
{
    m->nscount++;
    _rrappend(m, name, type, class, ttl);
}

void message_ar(struct message *m, char *name, unsigned short int type, unsigned short int class, unsigned long int ttl)
{
    m->arcount++;
    _rrappend(m, name, type, class, ttl);
}

void message_rdata_long(struct message *m, struct in_addr l)
{
    short2net(4, &(m->_buf));
    long2net(l.s_addr, &(m->_buf));
}

void message_rdata_name(struct message *m, char *name)
{
    unsigned char *mybuf = m->_buf;

    m->_buf += 2;
    short2net(_host(m, &(m->_buf), name), &mybuf);
}

void message_rdata_srv(struct message *m, unsigned short int priority, unsigned short int weight, unsigned short int port, char *name)
{
    unsigned char *mybuf = m->_buf;

    m->_buf += 2;
    short2net(priority, &(m->_buf));
    short2net(weight, &(m->_buf));
    short2net(port, &(m->_buf));
    short2net(_host(m, &(m->_buf), name) + 6, &mybuf);
}

void message_rdata_raw(struct message *m, unsigned char *rdata, unsigned short int rdlength)
{
    if (((unsigned char *)m->_buf - m->_packet) + rdlength > 4096)
        rdlength = 0;
    short2net(rdlength, &(m->_buf));
    memcpy(m->_buf, rdata, rdlength);
    m->_buf += rdlength;
}

unsigned char *message_packet(struct message *m)
{
    unsigned char c, *buf = m->_buf;

    m->_buf = m->_packet;
    short2net(m->id, &(m->_buf));

    if (m->header.qr)
        m->_buf[0] |= 0x80;
    if ((c = m->header.opcode))
        m->_buf[0] |= (c << 3);
    if (m->header.aa)
        m->_buf[0] |= 0x04;
    if (m->header.tc)
        m->_buf[0] |= 0x02;
    if (m->header.rd)
        m->_buf[0] |= 0x01;
    if (m->header.ra)
        m->_buf[1] |= 0x80;
    if ((c = m->header.z))
        m->_buf[1] |= (c << 4);
    if (m->header.rcode)
        m->_buf[1] |= m->header.rcode;

    m->_buf += 2;
    short2net(m->qdcount, &(m->_buf));
    short2net(m->ancount, &(m->_buf));
    short2net(m->nscount, &(m->_buf));
    short2net(m->arcount, &(m->_buf));
    m->_buf = buf;      /* Restore, so packet_len works */

    return m->_packet;
}

int message_packet_len(struct message *m)
{
    if (m->_buf == 0)
        return 12;

    return (unsigned char *)m->_buf - m->_packet;
}
