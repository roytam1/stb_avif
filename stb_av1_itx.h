/*
 * Scalar AV1 inverse transforms (all rect sizes and tx types) for the
 * minimal intra still-picture decoder, following dav1d 1.5.4's
 * inv_txfm_add_c() exactly.
 *
 * Copyright © 2018-2019, VideoLAN and dav1d authors
 * Copyright © 2018-2019, Two Orioles, LLC
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef STB_AV1_ITX_H
#define STB_AV1_ITX_H

#ifndef STBV_I32_DEFINED
#error "stb_av1_itx.h requires stbv_i32"
#endif

#include <string.h>
#include "stb_av1_tx.h"
#include "stb_av1_itx1d.h"

/* dav1d enum Tx1dType ordering: DCT, ADST, FLIPADST, IDENTITY. */
typedef void (*stbv_av1_itx1d_fn)(stbv_i32 *, const int, const int, const int);

static const stbv_av1_itx1d_fn stbv_av1_tx1d_fns[5][4] = {
    { inv_dct4_1d_c,  inv_adst4_1d_c,  inv_flipadst4_1d_c,  inv_identity4_1d_c  },
    { inv_dct8_1d_c,  inv_adst8_1d_c,  inv_flipadst8_1d_c,  inv_identity8_1d_c  },
    { inv_dct16_1d_c, inv_adst16_1d_c, inv_flipadst16_1d_c, inv_identity16_1d_c },
    { inv_dct32_1d_c, NULL,            NULL,                inv_identity32_1d_c },
    { inv_dct64_1d_c, NULL,            NULL,                NULL                }
};

/* dav1d_tx1d_types[]: {first (width-axis), second (height-axis)} per txtp. */
/* Effective 1D type pairs.  NOTE: dav1d assigns the itxfm_add slots
 * cross-wise ([ADST_DCT] = fn_dct_adst etc.) because its intermediate
 * buffer is transposed; replicating the EFFECTIVE mapping here:
 *   e.g. decoded ADST_DCT runs first=DCT (columns), second=ADST (rows). */
static const stbv_u8 stbv_av1_tx1d_types[STBV_AV1_TX_WHT_WHT + 1][2] = {
    { 0, 0 }, /* DCT_DCT           */
    { 0, 1 }, /* ADST_DCT          */
    { 1, 0 }, /* DCT_ADST          */
    { 1, 1 }, /* ADST_ADST         */
    { 0, 3 }, /* FLIPADST_DCT      */
    { 3, 0 }, /* DCT_FLIPADST      */
    { 3, 3 }, /* FLIPADST_FLIPADST */
    { 3, 1 }, /* ADST_FLIPADST     */
    { 1, 3 }, /* FLIPADST_ADST     */
    { 2, 2 }, /* IDTX              */
    { 2, 0 }, /* V_DCT             */
    { 0, 2 }, /* H_DCT             */
    { 2, 1 }, /* V_ADST            */
    { 1, 2 }, /* H_ADST            */
    { 2, 3 }, /* V_FLIPADST        */
    { 3, 2 }, /* H_FLIPADST        */
    { 0, 0 }  /* WHT_WHT (unused)  */
};

/* Intermediate rounding shift per transform size, in tx enum order
 * (dav1d inv_txfm_fn*() instantiation list). */
static const unsigned char stbv_av1_itx_shifts[STBV_AV1_N_TX_SIZES] = {
    0, 1, 2, 2, 2, /* 4x4, 8x8, 16x16, 32x32, 64x64 */
    0, 0,          /* 4x8, 8x4  */
    1, 1, 1, 1, 1, 1, /* 8x16, 16x8, 16x32, 32x16, 32x64, 64x32 */
    1, 1,          /* 4x16, 16x4 */
    2, 2, 2, 2     /* 8x32, 32x8, 16x64, 64x16 */
};

#define STBV_AV1_ITX_CLIP(v, lo, hi) \
    ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

/*
 * Compute the 2-D inverse transform into tmp[] (w*h entries, layout
 * tmp[y*w+x], values still needing the final (v+8)>>4 rounding).
 *
 * Returns 0 and sets *dc_out for the DC-only shortcut (eob == 0 and
 * DCT_DCT); returns 1 when tmp[] was filled.  Mirrors dav1d
 * inv_txfm_add_c(); unlike dav1d's C reference, the coefficient grid is
 * explicitly zero-padded from its clipped sw x sh extent up to the full
 * w x h transform input, which is the semantics the SIMD paths implement
 * and what the spec requires (coefficients outside the coded 32x32
 * sub-grid are implicitly zero).
 */
static int stbv_av1_inv_txfm_core(stbv_i32 *coeff, const int eob,
                                  const int tx, const int txtp,
                                  const int bd, stbv_i32 *tmp, int *dc_out)
{
    const stbv_av1_tx_dim *t_dim = &stbv_av1_tx_dims[tx];
    const int w = 4 * t_dim->w, h = 4 * t_dim->h;
    const int has_dconly = txtp == STBV_AV1_TX_DCT_DCT;
    const int is_rect2 = w * 2 == h || h * 2 == w;
    const int shift = stbv_av1_itx_shifts[tx];
    const int rnd = (1 << shift) >> 1;
    const int sh = h < 32 ? h : 32, sw = w < 32 ? w : 32;
    const stbv_u8 *txtps;
    stbv_av1_itx1d_fn first_fn, second_fn;
    int row_clip_min, row_clip_max, col_clip_min, col_clip_max;
    stbv_i32 *c;
    int y, x, i;

    if (eob < has_dconly) {
        int dc = coeff[0];
        coeff[0] = 0;
        if (is_rect2)
            dc = (dc * 181 + 128) >> 8;
        dc = (dc * 181 + 128) >> 8;
        dc = (dc + rnd) >> shift;
        *dc_out = (dc * 181 + 128 + 2048) >> 12;
        return 0;
    }

    if (bd == 8) {
        row_clip_min = -32768;
        col_clip_min = -32768;
    } else {
        const unsigned max = (1U << bd) - 1;
        row_clip_min = (int)((unsigned)~max << 7);
        col_clip_min = (int)((unsigned)~max << 5);
    }
    row_clip_max = ~row_clip_min;
    col_clip_max = ~col_clip_min;

    txtps = stbv_av1_tx1d_types[txtp];
    first_fn = stbv_av1_tx1d_fns[t_dim->lw][txtps[0]];
    second_fn = stbv_av1_tx1d_fns[t_dim->lh][txtps[1]];
    if (!first_fn) first_fn = stbv_av1_tx1d_fns[t_dim->lw][0];
    if (!second_fn) second_fn = stbv_av1_tx1d_fns[t_dim->lh][0];

    c = tmp;
    for (y = 0; y < sh; y++, c += w) {
        if (is_rect2) {
            for (x = 0; x < sw; x++)
                c[x] = (coeff[y + x * sh] * 181 + 128) >> 8;
        } else {
            for (x = 0; x < sw; x++)
                c[x] = coeff[y + x * sh];
        }
        for (x = sw; x < w; x++)
            c[x] = 0;
        first_fn(c, 1, row_clip_min, row_clip_max);
    }
    if (sh < h)
        memset(tmp + sh * w, 0, (size_t)((h - sh) * w) * sizeof(stbv_i32));

    memset(coeff, 0, (size_t)(sw * sh) * sizeof(stbv_i32));
    for (i = 0; i < w * h; i++)
        tmp[i] = STBV_AV1_ITX_CLIP((tmp[i] + rnd) >> shift,
                                   col_clip_min, col_clip_max);

    for (x = 0; x < w; x++)
        second_fn(&tmp[x], w, col_clip_min, col_clip_max);
    return 1;
}

static void stbv_av1_inv_wht4_1d(stbv_i32 *c, const int stride)
{
    const int in0 = c[0 * stride], in1 = c[1 * stride];
    const int in2 = c[2 * stride], in3 = c[3 * stride];

    const int t0 = in0 + in1;
    const int t2 = in2 - in3;
    const int t4 = (t0 - t2) >> 1;
    const int t3 = t4 - in3;
    const int t1 = t4 - in1;

    c[0 * stride] = t0 - t3;
    c[1 * stride] = t3;
    c[2 * stride] = t1;
    c[3 * stride] = t2 + t1;
}

/* WHT_WHT (lossless) 4x4; dav1d inv_txfm_add_wht_wht_4x4_c(). */
static int stbv_av1_inv_wht_core(stbv_i32 *coeff, stbv_i32 *tmp)
{
    int x, y;
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++)
            tmp[y * 4 + x] = coeff[y + x * 4] >> 2;
        stbv_av1_inv_wht4_1d(tmp + y * 4, 1);
    }
    memset(coeff, 0, (size_t)(4 * 4) * sizeof(stbv_i32));
    for (x = 0; x < 4; x++)
        stbv_av1_inv_wht4_1d(&tmp[x], 4);
    return 1;
}

/* 8-bit output: dst has stride bytes; pixel range 0..255. */
static void stbv_av1_inv_txfm_add8(stbv_u8 *dst, const int stride,
                                   stbv_i32 *coeff, const int eob,
                                   const int tx, const int txtp)
{
    stbv_i32 tmp[64 * 64];
    int dc = 0;
    int x, y;

    if (txtp == STBV_AV1_TX_WHT_WHT) {
        if (stbv_av1_inv_wht_core(coeff, tmp)) {
            for (y = 0; y < 4; y++, dst += stride)
                for (x = 0; x < 4; x++) {
                    int v = dst[x] + tmp[y * 4 + x];
                    dst[x] = (stbv_u8)STBV_AV1_ITX_CLIP(v, 0, 255);
                }
        }
        return;
    }

    if (!stbv_av1_inv_txfm_core(coeff, eob, tx, txtp, 8, tmp, &dc)) {
        for (y = 0; y < 4 * stbv_av1_tx_dims[tx].h; y++, dst += stride)
            for (x = 0; x < 4 * stbv_av1_tx_dims[tx].w; x++) {
                int v = dst[x] + dc;
                dst[x] = (stbv_u8)STBV_AV1_ITX_CLIP(v, 0, 255);
            }
        return;
    }

    {
        const int w = 4 * stbv_av1_tx_dims[tx].w;
        const int h = 4 * stbv_av1_tx_dims[tx].h;
        const stbv_i32 *t = tmp;
        for (y = 0; y < h; y++, dst += stride)
            for (x = 0; x < w; x++) {
                int v = dst[x] + ((*t++ + 8) >> 4);
                dst[x] = (stbv_u8)STBV_AV1_ITX_CLIP(v, 0, 255);
            }
    }
}

/* High-bit-depth output: dst is u16 with stride units; pixel range
 * 0..(1<<bd)-1. */
static void stbv_av1_inv_txfm_add16(stbv_u16 *dst, const int stride,
                                    stbv_i32 *coeff, const int eob,
                                    const int tx, const int txtp, const int bd)
{
    stbv_i32 tmp[64 * 64];
    const int max_pix = (1 << bd) - 1;
    int dc = 0;
    int x, y;

    if (txtp == STBV_AV1_TX_WHT_WHT) {
        if (stbv_av1_inv_wht_core(coeff, tmp)) {
            for (y = 0; y < 4; y++, dst += stride)
                for (x = 0; x < 4; x++) {
                    int v = dst[x] + tmp[y * 4 + x];
                    dst[x] = (stbv_u16)STBV_AV1_ITX_CLIP(v, 0, max_pix);
                }
        }
        return;
    }

    if (!stbv_av1_inv_txfm_core(coeff, eob, tx, txtp, bd, tmp, &dc)) {
        for (y = 0; y < 4 * stbv_av1_tx_dims[tx].h; y++, dst += stride)
            for (x = 0; x < 4 * stbv_av1_tx_dims[tx].w; x++) {
                int v = dst[x] + dc;
                dst[x] = (stbv_u16)STBV_AV1_ITX_CLIP(v, 0, max_pix);
            }
        return;
    }

    {
        const int w = 4 * stbv_av1_tx_dims[tx].w;
        const int h = 4 * stbv_av1_tx_dims[tx].h;
        const stbv_i32 *t = tmp;
        for (y = 0; y < h; y++, dst += stride)
            for (x = 0; x < w; x++) {
                int v = dst[x] + ((*t++ + 8) >> 4);
                dst[x] = (stbv_u16)STBV_AV1_ITX_CLIP(v, 0, max_pix);
            }
    }
}

#undef STBV_AV1_ITX_CLIP
#endif
