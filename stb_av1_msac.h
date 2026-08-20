/*
 * stb_av1_msac.h - scalar AV1 MSAC entropy decoder
 *
 * This component is intended for integration into stb_avif.h.
 *
 * Portions are derived from dav1d 1.5.4 src/msac.c / src/msac.h.
 * Copyright (C) 2018, VideoLAN and dav1d authors
 * Copyright (C) 2018, Two Orioles, LLC
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef STB_AV1_MSAC_H
#define STB_AV1_MSAC_H

/* The including file must provide:
 *   stbv_u8, stbv_u16, stbv_u32, stbv_u64
 * and size_t.
 */

struct stb_av1_msac {
    const stbv_u8 *buf_pos;
    const stbv_u8 *buf_end;
    stbv_u64 dif;
    stbv_u32 rng;
    int cnt;
    int allow_update_cdf;
};

#define STB_AV1_MSAC_EC_PROB_SHIFT 6
#define STB_AV1_MSAC_EC_MIN_PROB 4
#define STB_AV1_MSAC_EC_WIN_SIZE ((int)(sizeof(stbv_u64) * 8))

/* Return the number of left shifts needed to put rng in [32768, 65536).
 * rng is required to be non-zero.  This deliberately avoids compiler-specific
 * clz builtins so that the code remains usable from C89 implementations.
 */
static int stb_av1_msac_norm_shift(stbv_u32 rng)
{
    int d = 0;
    while (rng < 32768U) {
        rng <<= 1;
        d++;
    }
    return d;
}

static void stb_av1_msac_refill(struct stb_av1_msac *s)
{
    const stbv_u8 *p = s->buf_pos;
    const stbv_u8 *end = s->buf_end;
    int c = STB_AV1_MSAC_EC_WIN_SIZE - s->cnt - 24;
    stbv_u64 dif = s->dif;

    do {
        if (p >= end) {
            /* dav1d fills unavailable input bits with ones. */
            dif |= ~(~(stbv_u64)0xff << c);
            break;
        }
        dif |= (stbv_u64)(*p++ ^ 0xff) << c;
        c -= 8;
    } while (c >= 0);

    s->dif = dif;
    s->cnt = STB_AV1_MSAC_EC_WIN_SIZE - c - 24;
    s->buf_pos = p;
}

static void stb_av1_msac_init(struct stb_av1_msac *s,
                              const stbv_u8 *data, size_t size,
                              int disable_cdf_update)
{
    s->buf_pos = data;
    s->buf_end = data + size;
    s->dif = 0;
    s->rng = 0x8000U;
    s->cnt = -15;
    s->allow_update_cdf = !disable_cdf_update;
    stb_av1_msac_refill(s);
}

static void stb_av1_msac_norm(struct stb_av1_msac *s,
                              stbv_u64 dif, stbv_u32 rng)
{
    int d = stb_av1_msac_norm_shift(rng);
    int cnt = s->cnt;

    /* dav1d keeps rng in 16 bits: the update expressions v += ret*(r - 2*v)
     * and u - v are allowed to go negative and wrap modulo 2^16.  Truncate
     * here so the wrap matches dav1d instead of corrupting the high bits. */
    rng &= 0xFFFFU;

    s->dif = dif << d;
    s->rng = rng << d;
    s->cnt = cnt - d;

    if (cnt < d)
        stb_av1_msac_refill(s);
}

static unsigned int stb_av1_msac_bool_equi(struct stb_av1_msac *s)
{
    stbv_u32 r = s->rng;
    stbv_u64 dif = s->dif;
    unsigned int v;
    stbv_u64 vw;
    unsigned int ret;

    v = ((r >> 8) << 7) + STB_AV1_MSAC_EC_MIN_PROB;
    vw = (stbv_u64)v << (STB_AV1_MSAC_EC_WIN_SIZE - 16);
    ret = dif >= vw;
    dif -= (stbv_u64)ret * vw;
    v += ret * (r - 2 * v);
    stb_av1_msac_norm(s, dif, v);
    return !ret;
}

static unsigned int stb_av1_msac_bool(struct stb_av1_msac *s,
                                      unsigned int f)
{
    stbv_u32 r = s->rng;
    stbv_u64 dif = s->dif;
    unsigned int v;
    stbv_u64 vw;
    unsigned int ret;

    v = ((r >> 8) * (f >> STB_AV1_MSAC_EC_PROB_SHIFT) >>
         (7 - STB_AV1_MSAC_EC_PROB_SHIFT)) +
        STB_AV1_MSAC_EC_MIN_PROB;
    vw = (stbv_u64)v << (STB_AV1_MSAC_EC_WIN_SIZE - 16);
    ret = dif >= vw;
    dif -= (stbv_u64)ret * vw;
    v += ret * (r - 2 * v);
    fprintf(stderr, "DBG bool r=%u f=%u v=%u ret=%u vp=%u d=%llu\n",
            r, f, (unsigned)(((r >> 8) * (f >> STB_AV1_MSAC_EC_PROB_SHIFT) >>
             (7 - STB_AV1_MSAC_EC_PROB_SHIFT)) + STB_AV1_MSAC_EC_MIN_PROB),
            ret, v, (unsigned long long)dif);
    stb_av1_msac_norm(s, dif, v);
    return !ret;
}

static unsigned int stb_av1_msac_symbol(struct stb_av1_msac *s,
                                        stbv_u16 *cdf,
                                        size_t n_symbols)
{
    unsigned int c = (unsigned int)(s->dif >>
                          (STB_AV1_MSAC_EC_WIN_SIZE - 16));
    unsigned int r = s->rng >> 8;
    unsigned int u;
    unsigned int v = s->rng;
    unsigned int val = 0;
    unsigned int count;
    unsigned int rate;
    unsigned int i;

    /* dav1d stores n_symbols CDF thresholds followed by the adaptation
       count at cdf[n_symbols].  Never interpret that count as a threshold.
       val runs over [0..n_symbols]; at val == n_symbols the min-prob term
       (and the count itself, which is <= 32) makes v zero, so the loop is
       guaranteed to terminate.  u tracks the previous cumulative range, so
       the new range is u - v, matching dav1d_msac_decode_symbol_adapt_c. */
    for (;;) {
        u = v;
        if (val >= (unsigned int)n_symbols) {
            v = 0;
            break;
        }
        v = r * (cdf[val] >> STB_AV1_MSAC_EC_PROB_SHIFT);
        v >>= 7 - STB_AV1_MSAC_EC_PROB_SHIFT;
        v += STB_AV1_MSAC_EC_MIN_PROB * ((unsigned int)n_symbols - val);
        if (c >= v)
            break;
        val++;
    }

    stb_av1_msac_norm(s,
        s->dif - ((stbv_u64)v << (STB_AV1_MSAC_EC_WIN_SIZE - 16)),
        u - v);

    if (s->allow_update_cdf) {
        count = cdf[n_symbols];
        rate = 4 + (count >> 4) + (n_symbols > 2);

        for (i = 0; i < val; i++)
            cdf[i] += (stbv_u16)((32768U - cdf[i]) >> rate);
        for (; i < (unsigned int)n_symbols; i++)
            cdf[i] -= (stbv_u16)(cdf[i] >> rate);

        cdf[n_symbols] = (stbv_u16)(count + (count < 32));
    }

    return val;
}

static unsigned int stb_av1_msac_bool_adapt(struct stb_av1_msac *s,
                                            stbv_u16 *cdf)
{
    unsigned int bit = stb_av1_msac_bool(s, cdf[0]);
    unsigned int count;
    int rate;

    if (s->allow_update_cdf) {
        count = cdf[1];
        rate = 4 + (int)(count >> 4);
        if (bit)
            cdf[0] += (stbv_u16)((32768U - cdf[0]) >> rate);
        else
            cdf[0] -= (stbv_u16)(cdf[0] >> rate);
        cdf[1] = (stbv_u16)(count + (count < 32));
    }
    return bit;
}

static unsigned int stb_av1_msac_bools(struct stb_av1_msac *s,
                                       unsigned int n)
{
    unsigned int v = 0;
    while (n--)
        v = (v << 1) | stb_av1_msac_bool_equi(s);
    return v;
}

static unsigned int stb_av1_msac_uniform(struct stb_av1_msac *s,
                                         unsigned int n)
{
    unsigned int l = 0;
    unsigned int m;
    unsigned int v;

    if (n <= 1)
        return 0;

    while (((unsigned int)1 << l) < n)
        l++;
    m = ((unsigned int)1 << l) - n;
    v = stb_av1_msac_bools(s, l - 1);
    if (v < m)
        return v;
    return (v << 1) - m + stb_av1_msac_bool_equi(s);
}

#endif /* STB_AV1_MSAC_H */
