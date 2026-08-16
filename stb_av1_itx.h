/*
 * Scalar AV1 inverse transforms for the initial intra-only decoder.
 * 1-D kernels adapted from dav1d 1.5.4.
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

#include "stb_av1_itx1d.h"

#define STBV_AV1_TX_DCT_DCT              0
#define STBV_AV1_TX_ADST_DCT             1
#define STBV_AV1_TX_DCT_ADST             2
#define STBV_AV1_TX_ADST_ADST            3
#define STBV_AV1_TX_FLIPADST_DCT         4
#define STBV_AV1_TX_DCT_FLIPADST         5
#define STBV_AV1_TX_FLIPADST_FLIPADST    6
#define STBV_AV1_TX_ADST_FLIPADST        7
#define STBV_AV1_TX_FLIPADST_ADST        8

static int stbv_av1_itx_shift(int n)
{
    if (n == 4) return 0;
    if (n == 8) return 1;
    return 2;
}

static void (*stbv_av1_itx_fn(int n, int type, int second))(stbv_i32 *, const int, const int, const int)
{
    int kind;

    /* dav1d_tx1d_types[type] is { row-transform, column-transform }. */
    /* Explicit mapping is less error-prone than trying to encode the
     * 2-element dav1d table in arithmetic. */
    if (!second) {
        switch (type) {
        case STBV_AV1_TX_ADST_DCT:
        case STBV_AV1_TX_ADST_ADST:
        case STBV_AV1_TX_ADST_FLIPADST:
            kind = 1; break;
        case STBV_AV1_TX_FLIPADST_DCT:
        case STBV_AV1_TX_FLIPADST_FLIPADST:
        case STBV_AV1_TX_FLIPADST_ADST:
            kind = 2; break;
        default:
            kind = 0; break;
        }
    } else {
        switch (type) {
        case STBV_AV1_TX_DCT_ADST:
        case STBV_AV1_TX_ADST_ADST:
        case STBV_AV1_TX_FLIPADST_ADST:
            kind = 1; break;
        case STBV_AV1_TX_DCT_FLIPADST:
        case STBV_AV1_TX_ADST_FLIPADST:
        case STBV_AV1_TX_FLIPADST_FLIPADST:
            kind = 2; break;
        default:
            kind = 0; break;
        }
    }

    if (kind == 0) {
        if (n == 4) return inv_dct4_1d_c;
        if (n == 8) return inv_dct8_1d_c;
        if (n == 16) return inv_dct16_1d_c;
        return inv_dct32_1d_c;
    }
    if (kind == 1) {
        if (n == 4) return inv_adst4_1d_c;
        if (n == 8) return inv_adst8_1d_c;
        if (n == 16) return inv_adst16_1d_c;
    }
    if (kind == 2) {
        if (n == 4) return inv_flipadst4_1d_c;
        if (n == 8) return inv_flipadst8_1d_c;
        if (n == 16) return inv_flipadst16_1d_c;
    }
    return (void (*)(stbv_i32 *, const int, const int, const int))0;
}

/* coeff is in AV1's transform coefficient layout: coefficient (x,y) is
 * coeff[x*n+y], which is the layout produced by stb_av1_coef.h. */
static int stbv_av1_inv_txfm_square(stbv_i32 *coeff, int n, int eob,
                                     int type, stbv_i32 *out)
{
    stbv_i32 tmp[32 * 32];
    void (*first)(stbv_i32 *, const int, const int, const int);
    void (*second)(stbv_i32 *, const int, const int, const int);
    int shift, rnd, last, y, x, v;

    if (n != 4 && n != 8 && n != 16 && n != 32)
        return -1;
    if (type < STBV_AV1_TX_DCT_DCT || type > STBV_AV1_TX_FLIPADST_ADST)
        return -1;

    /* AV1 does not permit ADST/FLIPADST for the 32x32 intra transforms in
     * this initial implementation. */
    if (n == 32 && type != STBV_AV1_TX_DCT_DCT)
        return -1;

    first = stbv_av1_itx_fn(n, type, 0);
    second = stbv_av1_itx_fn(n, type, 1);
    if (!first || !second)
        return -1;

    shift = stbv_av1_itx_shift(n);
    rnd = (1 << shift) >> 1;

    /* eob is a scan position. For the square 2-D transforms used here,
     * dav1d's last-nonzero-column optimization reduces to the scan index
     * lookup. Using eob itself is conservative and still exact. */
    (void)eob;
    last = n - 1;

    /* First 1-D transform operates across each row. The coefficient layout
     * is transposed (x*n+y), matching dav1d's inv_txfm_add_c(). */
    for (y = 0; y <= last; y++) {
        stbv_i32 *row = tmp + y * n;
        for (x = 0; x < n; x++)
            row[x] = coeff[y + x * n];
        first(row, 1, -32768, 32767);
    }

    for (y = last + 1; y < n; y++)
        for (x = 0; x < n; x++)
            tmp[y * n + x] = 0;

    /* Match dav1d's intermediate rounding between the two 1-D transforms. */
    for (v = 0; v < n * n; v++)
        tmp[v] = (tmp[v] + rnd) >> shift;

    for (x = 0; x < n; x++)
        second(tmp + x, n, -32768, 32767);

    for (y = 0; y < n; y++)
        for (x = 0; x < n; x++)
            out[y * n + x] = (tmp[y * n + x] + 8) >> 4;

    return 0;
}

#endif
