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

#ifndef LIB_MDNSD_H_
#define LIB_MDNSD_H_

#include "1035.h"
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdio.h>
#include <syslog.h>

#define QCLASS_IN (1)
#define DISCO_NAME "_services._dns-sd._udp.local."

#define DBG(fmt, args...)  mdnsd_log(LOG_DEBUG, "%s(): " fmt, __func__, ##args)
#define INFO(fmt, args...) mdnsd_log(LOG_INFO, "%s(): " fmt, __func__, ##args)
#define NOTE(fmt, args...) mdnsd_log(LOG_NOTICE, fmt, ##args)
#define WARN(fmt, args...) mdnsd_log(LOG_WARNING, fmt, ##args)
#define ERR(fmt, args...)  mdnsd_log(LOG_ERR, fmt, ##args)

/* Main daemon data */
typedef struct mdns_daemon mdns_daemon_t;
/* Record entry */
typedef struct mdns_record mdns_record_t;

/* Callback for received record. Data is passed from the register call */
typedef void (*mdnsd_record_received_callback)(const struct resource* r, void* data);

/* Answer data */
typedef struct mdns_answer {
    char *name;
    unsigned short int type;
    unsigned long int ttl;
    unsigned short int rdlen;
    unsigned char *rdata;
    struct in_addr ip;  /* A, network byte order */
    char *rdname;       /* NS/CNAME/PTR/SRV */
    struct {
        unsigned short int priority, weight, port;
    } srv;          /* SRV */
} mdns_answer_t;

/**
 * Global functions
 */

/**
 * Enable logging to syslog, stdout/stderr is used by default
 */
void mdnsd_log_open(const char *ident);

/**
 * Adjust log level, by default LOG_NOTICE
 */
int mdnsd_log_level(char *level);

/**
 * Log current time to DBG() or buf
 */
void mdnsd_log_time(struct timeval *tv, char *buf, size_t len);

/**
 * HEX dump a buffer to log
 */
void mdnsd_log_hex(char *msg, unsigned char *buffer, ssize_t len);

/**
 * Log to syslog or stdio
 */
void mdnsd_log(int severity, const char *fmt, ...);

/**
 * Create a new mdns daemon for the given class of names (usually 1) and
 * maximum frame size
 */
mdns_daemon_t *mdnsd_new(int class, int frame);

/* Create a new record, or update an existing one */
mdns_record_t *mdnsd_set_record(mdns_daemon_t *d, int shared, char *host,
                                const char *name, unsigned short type,
                                unsigned long ttl,
                                void (*conflict)(char *host, int type, void *arg),
                                void *arg);
/**
 * Set mDNS daemon host IP address
 */
void mdnsd_set_address(mdns_daemon_t *d, struct in_addr addr);

/**
 * Get mDNS daemon host IP address from previous set
 */
struct in_addr mdnsd_get_address(mdns_daemon_t *d);

/**
 * Gracefully shutdown the daemon, use mdnsd_out() to get the last
 * packets
 */
void mdnsd_shutdown(mdns_daemon_t *d);

/**
 * Flush all cached records (network/interface changed)
 */
void mdnsd_flush(mdns_daemon_t *d);

/**
 * Free given mdns_daemon_t *(should have used mdnsd_shutdown() first!)
 */
void mdnsd_free(mdns_daemon_t *d);

/**
 * Register callback which is called when a record is received. The data parameter is passed to the callback.
 * Calling this multiple times overwrites the previous register.
 */
void mdnsd_register_receive_callback(mdns_daemon_t *d, mdnsd_record_received_callback cb, void *data);

/**
 * I/O functions
 */

/**
 * Oncoming message from host (to be cached/processed)
 */
int mdnsd_in(mdns_daemon_t *d, struct message *m, unsigned long int ip, unsigned short int port);

/**
 * Outgoing messge to be delivered to host, returns >0 if one was
 * returned and m/ip/port set
 */
int mdnsd_out(mdns_daemon_t *d, struct message *m, unsigned long int *ip, unsigned short int *port);

/**
 * returns the max wait-time until mdnsd_out() needs to be called again 
 */
struct timeval *mdnsd_sleep(mdns_daemon_t *d);

/**
 * Q/A functions
 */

/**
 * Register a new query
 *
 * The answer() callback is called whenever one is found/changes/expires
 * (immediate or anytime after, mdns_answer_t valid until ->ttl==0)
 * either answer returns -1, or another mdnsd_query() with a %NULL answer
 * will remove/unregister this query
 */
void mdnsd_query(mdns_daemon_t *d, const char *host, int type, int (*answer)(mdns_answer_t *a, void *arg), void *arg);

/**
 * Returns the first (if last == NULL) or next answer after last from
 * the cache mdns_answer_t only valid until an I/O function is called
 */
mdns_answer_t *mdnsd_list(mdns_daemon_t *d, const char *host, int type, mdns_answer_t *last);

/**
 * Returns the next record of the given record, i.e. the value of next field.
 * @param r the base record
 * @return r->next
 */
mdns_record_t *mdnsd_record_next(const mdns_record_t *r);

/**
 * Gets the record data
 */
const mdns_answer_t *mdnsd_record_data(const mdns_record_t *r);


/**
 * Publishing functions
 */

/**
 * Create a new unique record
 *
 * Call mdnsd_list() first to make sure the record is not used yet.
 *
 * The conflict() callback is called at any point when one is detected
 * and unable to recover after the first data is set_*(), any future
 * changes effectively expire the old one and attempt to create a new
 * unique record
 */
mdns_record_t *mdnsd_unique(mdns_daemon_t *d, const char *host, unsigned short type, unsigned long ttl, void (*conflict)(char *host, int type, void *arg), void *arg);


/** 
 * Create a new shared record
 */
mdns_record_t *mdnsd_shared(mdns_daemon_t *d, const char *host, unsigned short type, unsigned long ttl);

/**
 * Get a previously created record based on the host name. NULL if not found. Does not return records for other hosts.
 * If multiple records are found, use record->next to iterate over all the results.
 */
mdns_record_t *mdnsd_get_published(mdns_daemon_t *d, const char *host);

/**
 * Check if there is already a query for the given host
 */
int mdnsd_has_query(mdns_daemon_t *d, const char *host);

/**
 * Find previously based record based on name and type
 */
mdns_record_t *mdnsd_find(mdns_daemon_t *d, const char *name, unsigned short type);

/**
 * de-list the given record
 */
void mdnsd_done(mdns_daemon_t *d, mdns_record_t *r);

/**
 * These all set/update the data for the given record, nothing is
 * published until they are called
 */
void mdnsd_set_raw(mdns_daemon_t *d, mdns_record_t *r, const char *data, unsigned short len);
void mdnsd_set_host(mdns_daemon_t *d, mdns_record_t *r, const char *name);
void mdnsd_set_ip(mdns_daemon_t *d, mdns_record_t *r, struct in_addr ip);
void mdnsd_set_srv(mdns_daemon_t *d, mdns_record_t *r, unsigned short priority, unsigned short weight, unsigned short port, char *name);

/**
 * Process input queue and output queue. Should be called at least the time which is returned in nextSleep.
 * Returns 0 on success, 1 on read error, 2 on write error
 */
int mdnsd_step(mdns_daemon_t *d, int mdns_socket, bool processIn, bool processOut, struct timeval *tv);

#endif  /* LIB_MDNSD_H_ */
