#ifndef STRUTILS_H_INCLUDED
#define STRUTILS_H_INCLUDED

#include <stdint.h>
/*
 * Encodes the data into plaintext (minus newlines and delimiters).  Escaped
 * characters are in the format \x33 (an ! in this case).  The escaped
 * characters are:
 * All characters < \x20
 *  Backslash (\x5c)
 *  All characters >= \x7f
 * Arguments (packet, start, end):
 *  packet - The uint8_t array of the whole packet.
 *  start - the position of the first character in the data.
 *  end - the position + 1 of the last character in the data.
 */
char *
escape_data(const uint8_t *, uint32_t, uint32_t);

/*
 * Read a reservation record style name, dealing with any compression.
 * A newly allocated string of the read name with length bytes
 * converted to periods is placed in the char * argument.
 * If there was an error reading the name, NULL is returned and the
 * position argument is left with it's passed value.
 * Args (packet, pos, id_pos, len, name)
 * packet - The uint8_t array of the whole packet.
 * pos - the start of the rr name.
 * id_pos - the start of the dns packet (id field)
 * len - the length of the whole packet
 * name - We will return read name via this pointer.
 */
char *
read_rr_name(const uint8_t *, uint32_t *, uint32_t, uint32_t);

char *
fail_name(const uint8_t *, uint32_t, uint32_t, const char *);

char *
b64encode(const uint8_t *, uint32_t, uint16_t);

#endif /* STRUTILS_H_INCLUDED */
