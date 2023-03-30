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


#ifndef container_of
#define container_of(ptr, type, member) \
    (type *)((char *)ptr - __builtin_offsetof(type, member))
#endif

struct memstats {
    struct {
        size_t alloc;
        size_t realloc;
        size_t free;
        size_t total;
    } bin[2];
};

static struct memstats stats;

struct memhdr {
    size_t type;
    size_t size;
    unsigned char data[0];
};

__attribute__((destructor)) void
print_stats()
{
    printf("\n");
    printf("=== THREAD ============================================\n");
    printf("   allocs: %-16zu  total: %-16zu\n", stats.bin[0].alloc, stats.bin[0].total);
    printf(" reallocs: %-16zu  frees: %-16zu\n", stats.bin[0].realloc, stats.bin[0].free);
    printf("=======================================================\n");
    printf("\n");
    printf("=== SHARED ============================================\n");
    printf("   allocs: %-16zu  total: %-16zu\n", stats.bin[1].alloc, stats.bin[1].total);
    printf(" reallocs: %-16zu  frees: %-16zu\n", stats.bin[1].realloc, stats.bin[1].free);
    printf("=======================================================\n");
}

void *
rts_ext_alloc(void *p, size_t size, int type)
{
    struct memhdr *h_curr, *h_prev;

    stats.bin[type].alloc++;
    stats.bin[type].total += size;

    h_curr = (struct memhdr *)malloc(sizeof(*h_curr) + size);
    h_curr->type = type;
    h_curr->size = size;

    if (p) {
        h_prev = container_of(p, struct memhdr, data);
        stats.bin[type].realloc++;
        stats.bin[type].total -= h_prev->size;
        __builtin_memcpy(h_curr->data, h_prev->data, h_prev->size);
        rts_ext_free(h_prev->data);
    }

    return h_curr->data;
}

void
rts_ext_free(void *p)
{
    struct memhdr *h_curr = container_of(p, struct memhdr, data);
    stats.bin[h_curr->type].free++;
    free(h_curr);
}

uint64_t
rts_ext_time(void)
{
    return time(NULL);
}
