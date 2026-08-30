/*
 * Minimal AV1 transform-size/type decoding derived from dav1d 1.5.4.
 *
 * This file is intended for the scalar C89-oriented stb_avif decoder.
 * The transform syntax here deliberately covers the intra still-picture
 * path first; inter-only transform selection is not included.
 *
 * Copyright © 2018-2021, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED.
 */
#ifndef STB_AV1_TX_H
#define STB_AV1_TX_H

#include "stb_av1_msac.h"
#include "stb_av1_cdf.h"

#define STBV_AV1_TX_4X4   0
#define STBV_AV1_TX_8X8   1
#define STBV_AV1_TX_16X16 2
#define STBV_AV1_TX_32X32 3
#define STBV_AV1_TX_64X64 4
#define STBV_AV1_TX_4X8   5
#define STBV_AV1_TX_8X4   6
#define STBV_AV1_TX_8X16  7
#define STBV_AV1_TX_16X8  8
#define STBV_AV1_TX_16X32 9
#define STBV_AV1_TX_32X16 10
#define STBV_AV1_TX_32X64 11
#define STBV_AV1_TX_64X32 12
#define STBV_AV1_TX_4X16  13
#define STBV_AV1_TX_16X4  14
#define STBV_AV1_TX_8X32  15
#define STBV_AV1_TX_32X8  16
#define STBV_AV1_TX_16X64 17
#define STBV_AV1_TX_64X16 18
#define STBV_AV1_N_TX_SIZES 19

#define STBV_AV1_TX_DCT_DCT               0
#define STBV_AV1_TX_ADST_DCT              1
#define STBV_AV1_TX_DCT_ADST              2
#define STBV_AV1_TX_ADST_ADST             3
#define STBV_AV1_TX_FLIPADST_DCT          4
#define STBV_AV1_TX_DCT_FLIPADST          5
#define STBV_AV1_TX_FLIPADST_FLIPADST     6
#define STBV_AV1_TX_ADST_FLIPADST         7
#define STBV_AV1_TX_FLIPADST_ADST         8
#define STBV_AV1_TX_IDTX                  9
#define STBV_AV1_TX_V_DCT                 10
#define STBV_AV1_TX_H_DCT                 11
#define STBV_AV1_TX_V_ADST                12
#define STBV_AV1_TX_H_ADST                13
#define STBV_AV1_TX_V_FLIPADST            14
#define STBV_AV1_TX_H_FLIPADST            15
#define STBV_AV1_TX_WHT_WHT               16

#define STBV_AV1_TX_CLASS_2D              0
#define STBV_AV1_TX_CLASS_H               1
#define STBV_AV1_TX_CLASS_V               2

static const unsigned char stbv_av1_tx_set_intra2[5] = {
    STBV_AV1_TX_IDTX, STBV_AV1_TX_DCT_DCT, STBV_AV1_TX_ADST_ADST,
    STBV_AV1_TX_ADST_DCT, STBV_AV1_TX_DCT_ADST
};

static const unsigned char stbv_av1_tx_set_intra1[7] = {
    STBV_AV1_TX_IDTX, STBV_AV1_TX_DCT_DCT, STBV_AV1_TX_V_DCT,
    STBV_AV1_TX_H_DCT, STBV_AV1_TX_ADST_ADST, STBV_AV1_TX_ADST_DCT,
    STBV_AV1_TX_DCT_ADST
};

/* Inter tx type mapping tables from dav1d tables.c tx_types_per_set[]. */
static const unsigned char stbv_av1_tx_set_inter1[15] = {
    STBV_AV1_TX_IDTX, STBV_AV1_TX_V_DCT, STBV_AV1_TX_H_DCT,
    STBV_AV1_TX_V_ADST, STBV_AV1_TX_H_ADST, STBV_AV1_TX_V_FLIPADST,
    STBV_AV1_TX_H_FLIPADST, STBV_AV1_TX_DCT_DCT, STBV_AV1_TX_ADST_DCT,
    STBV_AV1_TX_DCT_ADST, STBV_AV1_TX_FLIPADST_DCT,
    STBV_AV1_TX_DCT_FLIPADST, STBV_AV1_TX_ADST_ADST,
    STBV_AV1_TX_FLIPADST_FLIPADST, STBV_AV1_TX_ADST_FLIPADST
};
static const unsigned char stbv_av1_tx_set_inter2[11] = {
    STBV_AV1_TX_IDTX, STBV_AV1_TX_V_DCT, STBV_AV1_TX_H_DCT,
    STBV_AV1_TX_DCT_DCT, STBV_AV1_TX_ADST_DCT, STBV_AV1_TX_DCT_ADST,
    STBV_AV1_TX_FLIPADST_DCT, STBV_AV1_TX_DCT_FLIPADST,
    STBV_AV1_TX_ADST_ADST, STBV_AV1_TX_FLIPADST_FLIPADST,
    STBV_AV1_TX_ADST_FLIPADST
};
static const unsigned char stbv_av1_tx_set_inter3[2] = {
    STBV_AV1_TX_IDTX, STBV_AV1_TX_DCT_DCT
};

/* Transform dimensions use 4x4 units. */
typedef struct stbv_av1_tx_dim {
    unsigned char w;
    unsigned char h;
    unsigned char lw;
    unsigned char lh;
    unsigned char min;
    unsigned char max;
    unsigned char sub;
    unsigned char ctx;
} stbv_av1_tx_dim;

static const stbv_av1_tx_dim stbv_av1_tx_dims[STBV_AV1_N_TX_SIZES] = {
    /* dav1d_txfm_dimensions[], in dav1d enum order. */
    { 1,  1, 0, 0, 0, 0, STBV_AV1_TX_4X4,   0 },
    { 2,  2, 1, 1, 1, 1, STBV_AV1_TX_4X4,   1 },
    { 4,  4, 2, 2, 2, 2, STBV_AV1_TX_8X8,   2 },
    { 8,  8, 3, 3, 3, 3, STBV_AV1_TX_16X16, 3 },
    {16, 16, 4, 4, 4, 4, STBV_AV1_TX_32X32, 4 },
    { 1,  2, 0, 1, 0, 1, STBV_AV1_TX_4X4,   1 },
    { 2,  1, 1, 0, 0, 1, STBV_AV1_TX_4X4,   1 },
    { 2,  4, 1, 2, 1, 2, STBV_AV1_TX_8X8,   2 },
    { 4,  2, 2, 1, 1, 2, STBV_AV1_TX_8X8,   2 },
    { 4,  8, 2, 3, 2, 3, STBV_AV1_TX_16X16, 3 },
    { 8,  4, 3, 2, 2, 3, STBV_AV1_TX_16X16, 3 },
    { 8, 16, 3, 4, 3, 4, STBV_AV1_TX_32X32, 4 },
    {16,  8, 4, 3, 3, 4, STBV_AV1_TX_32X32, 4 },
    { 1,  4, 0, 2, 0, 2, STBV_AV1_TX_4X8,   1 },
    { 4,  1, 2, 0, 0, 2, STBV_AV1_TX_8X4,   1 },
    { 2,  8, 1, 3, 1, 3, STBV_AV1_TX_8X16,  2 },
    { 8,  2, 3, 1, 1, 3, STBV_AV1_TX_16X8,  2 },
    { 4, 16, 2, 4, 2, 4, STBV_AV1_TX_16X32, 3 },
    {16,  4, 4, 2, 2, 4, STBV_AV1_TX_32X16, 3 }
};

/* dav1d_max_txfm_size_for_bs[bs][plane]: y, 420, 422, 444. */
static const stbv_u8 stbv_av1_max_tx_for_bs[STBV_AV1_N_BS_SIZES][4] = {
    {STBV_AV1_TX_64X64, STBV_AV1_TX_32X32, STBV_AV1_TX_32X32, STBV_AV1_TX_32X32},
    {STBV_AV1_TX_64X64, STBV_AV1_TX_32X32, STBV_AV1_TX_32X32, STBV_AV1_TX_32X32},
    {STBV_AV1_TX_64X64, STBV_AV1_TX_32X32, STBV_AV1_TX_4X4,   STBV_AV1_TX_32X32},
    {STBV_AV1_TX_64X64, STBV_AV1_TX_32X32, STBV_AV1_TX_32X32, STBV_AV1_TX_32X32},
    {STBV_AV1_TX_64X32, STBV_AV1_TX_32X16, STBV_AV1_TX_32X32, STBV_AV1_TX_32X32},
    {STBV_AV1_TX_64X16, STBV_AV1_TX_32X8,  STBV_AV1_TX_32X16, STBV_AV1_TX_32X16},
    {STBV_AV1_TX_32X64, STBV_AV1_TX_16X32, STBV_AV1_TX_4X4,   STBV_AV1_TX_32X32},
    {STBV_AV1_TX_32X32, STBV_AV1_TX_16X16, STBV_AV1_TX_16X32, STBV_AV1_TX_32X32},
    {STBV_AV1_TX_32X16, STBV_AV1_TX_16X8,  STBV_AV1_TX_16X16, STBV_AV1_TX_32X16},
    {STBV_AV1_TX_32X8,  STBV_AV1_TX_16X4,  STBV_AV1_TX_16X8,  STBV_AV1_TX_32X8},
    {STBV_AV1_TX_16X64, STBV_AV1_TX_8X32,  STBV_AV1_TX_4X4,   STBV_AV1_TX_16X32},
    {STBV_AV1_TX_16X32, STBV_AV1_TX_8X16,  STBV_AV1_TX_4X4,   STBV_AV1_TX_16X32},
    {STBV_AV1_TX_16X16, STBV_AV1_TX_8X8,   STBV_AV1_TX_8X16,  STBV_AV1_TX_16X16},
    {STBV_AV1_TX_16X8,  STBV_AV1_TX_8X4,   STBV_AV1_TX_8X8,   STBV_AV1_TX_16X8},
    {STBV_AV1_TX_16X4,  STBV_AV1_TX_8X4,   STBV_AV1_TX_8X4,   STBV_AV1_TX_16X4},
    {STBV_AV1_TX_8X32,  STBV_AV1_TX_4X16,  STBV_AV1_TX_4X4,   STBV_AV1_TX_8X32},
    {STBV_AV1_TX_8X16,  STBV_AV1_TX_4X8,   STBV_AV1_TX_4X4,   STBV_AV1_TX_8X16},
    {STBV_AV1_TX_8X8,   STBV_AV1_TX_4X4,   STBV_AV1_TX_4X8,   STBV_AV1_TX_8X8},
    {STBV_AV1_TX_8X4,   STBV_AV1_TX_4X4,   STBV_AV1_TX_4X4,   STBV_AV1_TX_8X4},
    {STBV_AV1_TX_4X16,  STBV_AV1_TX_4X8,   STBV_AV1_TX_4X4,   STBV_AV1_TX_4X16},
    {STBV_AV1_TX_4X8,   STBV_AV1_TX_4X4,   STBV_AV1_TX_4X4,   STBV_AV1_TX_4X8},
    {STBV_AV1_TX_4X4,   STBV_AV1_TX_4X4,   STBV_AV1_TX_4X4,   STBV_AV1_TX_4X4}
};

static int stbv_av1_tx_class(int tx_type)
{
    switch (tx_type) {
    case STBV_AV1_TX_V_DCT:
    case STBV_AV1_TX_V_ADST:
    case STBV_AV1_TX_V_FLIPADST:
        return STBV_AV1_TX_CLASS_V;
    case STBV_AV1_TX_H_DCT:
    case STBV_AV1_TX_H_ADST:
    case STBV_AV1_TX_H_FLIPADST:
        return STBV_AV1_TX_CLASS_H;
    default:
        return STBV_AV1_TX_CLASS_2D;
    }
}

/* dav1d_txtp_from_uvmode: chroma txtp derived from the intra UV mode.
 * Indexed by UV intra mode (DC,V,H,DDL,DDR,VR,HD,HU,VL,SMOOTH,
 * SMOOTH_V,SMOOTH_H,PAETH,CFL). */
static const unsigned char stbv_av1_txtp_from_uvmode[14] = {
    /* DC */        STBV_AV1_TX_DCT_DCT,
    /* VERT */      STBV_AV1_TX_ADST_DCT,
    /* HOR */       STBV_AV1_TX_DCT_ADST,
    /* DDL(45) */   STBV_AV1_TX_DCT_DCT,
    /* DDR(135) */  STBV_AV1_TX_ADST_ADST,
    /* VR(113) */   STBV_AV1_TX_ADST_DCT,
    /* HD(157) */   STBV_AV1_TX_DCT_ADST,
    /* HU(203) */   STBV_AV1_TX_DCT_ADST,
    /* VL(67) */    STBV_AV1_TX_ADST_DCT,
    /* SMOOTH */    STBV_AV1_TX_ADST_ADST,
    /* SMOOTH_V */  STBV_AV1_TX_ADST_DCT,
    /* SMOOTH_H */  STBV_AV1_TX_DCT_ADST,
    /* PAETH */     STBV_AV1_TX_ADST_ADST,
    /* CFL */       STBV_AV1_TX_DCT_DCT
};

/*
 * Decode one transform-size choice from dav1d's txsz CDF.
 *
 * max_tx is the maximum transform size for the current block.  tctx is the
 * transform-size context obtained from the neighbouring transform map.  The
 * returned value is the first transform size selected by the variable-tx
 * syntax; callers performing a full var-tx tree should repeat this operation
 * for each sub-transform using the txpart syntax below.
 */
static int stbv_av1_decode_tx_size(struct stb_av1_msac *msac,
                                   stbv_av1_cdf *cdf,
                                   int max_tx,
                                   int tctx)
{
    unsigned int depth;
    int n, max2;

    if (max_tx < 0 || max_tx >= STBV_AV1_N_TX_SIZES)
        return STBV_AV1_TX_4X4;
    /* dav1d: txsz[t_dim->max - 1][tctx], where t_dim->max is the square
     * maximum of the block's largest transform (1..4). */
    max2 = stbv_av1_tx_dims[max_tx].max;
    if (max2 <= STBV_AV1_TX_4X4)
        return STBV_AV1_TX_4X4;
    if (tctx < 0) tctx = 0;
    if (tctx > 2) tctx = 2;

    n = max2 < STBV_AV1_TX_16X16 ? max2 : 2;
    depth = stb_av1_msac_symbol(msac,
             &cdf->txsz[(max2 - 1) * 12 + tctx * 4],
             (size_t)n);

    while (depth-- > 0 && max_tx > STBV_AV1_TX_4X4)
        max_tx = stbv_av1_tx_dims[max_tx].sub;
    return max_tx;
}

/*
 * Decode one variable-transform partition decision.  This is the direct
 * scalar equivalent of dav1d's read_tx_tree() decision at one node.
 *
 * cat follows dav1d exactly:
 *     2 * (TX_64X64 - t_dim->max) - depth
 *
 * txpart contains [cat][above_smaller + left_smaller], two CDFs per cat.
 */
static int stbv_av1_decode_tx_split(struct stb_av1_msac *msac,
                                    stbv_av1_cdf *cdf,
                                    int tx,
                                    int depth,
                                    int above_smaller,
                                    int left_smaller)
{
    int cat;
    int ctx;

    if (depth >= 2 || tx <= STBV_AV1_TX_4X4)
        return 0;

    cat = 2 * (STBV_AV1_TX_64X64 - tx) - depth;
    if (cat < 0) cat = 0;
    if (cat > 2) cat = 2;

    ctx = (above_smaller ? 1 : 0) + (left_smaller ? 1 : 0);
    return (int)stb_av1_msac_bool_adapt(msac,
                &cdf->txpart[(cat * 3 + ctx) * 2]);
}

/*
 * Select the leaf transform type for an intra block.
 *
 * y_mode_nofilt is the ordinary directional/DC intra mode (FILTER_PRED has
 * already been converted to its underlying directional mode by the caller).
 * reduced_txtp_set selects dav1d's four-entry Intra2 set.
 *
 * The caller is responsible for dav1d's gating: no symbol is coded when the
 * block is lossless, when t_dim->max + 1 >= TX_64X64, when qidx == 0, or for
 * chroma planes (txtp is derived via stbv_av1_txtp_from_uvmode).
 */
static int stbv_av1_decode_intra_txtp(struct stb_av1_msac *msac,
                                      stbv_av1_cdf *cdf,
                                      int tx_min,
                                      int y_mode_nofilt,
                                      int reduced_txtp_set)
{
    unsigned int idx;
    int min2;

    if (y_mode_nofilt < 0 || y_mode_nofilt > 12) y_mode_nofilt = 0; /* DC */
    if (reduced_txtp_set || tx_min == STBV_AV1_TX_16X16) {
        /* txtp_intra2[min][y_mode], min in 0..2 */
        min2 = tx_min > 2 ? 2 : (tx_min < 0 ? 0 : tx_min);
        idx = stb_av1_msac_symbol(msac,
              &cdf->txtp_intra2[min2 * 104 + y_mode_nofilt * 8], 4);
        if (idx < 5)
            return stbv_av1_tx_set_intra2[idx];
        return STBV_AV1_TX_DCT_DCT;
    }

    /* txtp_intra1[min][y_mode], min in 0..1 */
    min2 = tx_min > 1 ? 1 : (tx_min < 0 ? 0 : tx_min);
    idx = stb_av1_msac_symbol(msac,
          &cdf->txtp_intra1[min2 * 104 + y_mode_nofilt * 8], 6);
    if (idx < 7)
        return stbv_av1_tx_set_intra1[idx];
    return STBV_AV1_TX_DCT_DCT;
}

/*
 * Decode the chroma txtp for inter blocks (including IBC).
 * uvt_dim is the chroma transform dimension; ytxtp is the already-decoded
 * luma txtp.  No MSAC consumption.
 */
static int stbv_av1_get_uv_inter_txtp(int uvt_dim_min, int uvt_dim_max,
                                      int ytxtp)
{
    if (uvt_dim_max == 3) /* TX_32X32 */
        return ytxtp == STBV_AV1_TX_IDTX ? STBV_AV1_TX_IDTX
                                          : STBV_AV1_TX_DCT_DCT;
    if (uvt_dim_min == 2) { /* TX_16X16 */
        /* H_FLIPADST=15, V_FLIPADST=14, H_ADST=13, V_ADST=12 */
        if ((1 << ytxtp) & ((1 << 15) | (1 << 14) | (1 << 13) | (1 << 12)))
            return STBV_AV1_TX_DCT_DCT;
    }
    return ytxtp;
}

/*
 * Decode the transform type for an inter block (including IBC).
 *
 * t_dim_min is the smaller dimension of the luma transform.
 * t_dim_max is the larger dimension.
 * reduced_txtp_set selects the restricted inter set.
 *
 * Returns the decoded transform type.  The caller must NOT be in an intra
 * block (use stbv_av1_decode_intra_txtp for intra).
 */
static int stbv_av1_decode_inter_txtp(struct stb_av1_msac *msac,
                                      stbv_av1_cdf *cdf,
                                      int t_dim_min, int t_dim_max,
                                      int reduced_txtp_set)
{
    unsigned int idx;

    if (reduced_txtp_set || t_dim_max == 3 /* TX_32X32 */) {
        /* txtp_inter3[t_dim_min]: single bool CDF, DCT_DCT vs IDTX */
        int min2 = t_dim_min > 3 ? 3 : (t_dim_min < 0 ? 0 : t_dim_min);
        idx = stb_av1_msac_bool_adapt(msac,
                  &cdf->txtp_inter3[min2 * 2]);
        return stbv_av1_tx_set_inter3[idx];
    } else if (t_dim_min == 2 /* TX_16X16 */) {
        idx = stb_av1_msac_symbol(msac, cdf->txtp_inter2, 10);
        return stbv_av1_tx_set_inter2[idx < 11 ? idx : 0];
    } else {
        /* t_dim_min is 0 or 1 */
        int min2 = t_dim_min > 1 ? 1 : (t_dim_min < 0 ? 0 : t_dim_min);
        idx = stb_av1_msac_symbol(msac, cdf->txtp_inter1[min2], 14);
        return stbv_av1_tx_set_inter1[idx < 15 ? idx : 0];
    }
}

#endif /* STB_AV1_TX_H */
