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

#include <osw_ut.h>

OSW_UT(osw_tlv_merge_delta)
{
    enum {
        STATI,
        STATF,
        STATS,
        STATN,
        STATMAX,
    };
    enum {
        NESTI,
        NESTF,
        NESTMAX,
    };
    const struct osw_tlv_policy pn[NESTMAX] = {
        [NESTI] = { .type = OSW_TLV_U32 },
        [NESTF] = { .type = OSW_TLV_FLOAT },
    };
    const struct osw_tlv_policy p[STATMAX] = {
        [STATI] = { .type = OSW_TLV_U32 },
        [STATF] = { .type = OSW_TLV_FLOAT },
        [STATS] = { .type = OSW_TLV_STRING },
        [STATN] = { .type = OSW_TLV_NESTED, .nested = pn, .tb_size = NESTMAX },
    };
    const struct osw_tlv_merge_policy p2n[NESTMAX] = {
        [NESTI] = { .type = OSW_TLV_OP_ACCUMULATE },
        [NESTF] = { .type = OSW_TLV_OP_ACCUMULATE },
    };
    const struct osw_tlv_merge_policy p2[STATMAX] = {
        [STATI] = { .type = OSW_TLV_OP_ACCUMULATE },
        [STATF] = { .type = OSW_TLV_OP_ACCUMULATE },
        [STATS] = { .type = OSW_TLV_OP_OVERWRITE },
        [STATN] = { .type = OSW_TLV_OP_MERGE, .nested = p2n, .tb_size = NESTMAX },
    };

    struct osw_tlv dest = {0};
    struct osw_tlv prev = {0};
    struct osw_tlv src = {0};

    osw_tlv_put_u32_delta(&src, STATI, 1);
    osw_tlv_merge(&dest, &prev, src.data, src.used, false, p, p2, STATMAX);
    osw_tlv_fini(&src);
    assert(dest.used > 0);

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATI] != NULL);
        assert(tb[STATF] == NULL);
        assert(osw_tlv_get_u32(tb[STATI]) == 1);
    }

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(prev.data, prev.used, p, tb, STATMAX);
        assert(tb[STATI] == NULL);
    }

    osw_tlv_put_u32_delta(&src, STATI, 2);
    /* diff_on_first for delta should be no-op */
    osw_tlv_merge(&dest, &prev, src.data, src.used, true, p, p2, STATMAX);
    osw_tlv_fini(&src);

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATI] != NULL);
        assert(osw_tlv_get_u32(tb[STATI]) == 3);
    }

    {
        size_t start = osw_tlv_put_nested(&src, STATN);
        osw_tlv_put_float_delta(&src, NESTF, 0.5);
        osw_tlv_end_nested(&src, start);
        osw_tlv_merge(&dest, &prev, src.data, src.used, false, p, p2, STATMAX);
        osw_tlv_fini(&src);
    }

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATI] != NULL);
        assert(tb[STATN] != NULL);
        const struct osw_tlv_hdr *n = tb[STATN];

        {
            const struct osw_tlv_hdr *tb[STATMAX] = {0};
            osw_tlv_parse(osw_tlv_get_data(n), n->len, pn, tb, STATMAX);
            assert(tb[NESTI] == NULL);
            assert(tb[NESTF] != NULL);
            assert(osw_tlv_get_float(tb[NESTF]) == 0.5);
        }
    }

    {
        size_t start = osw_tlv_put_nested(&src, STATN);
        osw_tlv_put_u32_delta(&src, NESTI, 10);
        osw_tlv_end_nested(&src, start);
        osw_tlv_merge(&dest, &prev, src.data, src.used, false, p, p2, STATMAX);
        osw_tlv_fini(&src);
    }

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATI] != NULL);
        assert(tb[STATN] != NULL);
        const struct osw_tlv_hdr *n = tb[STATN];

        {
            const struct osw_tlv_hdr *tb[STATMAX] = {0};
            osw_tlv_parse(osw_tlv_get_data(n), n->len, pn, tb, STATMAX);
            assert(tb[NESTI] != NULL);
            assert(tb[NESTF] != NULL);
            assert(osw_tlv_get_u32(tb[NESTI]) == 10);
            assert(osw_tlv_get_float(tb[NESTF]) == 0.5);
        }
    }

    {
        osw_tlv_put_string(&src, STATS, "hello");
        osw_tlv_merge(&dest, &prev, src.data, src.used, false, p, p2, STATMAX);
        osw_tlv_fini(&src);
    }

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATI] != NULL);
        assert(tb[STATN] != NULL);
        assert(tb[STATS] != NULL);
        assert(strcmp(osw_tlv_get_string(tb[STATS]), "hello") == 0);
    }

    assert(prev.used == 0);
}

OSW_UT(osw_tlv_merge_absolute)
{
    enum {
        STATI,
        STATF,
        STATS,
        STATN,
        STATMAX,
    };
    enum {
        NESTI,
        NESTF,
        NESTMAX,
    };
    const struct osw_tlv_policy pn[NESTMAX] = {
        [NESTI] = { .type = OSW_TLV_U32 },
        [NESTF] = { .type = OSW_TLV_FLOAT },
    };
    const struct osw_tlv_policy p[STATMAX] = {
        [STATI] = { .type = OSW_TLV_U32 },
        [STATF] = { .type = OSW_TLV_FLOAT },
        [STATS] = { .type = OSW_TLV_STRING },
        [STATN] = { .type = OSW_TLV_NESTED, .nested = pn, .tb_size = NESTMAX },
    };
    const struct osw_tlv_merge_policy p2n[NESTMAX] = {
        [NESTI] = { .type = OSW_TLV_OP_ACCUMULATE },
        [NESTF] = { .type = OSW_TLV_OP_ACCUMULATE },
    };
    const struct osw_tlv_merge_policy p2[STATMAX] = {
        [STATI] = { .type = OSW_TLV_OP_ACCUMULATE },
        [STATF] = { .type = OSW_TLV_OP_ACCUMULATE },
        [STATS] = { .type = OSW_TLV_OP_OVERWRITE },
        [STATN] = { .type = OSW_TLV_OP_MERGE, .nested = p2n, .tb_size = NESTMAX },
    };


    struct osw_tlv dest = {0};
    struct osw_tlv prev = {0};
    struct osw_tlv src = {0};

    osw_tlv_put_u32(&src, STATI, 1);
    osw_tlv_merge(&dest, &prev, src.data, src.used, false, p, p2, STATMAX);
    osw_tlv_fini(&src);
    assert(dest.used == 0);
    assert(prev.used > 0);

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATI] == NULL);
        assert(tb[STATF] == NULL);
    }

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(prev.data, prev.used, p, tb, STATMAX);
        assert(tb[STATI] != NULL);
        assert(tb[STATF] == NULL);
        assert(osw_tlv_get_u32(tb[STATI]) == 1);
    }

    osw_tlv_put_u32(&src, STATI, 3);
    osw_tlv_merge(&dest, &prev, src.data, src.used, false, p, p2, STATMAX);
    osw_tlv_fini(&src);
    assert(dest.used > 0);
    assert(prev.used > 0);

    /* this should increase delta to "2" (3-1) */

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATI] != NULL);
        assert(osw_tlv_get_u32(tb[STATI]) == 2);
    }

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(prev.data, prev.used, p, tb, STATMAX);
        assert(tb[STATI] != NULL);
        assert(osw_tlv_get_u32(tb[STATI]) == 3);
    }

    osw_tlv_put_string(&src, STATS, "hello");
    osw_tlv_merge(&dest, &prev, src.data, src.used, false, p, p2, STATMAX);
    osw_tlv_fini(&src);

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATI] != NULL);
        assert(tb[STATS] != NULL);
        assert(strcmp(osw_tlv_get_string(tb[STATS]), "hello") == 0);
    }

    {
        size_t start = osw_tlv_put_nested(&src, STATN);
        osw_tlv_put_u32(&src, NESTI, 10);
        osw_tlv_end_nested(&src, start);
        osw_tlv_merge(&dest, &prev, src.data, src.used, false, p, p2, STATMAX);
        osw_tlv_fini(&src);
    }

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(prev.data, prev.used, p, tb, STATMAX);
        assert(tb[STATI] != NULL);
        assert(tb[STATN] != NULL);
        const struct osw_tlv_hdr *n = tb[STATN];

        {
            const struct osw_tlv_hdr *tb[STATMAX] = {0};
            osw_tlv_parse(osw_tlv_get_data(n), n->len, pn, tb, STATMAX);
            assert(tb[NESTI] != NULL);
            assert(osw_tlv_get_u32(tb[NESTI]) == 10);
        }
    }

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATI] != NULL);
        assert(tb[STATN] == NULL);
    }

    {
        size_t start = osw_tlv_put_nested(&src, STATN);
        osw_tlv_put_u32(&src, NESTI, 13);
        osw_tlv_end_nested(&src, start);
        osw_tlv_merge(&dest, &prev, src.data, src.used, false, p, p2, STATMAX);
        osw_tlv_fini(&src);
    }

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(prev.data, prev.used, p, tb, STATMAX);
        assert(tb[STATI] != NULL);
        assert(tb[STATN] != NULL);
        const struct osw_tlv_hdr *n = tb[STATN];

        {
            const struct osw_tlv_hdr *tb[STATMAX] = {0};
            osw_tlv_parse(osw_tlv_get_data(n), n->len, pn, tb, STATMAX);
            assert(tb[NESTI] != NULL);
            assert(osw_tlv_get_u32(tb[NESTI]) == 13);
        }
    }

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATI] != NULL);
        assert(tb[STATN] != NULL);
        const struct osw_tlv_hdr *n = tb[STATN];

        {
            const struct osw_tlv_hdr *tb[STATMAX] = {0};
            osw_tlv_parse(osw_tlv_get_data(n), n->len, pn, tb, STATMAX);
            assert(tb[NESTI] != NULL);
            assert(osw_tlv_get_u32(tb[NESTI]) == 3);
        }
    }
}

OSW_UT(osw_tlv_merge_absolute_first)
{
    enum {
        STATI,
        STATMAX,
    };
    const struct osw_tlv_policy p[STATMAX] = {
        [STATI] = { .type = OSW_TLV_U32 },
    };
    const struct osw_tlv_merge_policy p2[STATMAX] = {
        [STATI] = { .type = OSW_TLV_OP_ACCUMULATE },
    };

    struct osw_tlv dest = {0};
    struct osw_tlv prev = {0};
    struct osw_tlv src = {0};

    osw_tlv_put_u32(&src, STATI, 10);
    osw_tlv_merge(&dest, &prev, src.data, src.used, true, p, p2, STATMAX);
    osw_tlv_fini(&src);
    assert(dest.used > 0);
    assert(prev.used > 0);

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATI] != NULL);
        assert(osw_tlv_get_u32(tb[STATI]) == 10);
    }
}

OSW_UT(osw_tlv_merge_absolute_first_2samples)
{
    enum {
        STATI,
        STATMAX,
    };
    const struct osw_tlv_policy p[STATMAX] = {
        [STATI] = { .type = OSW_TLV_U32 },
    };
    const struct osw_tlv_merge_policy p2[STATMAX] = {
        [STATI] = { .type = OSW_TLV_OP_ACCUMULATE, .first = OSW_TLV_TWO_SAMPLES_MINIMUM },
    };

    struct osw_tlv dest = {0};
    struct osw_tlv prev = {0};
    struct osw_tlv src = {0};

    osw_tlv_put_u32(&src, STATI, 10);
    /* `true` here is overridden with merge policy */
    osw_tlv_merge(&dest, &prev, src.data, src.used, true, p, p2, STATMAX);
    osw_tlv_fini(&src);
    assert(dest.used == 0);
    assert(prev.used > 0);

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATI] == NULL);
    }

    osw_tlv_put_u32(&src, STATI, 12);
    osw_tlv_merge(&dest, &prev, src.data, src.used, true, p, p2, STATMAX);
    osw_tlv_fini(&src);
    assert(dest.used > 0);
    assert(prev.used > 0);
    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATI] != NULL);
        assert(osw_tlv_get_u32(tb[STATI]) == 2);
    }
}

OSW_UT(osw_tlv_merge_absolute_nested_2samples)
{
    enum {
        STATN,
        STATMAX,
    };
    enum {
        NESTI,
        NESTMAX,
    };
    const struct osw_tlv_policy pn[NESTMAX] = {
        [NESTI] = { .type = OSW_TLV_U32 },
    };
    const struct osw_tlv_policy p[STATMAX] = {
        [STATN] = { .type = OSW_TLV_NESTED,
                    .nested = pn,
                    .tb_size = NESTMAX },
    };
    const struct osw_tlv_merge_policy p2n[NESTMAX] = {
        [NESTI] = { .type = OSW_TLV_OP_ACCUMULATE },
    };
    const struct osw_tlv_merge_policy p2[STATMAX] = {
        [STATN] = { .type = OSW_TLV_OP_MERGE,
                    .nested = p2n,
                    .tb_size = NESTMAX,
                    .first = OSW_TLV_TWO_SAMPLES_MINIMUM },
    };

    struct osw_tlv dest = {0};
    struct osw_tlv prev = {0};
    struct osw_tlv src = {0};

    {
        size_t start = osw_tlv_put_nested(&src, STATN);
        osw_tlv_put_u32(&src, NESTI, 10);
        osw_tlv_end_nested(&src, start);
    }
    /* `true` here is overridden with merge policy */
    osw_tlv_merge(&dest, &prev, src.data, src.used, true, p, p2, STATMAX);
    osw_tlv_fini(&src);
    assert(dest.used == 0);
    assert(prev.used > 0);

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATN] == NULL);
    }

    {
        size_t start = osw_tlv_put_nested(&src, STATN);
        osw_tlv_put_u32(&src, NESTI, 12);
        osw_tlv_end_nested(&src, start);
    }
    /* `true` here is overridden with merge policy */
    osw_tlv_merge(&dest, &prev, src.data, src.used, true, p, p2, STATMAX);
    osw_tlv_fini(&src);
    assert(dest.used > 0);
    assert(prev.used > 0);

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATN] != NULL);

        const struct osw_tlv_hdr *tb2[NESTMAX] = {0};
        osw_tlv_parse(osw_tlv_get_data(tb[STATN]), tb[STATN]->len, pn, tb2, NESTMAX);
        assert(tb2[NESTI] != NULL);
        assert(osw_tlv_get_u32(tb2[NESTI]) == 2);
    }
}

OSW_UT(osw_tlv_merge_overflow)
{
    enum {
        STATI,
        STATMAX,
    };
    const struct osw_tlv_policy p[STATMAX] = {
        [STATI] = { .type = OSW_TLV_U32 },
    };
    const struct osw_tlv_merge_policy p2[STATMAX] = {
        [STATI] = { .type = OSW_TLV_OP_ACCUMULATE },
    };

    struct osw_tlv dest = {0};
    struct osw_tlv prev = {0};
    struct osw_tlv src = {0};

    uint32_t i = 0xffffffff;
    osw_tlv_put_u32(&prev, STATI, i);

    osw_tlv_put_u32(&src, STATI, 2);
    osw_tlv_merge(&dest, &prev, src.data, src.used, false, p, p2, STATMAX);
    osw_tlv_fini(&src);

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATI] != NULL);
        assert(osw_tlv_get_u32(tb[STATI]) == 3);
    }
}

OSW_UT(osw_tlv_merge_optype)
{
    enum {
        STATI1,
        STATI2,
        STATI3,
        STATI4,
        STATMAX,
    };
    const struct osw_tlv_policy p[STATMAX] = {
        [STATI1] = { .type = OSW_TLV_U32 },
        [STATI2] = { .type = OSW_TLV_U32 },
        [STATI3] = { .type = OSW_TLV_U32 },
        [STATI4] = { .type = OSW_TLV_U32 },
    };
    const struct osw_tlv_merge_policy p2[STATMAX] = {
        [STATI1] = { .type = OSW_TLV_OP_NONE },
        [STATI2] = { .type = OSW_TLV_OP_OVERWRITE },
        [STATI3] = { .type = OSW_TLV_OP_ACCUMULATE },
        [STATI4] = { .type = OSW_TLV_OP_MERGE },
    };

    struct osw_tlv dest = {0};
    struct osw_tlv prev = {0};
    struct osw_tlv src = {0};

    /* note: STATI4 should generate a WARN_ON(), can't check it here */

    osw_tlv_put_u32_delta(&src, STATI1, 1);
    osw_tlv_put_u32_delta(&src, STATI2, 2);
    osw_tlv_put_u32_delta(&src, STATI3, 3);
    osw_tlv_put_u32_delta(&src, STATI4, 4);
    osw_tlv_merge(&dest, &prev, src.data, src.used, false, p, p2, STATMAX);
    osw_tlv_fini(&src);

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATI1] == NULL);
        assert(tb[STATI2] != NULL);
        assert(tb[STATI3] != NULL);
        assert(tb[STATI4] == NULL);
        assert(osw_tlv_get_u32(tb[STATI2]) == 2);
        assert(osw_tlv_get_u32(tb[STATI3]) == 3);
    }

    osw_tlv_put_u32_delta(&src, STATI2, 4);
    osw_tlv_put_u32_delta(&src, STATI3, 6);
    osw_tlv_merge(&dest, &prev, src.data, src.used, false, p, p2, STATMAX);
    osw_tlv_fini(&src);

    {
        const struct osw_tlv_hdr *tb[STATMAX] = {0};
        osw_tlv_parse(dest.data, dest.used, p, tb, STATMAX);
        assert(tb[STATI1] == NULL);
        assert(tb[STATI2] != NULL);
        assert(tb[STATI3] != NULL);
        assert(tb[STATI4] == NULL);
        assert(osw_tlv_get_u32(tb[STATI2]) == 4);
        assert(osw_tlv_get_u32(tb[STATI3]) == (3 + 6));
    }
}

OSW_UT(osw_tlv_merge_repack_str)
{
    enum {
        STAT1,
        STATMAX,
    };
    const struct osw_tlv_policy p[STATMAX] = {
        [STAT1] = { .type = OSW_TLV_STRING },
    };
    const struct osw_tlv_merge_policy p2[STATMAX] = {
        [STAT1] = { .type = OSW_TLV_OP_OVERWRITE },
    };

    struct osw_tlv dest = {0};
    struct osw_tlv prev = {0};
    struct osw_tlv src = {0};

    osw_tlv_put_string(&src, STAT1, "1234");
    osw_tlv_merge(&dest, &prev, src.data, src.used, false, p, p2, STATMAX);
    osw_tlv_fini(&src);

    assert(dest.used > 0);
    assert(prev.used == 0);

    const size_t len1 = dest.used;

    osw_tlv_put_string(&src, STAT1, "12345678");
    osw_tlv_merge(&dest, &prev, src.data, src.used, false, p, p2, STATMAX);
    osw_tlv_fini(&src);

    assert(dest.used > 0);
    assert(prev.used == 0);
    assert(dest.used != len1);

    const size_t len2 = dest.used;

    osw_tlv_put_string(&src, STAT1, "1234");
    osw_tlv_merge(&dest, &prev, src.data, src.used, false, p, p2, STATMAX);
    osw_tlv_fini(&src);

    assert(dest.used > 0);
    assert(prev.used == 0);
    assert(dest.used != len2);
    assert(dest.used == len1);
}
