/*
 * stb_av1_getbits.h - scalar AV1 uncompressed-bitstream reader
 *
 * Portions are derived from dav1d 1.5.4 src/getbits.c / src/getbits.h.
 * Copyright (C) 2018, VideoLAN and dav1d authors
 * Copyright (C) 2018, Two Orioles, LLC
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef STB_AV1_GETBITS_H
#define STB_AV1_GETBITS_H

/* The including file must provide stbv_u8, stbv_u32, stbv_u64 and size_t. */
struct stb_av1_getbits {
    stbv_u64 state;
    int bits_left;
    int error;
    const stbv_u8 *ptr;
    const stbv_u8 *ptr_start;
    const stbv_u8 *ptr_end;
};

static void stb_av1_getbits_init(struct stb_av1_getbits *c,
                                 const stbv_u8 *data, size_t size)
{
    c->ptr = data;
    c->ptr_start = data;
    c->ptr_end = data + size;
    c->state = 0;
    c->bits_left = 0;
    c->error = 0;
}

static unsigned int stb_av1_get_bit(struct stb_av1_getbits *c)
{
    stbv_u64 state;
    if (!c->bits_left) {
        if (c->ptr >= c->ptr_end) {
            c->error = 1;
            return 0;
        }
        state = *c->ptr++;
        c->bits_left = 7;
        c->state = (stbv_u64)state << 57;
        return (unsigned int)(state >> 7);
    }
    state = c->state;
    c->bits_left--;
    c->state = state << 1;
    return (unsigned int)(state >> 63);
}

static void stb_av1_getbits_refill(struct stb_av1_getbits *c, int n)
{
    unsigned int state = 0;
    do {
        if (c->ptr >= c->ptr_end) {
            c->error = 1;
            if (state)
                break;
            return;
        }
        state = (state << 8) | *c->ptr++;
        c->bits_left += 8;
    } while (n > c->bits_left);
    c->state |= (stbv_u64)state << (64 - c->bits_left);
}

static unsigned int stb_av1_get_bits(struct stb_av1_getbits *c, int n)
{
    stbv_u64 state;
    unsigned int v;
    if (n <= 0 || n > 32) {
        c->error = 1;
        return 0;
    }
    if ((unsigned int)n > (unsigned int)c->bits_left)
        stb_av1_getbits_refill(c, n);
    state = c->state;
    c->bits_left -= n;
    c->state = state << n;
    v = (unsigned int)(state >> (64 - n));
    return v;
}

static int stb_av1_get_sbits(struct stb_av1_getbits *c, int n)
{
    unsigned int v;
    if (n <= 0 || n > 31) {
        c->error = 1;
        return 0;
    }
    v = stb_av1_get_bits(c, n);
    if (v & ((unsigned int)1 << (n - 1)))
        return (int)(v - ((unsigned int)1 << n));
    return (int)v;
}

static unsigned int stb_av1_get_uleb128(struct stb_av1_getbits *c)
{
    stbv_u64 val = 0;
    unsigned int i = 0;
    unsigned int more;
    do {
        unsigned int v = stb_av1_get_bits(c, 8);
        more = v & 0x80U;
        val |= (stbv_u64)(v & 0x7fU) << i;
        i += 7;
    } while (more && i < 56);
    if (val > 0xffffffffU || more) {
        c->error = 1;
        return 0;
    }
    return (unsigned int)val;
}

static unsigned int stb_av1_get_uniform(struct stb_av1_getbits *c,
                                         unsigned int max)
{
    unsigned int l = 0;
    unsigned int m;
    unsigned int v;
    if (max <= 1)
        return 0;
    while (((unsigned int)1 << l) < max)
        l++;
    m = ((unsigned int)1 << l) - max;
    v = stb_av1_get_bits(c, (int)l - 1);
    if (v < m)
        return v;
    return (v << 1) - m + stb_av1_get_bit(c);
}

static unsigned int stb_av1_get_vlc(struct stb_av1_getbits *c)
{
    unsigned int n_bits = 0;
    if (stb_av1_get_bit(c))
        return 0;
    for (;;) {
        if (++n_bits == 32)
            return 0xffffffffU;
        if (stb_av1_get_bit(c))
            break;
    }
    return (((unsigned int)1 << n_bits) - 1U) +
           stb_av1_get_bits(c, (int)n_bits);
}

/* AV1's inv_recenter() helper. */
static unsigned int stb_av1_inv_recenter(unsigned int r, unsigned int v)
{
    if (v > 2 * r)
        return v;
    if (v & 1U)
        return r - ((v + 1U) >> 1);
    return r + (v >> 1);
}

static unsigned int stb_av1_get_bits_subexp(struct stb_av1_getbits *c,
                                             unsigned int ref,
                                             unsigned int n)
{
    unsigned int v = 0;
    unsigned int i;
    for (i = 0;; i++) {
        unsigned int b = i ? 3U + i - 1U : 3U;
        unsigned int range = 3U * ((unsigned int)1 << b);
        if (n < v + range) {
            v += stb_av1_get_uniform(c, n - v + 1U);
            break;
        }
        if (!stb_av1_get_bit(c)) {
            v += stb_av1_get_bits(c, (int)b);
            break;
        }
        v += (unsigned int)1 << b;
    }
    if (ref * 2U <= n)
        return stb_av1_inv_recenter(ref, v);
    return n - stb_av1_inv_recenter(n - ref, v);
}

static unsigned int stb_av1_get_bits_pos(const struct stb_av1_getbits *c)
{
    return (unsigned int)((c->ptr - c->ptr_start) * 8 - c->bits_left);
}

static void stb_av1_getbits_bytealign(struct stb_av1_getbits *c)
{
    c->bits_left = 0;
    c->state = 0;
}

#endif /* STB_AV1_GETBITS_H */
