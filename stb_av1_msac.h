/* stb_av1_msac.h - scalar AV1 MSAC entropy decoder.
 * Verbatim port of dav1d 1.5.4 src/msac.c|h (BSD-2-Clause) onto the
 * stbv_* typedefs; clz replaced by a shift loop for C89/MSVC6. */
#ifndef STB_AV1_MSAC_H
#define STB_AV1_MSAC_H

struct stb_av1_msac {
    const stbv_u8 *buf_pos;
    const stbv_u8 *buf_end;
    stbv_u64 dif;
    stbv_u32 rng;
    int cnt;
    int allow_update_cdf;
};

#ifdef STB_DBG_TRACE
/* shared debug state (single-TU builds); tentative definitions */
int stb_dbg_blknum;
int stb_dbg_blkx;
int stb_dbg_blky;
unsigned int stb_dbg_pre;
#define STB_DBG_PRE(s) ((stb_dbg_pre = (s)->rng))
#endif

#define STB_AV1_MSAC_EC_PROB_SHIFT 6
#define STB_AV1_MSAC_EC_MIN_PROB 4
#define STB_AV1_MSAC_EC_WIN_SIZE ((int)(sizeof(stbv_u64) * 8))

static int stb_av1_msac_clz(stbv_u32 m)
{
    int b = 0;
    while (!(m & 0x80000000U)) {
        m <<= 1;
        b++;
        if (b >= 32) break;
    }
    return b;
}

static void stb_av1_msac_refill(struct stb_av1_msac *s)
{
    const stbv_u8 *buf_pos = s->buf_pos;
    const stbv_u8 *buf_end = s->buf_end;
    int c = STB_AV1_MSAC_EC_WIN_SIZE - s->cnt - 24;
    stbv_u64 dif = s->dif;
    do {
        if (buf_pos >= buf_end) {
            dif |= ~(~(stbv_u64)0xff << c);
            break;
        }
        dif |= (stbv_u64)(*buf_pos++ ^ 0xff) << c;
        c -= 8;
    } while (c >= 0);
    s->dif = dif;
    s->cnt = STB_AV1_MSAC_EC_WIN_SIZE - c - 24;
    s->buf_pos = buf_pos;
}

static void stb_av1_msac_norm(struct stb_av1_msac *s,
                              stbv_u64 dif, stbv_u32 rng)
{
    const int d = 15 ^ (31 ^ stb_av1_msac_clz(rng));
    const int cnt = s->cnt;
    s->dif = dif << d;
    s->rng = rng << d;
    s->cnt = cnt - d;
    /* unsigned compare avoids redundant refills at eob */
    if ((stbv_u32)cnt < (stbv_u32)d)
        stb_av1_msac_refill(s);
}

static unsigned int stb_av1_msac_bool_equi(struct stb_av1_msac *s)
{
    const stbv_u32 r = s->rng;
    stbv_u64 dif = s->dif;
    stbv_u32 v = ((r >> 8) << 7) + STB_AV1_MSAC_EC_MIN_PROB;
    stbv_u64 vw = (stbv_u64)v << (STB_AV1_MSAC_EC_WIN_SIZE - 16);
    const stbv_u32 ret = dif >= vw;
    dif -= (stbv_u64)ret * vw;
    v += ret * (r - 2 * v);
    stb_av1_msac_norm(s, dif, v);
    return !ret;
}

static unsigned int stb_av1_msac_bool(struct stb_av1_msac *s,
                                      unsigned int f)
{
    const stbv_u32 r = s->rng;
    stbv_u64 dif = s->dif;
    stbv_u32 v = ((r >> 8) * (f >> STB_AV1_MSAC_EC_PROB_SHIFT)
                  >> (7 - STB_AV1_MSAC_EC_PROB_SHIFT)) +
                 STB_AV1_MSAC_EC_MIN_PROB;
    stbv_u64 vw = (stbv_u64)v << (STB_AV1_MSAC_EC_WIN_SIZE - 16);
    const stbv_u32 ret = dif >= vw;
    dif -= (stbv_u64)ret * vw;
    v += ret * (r - 2 * v);
    stb_av1_msac_norm(s, dif, v);
    return !ret;
}

static unsigned int stb_av1_msac_symbol(struct stb_av1_msac *s,
                                        stbv_u16 *cdf,
                                        size_t n_symbols)
{
    const stbv_u32 c = (stbv_u32)(s->dif >>
                        (STB_AV1_MSAC_EC_WIN_SIZE - 16));
    const stbv_u32 r = s->rng >> 8;
    stbv_u32 u, v = s->rng;
    unsigned int val = 0;

    /* dav1d: val starts at -1 and increments first; emulate with do-style
     * loop while keeping C89 declarations-at-top cleanliness. */
    do {
        u = v;
        v = r * (cdf[val] >> STB_AV1_MSAC_EC_PROB_SHIFT);
        v >>= 7 - STB_AV1_MSAC_EC_PROB_SHIFT;
        v += STB_AV1_MSAC_EC_MIN_PROB * ((unsigned int)n_symbols - val);
        if (c >= v)
            break;
        val++;
    } while (val <= n_symbols);

    stb_av1_msac_norm(s,
        s->dif - ((stbv_u64)v << (STB_AV1_MSAC_EC_WIN_SIZE - 16)),
        u - v);

    if (s->allow_update_cdf) {
        const stbv_u32 count = cdf[n_symbols];
        const stbv_u32 rate = 4 + (count >> 4) + (n_symbols > 2);
        stbv_u32 i;
        for (i = 0; i < val; i++)
            cdf[i] += (stbv_u16)((32768U - cdf[i]) >> rate);
        for (; i < (stbv_u32)n_symbols; i++)
            cdf[i] -= (stbv_u16)(cdf[i] >> rate);
        cdf[n_symbols] = (stbv_u16)(count + (count < 32));
    }

    return val;
}

static unsigned int stb_av1_msac_bool_adapt(struct stb_av1_msac *s,
                                            stbv_u16 *cdf)
{
    const unsigned int bit = stb_av1_msac_bool(s, cdf[0]);
    if (s->allow_update_cdf) {
        const stbv_u32 count = cdf[1];
        const stbv_u32 rate = 4 + (count >> 4);
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
    unsigned int l = 0, m, v;
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

#endif /* STB_AV1_MSAC_H */
