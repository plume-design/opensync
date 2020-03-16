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

/* Familiarize yourself with RFC1035 if you want to know what all the
 * variable names mean.  This file hides most of the dirty work all of
 * this code depends on the buffer space a packet is in being 4096 and
 * zero'd before the packet is copied in also conveniently decodes srv
 * rr's, type 33, see RFC2782
 */
#ifndef MDNS_1035_H_
#define MDNS_1035_H_

#include <arpa/inet.h>

/* Should be reasonably large, for UDP */
#define MAX_PACKET_LEN 65535
#define MAX_NUM_LABELS 512

struct question {
    char *name;
    unsigned short int type, class;
};

#define QTYPE_A      1
#define QTYPE_NS     2
#define QTYPE_CNAME  5
#define QTYPE_PTR    12
#define QTYPE_TXT    16
#define QTYPE_SRV    33

struct resource {
    char *name;
    unsigned short int type, class;
    unsigned long int ttl;
    unsigned short int rdlength;
    unsigned char *rdata;
    union {
        struct {
            struct in_addr ip;
            char *name;
        } a;
        struct {
            char *name;
        } ns;
        struct {
            char *name;
        } cname;
        struct {
            char *name;
        } ptr;
        struct {
            unsigned short int priority, weight, port;
            char *name;
        } srv;
    } known;
};

struct message {
    /* External data */
    unsigned short int id;
    struct {
        unsigned short qr:1, opcode:4, aa:1, tc:1, rd:1, ra:1, z:3, rcode:4;
    } header;
    unsigned short int qdcount, ancount, nscount, arcount;
    struct question *qd;
    struct resource *an, *ns, *ar;

    /* Internal variables */
    unsigned char *_buf;
    char *_labels[MAX_NUM_LABELS];
    int _len, _label;

    /* Packet acts as padding, easier mem management */
    unsigned char _packet[MAX_PACKET_LEN];
};

/**
 * Returns the next short/long off the buffer (and advances it)
 */
unsigned short int net2short(unsigned char **buf);
unsigned long int  net2long (unsigned char **buf);

/**
 * copies the short/long into the buffer (and advances it)
 */
void short2net(unsigned short int i, unsigned char **buf);
void long2net (unsigned long int  l, unsigned char **buf);

/**
 * parse packet into message, packet must be at least MAX_PACKET_LEN and
 * message must be zero'd for safety
 */
void message_parse(struct message *m, unsigned char *packet);

/**
 * create a message for sending out on the wire
 */
struct message *message_wire(void);

/**
 * append a question to the wire message
 */
void message_qd(struct message *m, char *name, unsigned short int type, unsigned short int class);

/**
 * append a resource record to the message, all called in order!
 */
void message_an(struct message *m, char *name, unsigned short int type, unsigned short int class, unsigned long int ttl);
void message_ns(struct message *m, char *name, unsigned short int type, unsigned short int class, unsigned long int ttl);
void message_ar(struct message *m, char *name, unsigned short int type, unsigned short int class, unsigned long int ttl);

/**
 * Append various special types of resource data blocks
 */
void message_rdata_long (struct message *m, struct in_addr l);
void message_rdata_name (struct message *m, char *name);
void message_rdata_srv  (struct message *m, unsigned short int priority, unsigned short int weight,
             unsigned short int port, char *name);
void message_rdata_raw  (struct message *m, unsigned char *rdata, unsigned short int rdlength);

/**
 * Return the wire format (and length) of the message, just free message
 * when done
 */
unsigned char *message_packet     (struct message *m);
int            message_packet_len (struct message *m);

#endif  /* MDNS_1035_H_ */
