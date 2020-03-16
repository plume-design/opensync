#ifndef RTYPES_H_INCLUDED
#define RTYPES_H_INCLUDED

#include <pcap.h>
#include <stdint.h>
#include <stdbool.h>

typedef char * rr_data_parser(const uint8_t*, uint32_t, uint32_t,
                              uint16_t, uint32_t);

typedef struct
{
    uint16_t cls;
    uint16_t rtype;
    rr_data_parser * parser;
    const char * name;
    const char * doc;
    unsigned long long count;
} rr_parser_container;

rr_parser_container *
find_parser(uint16_t, uint16_t);

char *
read_dns_name(uint8_t *, uint32_t, uint32_t);

rr_data_parser opts;
rr_data_parser escape;

extern rr_parser_container rr_parsers[];

void
print_parsers(void);

void
print_parser_usage(void);

bool
is_default_rr_parser(rr_parser_container *parser);

#endif /* RTYPES_H_INCLUDED */
