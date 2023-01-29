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

#include <ev.h>
#include <osw_ut.h>

OSW_UT(osw_tlv_simple)
{
    struct osw_tlv t = {0};
    const uint32_t ival = 1337;
    const float fval = 0.5;
    const char *sval = "hello";

    /* out of order */
    osw_tlv_put_string(&t, 2, sval);
    osw_tlv_put_u32(&t, 0, ival);
    osw_tlv_put_float(&t, 1, fval);
    osw_tlv_put_buf(&t, 3, sval, 2); /* "he" */

    const struct osw_tlv_hdr *tb[4] = {0};
    const size_t n_tb = 4;
    assert(OSW_TLV_PARSE(&t, NULL, tb, n_tb) == 0);
    assert(tb[0] != NULL);
    assert(tb[1] != NULL);
    assert(tb[2] != NULL);
    assert(tb[3] != NULL);
    assert(tb[0]->type == OSW_TLV_U32);
    assert(tb[1]->type == OSW_TLV_FLOAT);
    assert(tb[2]->type == OSW_TLV_STRING);
    assert(tb[3]->type == OSW_TLV_UNSPEC);
    assert(osw_tlv_get_u32(tb[0]) == ival);
    assert(osw_tlv_get_float(tb[1]) == fval);
    assert(strcmp(osw_tlv_get_data(tb[2]), sval) == 0);
    assert(strncmp(osw_tlv_get_data(tb[3]), sval, 2) == 0);
    assert(tb[2]->len == (strlen(sval) + 1));
    assert(tb[3]->len == 2);
    assert(OSW_TLV_FIND(&t, 0) == tb[0]);
    assert(OSW_TLV_FIND(&t, 1) == tb[1]);
    assert(OSW_TLV_FIND(&t, 2) == tb[2]);
    assert(OSW_TLV_FIND(&t, 3) == tb[3]);
}

OSW_UT(osw_tlv_overlap)
{
    struct osw_tlv t = {0};
    const uint32_t i1 = 0xdead;
    const uint32_t i2 = 0xbeef;

    /* out of order */
    osw_tlv_put_u32(&t, 0, i1);
    osw_tlv_put_u32(&t, 0, i2);

    const struct osw_tlv_hdr *tb[2] = {0};
    const size_t n_tb = 2;
    assert(OSW_TLV_PARSE(&t, NULL, tb, n_tb) == 0);
    assert(tb[0] != NULL);
    assert(tb[1] == NULL);

    /* the value at tail should shadow previous ones */
    assert(osw_tlv_get_u32(tb[0]) != i1);
    assert(osw_tlv_get_u32(tb[0]) == i2);
}

OSW_UT(osw_tlv_out_of_bounds)
{
    struct osw_tlv t = {0};
    //const size_t hdr_len = sizeof(struct osw_tlv_hdr);
    const uint32_t i1 = 0xdead;
    const uint32_t i2 = 0xdead;
    struct {
        struct osw_tlv_hdr t1; uint32_t i1;
        struct osw_tlv_hdr t2; uint32_t i2;
    } __attribute__((packed)) buf = {
        .t1 = {
            .id = 0,
            .type = OSW_TLV_U32,
            .len = sizeof(i1),
        },
        .i1 = i1,
        .t2 = {
            .id = 1,
            .type = OSW_TLV_U32,
            .len = sizeof(i2),
        },
        .i2 = i2,
    };

    t.data = &buf;

    /* sanity check */
    t.used = sizeof(buf);
    {
        const struct osw_tlv_hdr *tb[2] = {0};
        const size_t n_tb = 2;
        assert(OSW_TLV_PARSE(&t, NULL, tb, n_tb) == 0);
        assert(tb[0] != NULL);
        assert(tb[1] != NULL);
        assert(osw_tlv_get_u32(tb[0]) == i1);
        assert(osw_tlv_get_u32(tb[1]) == i2);
    }

    /* intentional corruption, out of bounds on payload */
    t.used = sizeof(buf) - 1;

    {
        const struct osw_tlv_hdr *tb[2] = {0};
        const size_t n_tb = 2;
        //assert(OSW_TLV_PARSE(&t, NULL, 0, tb, n_tb) == (hdr_len + 3));
        assert(OSW_TLV_PARSE(&t, NULL, tb, n_tb) != 0);
        assert(tb[0] != NULL);
        assert(tb[1] == NULL);
        assert(osw_tlv_get_u32(tb[0]) == i1);
    }

    /* intentional corruption, out of bounds on header */
    t.used = sizeof(buf) - sizeof(i1) - 1;

    {
        const struct osw_tlv_hdr *tb[2] = {0};
        const size_t n_tb = 2;
        //assert(OSW_TLV_PARSE(&t, NULL, 0, tb, n_tb) == (hdr_len - 1));
        assert(OSW_TLV_PARSE(&t, NULL, tb, n_tb) != 0);
        assert(tb[0] != NULL);
        assert(tb[1] == NULL);
        assert(osw_tlv_get_u32(tb[0]) == i1);
    }
}

OSW_UT(osw_tlv_mem)
{
    struct osw_tlv t = {0};
    osw_tlv_put_u32(&t, 0, 1);
    osw_tlv_fini(&t);
    assert(t.data == NULL);
    assert(t.size == 0);
    assert(t.used == 0);
}

OSW_UT(osw_tlv_nested)
{
    struct osw_tlv t = {0};
    const uint32_t i1 = 1337;

    osw_tlv_put_u32(&t, 0, i1);
    {
        size_t start = osw_tlv_put_nested(&t, 1);
        osw_tlv_put_u32(&t, 0, 10);
        osw_tlv_put_u32(&t, 1, 20);
        osw_tlv_put_u32(&t, 2, 30);
        osw_tlv_end_nested(&t, start);
    }

    const struct osw_tlv_hdr *t0 = OSW_TLV_FIND(&t, 0);
    const struct osw_tlv_hdr *t1 = OSW_TLV_FIND(&t, 1);
    assert(t0 != NULL);
    assert(t1 != NULL);
    assert(osw_tlv_get_u32(t0) == i1);

    const void *nested = osw_tlv_get_data(t1);
    assert(nested != NULL);
    const size_t len = t1->len;
    const struct osw_tlv_hdr *a0 = osw_tlv_find(nested, len, 0);
    const struct osw_tlv_hdr *a1 = osw_tlv_find(nested, len, 1);
    const struct osw_tlv_hdr *a2 = osw_tlv_find(nested, len, 2);
    const struct osw_tlv_hdr *a3 = osw_tlv_find(nested, len, 3);
    assert(a0 != NULL);
    assert(a1 != NULL);
    assert(a2 != NULL);
    assert(a3 == NULL);
    assert(osw_tlv_get_u32(a0) == 10);
    assert(osw_tlv_get_u32(a1) == 20);
    assert(osw_tlv_get_u32(a2) == 30);
}

OSW_UT(osw_tlv_policy)
{
    struct osw_tlv t = {0};

    osw_tlv_put_u32(&t, 0, 0);
    osw_tlv_put_float(&t, 1, 0.0f);
    osw_tlv_put_string(&t, 2, "hello");
    osw_tlv_put_float(&t, 3, 0.0f);

    {
        const struct osw_tlv_policy p[] = {
            [0] = { .type = OSW_TLV_U32 },
            [1] = { .type = OSW_TLV_FLOAT },
            [2] = { .type = OSW_TLV_STRING },
            [3] = { .type = OSW_TLV_U32 },
        };
        const struct osw_tlv_hdr *tb[4] = {0};
        const size_t n_tb = 4;
        assert(OSW_TLV_PARSE(&t, p, tb, n_tb) == 0);
        assert(tb[0] != NULL);
        assert(tb[1] != NULL);
        assert(tb[2] != NULL);
        assert(tb[3] == NULL);
    }

    {
        const struct osw_tlv_policy p[4] = {
            [2] = { .type = OSW_TLV_STRING, .min_len = 10, .max_len = 20 },
        };
        const struct osw_tlv_hdr *tb[4] = {0};
        const size_t n_tb = 4;
        assert(OSW_TLV_PARSE(&t, p, tb, n_tb) == 0);
        assert(tb[2] == NULL);
    }

    {
        const struct osw_tlv_policy p[4] = {
            [2] = { .type = OSW_TLV_STRING, .min_len = 1, .max_len = 20 },
        };
        const struct osw_tlv_hdr *tb[4] = {0};
        const size_t n_tb = 4;
        assert(OSW_TLV_PARSE(&t, p, tb, n_tb) == 0);
        assert(tb[2] != NULL);
    }

    {
        const struct osw_tlv_policy p[4] = {
            [2] = { .type = OSW_TLV_STRING, .min_len = 1, .max_len = 2 },
        };
        const struct osw_tlv_hdr *tb[4] = {0};
        const size_t n_tb = 4;
        assert(OSW_TLV_PARSE(&t, p, tb, n_tb) == 0);
        assert(tb[2] == NULL);
    }
}
