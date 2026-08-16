/*
 * stb_av1_intra.h - scalar AV1 intra block syntax
 *
 * Portions are derived from dav1d 1.5.4 src/decode.c and src/tables.c.
 * Copyright (C) 2018-2021, VideoLAN and dav1d authors
 * Copyright (C) 2018, Two Orioles, LLC
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef STB_AV1_INTRA_H
#define STB_AV1_INTRA_H

#include "stb_av1_msac.h"
#include "stb_av1_cdf.h"

#define STBV_AV1_DC_PRED              0
#define STBV_AV1_VERT_PRED            1
#define STBV_AV1_HOR_PRED             2
#define STBV_AV1_DIAG_DOWN_LEFT_PRED  3
#define STBV_AV1_DIAG_DOWN_RIGHT_PRED 4
#define STBV_AV1_VERT_RIGHT_PRED      5
#define STBV_AV1_HOR_DOWN_PRED        6
#define STBV_AV1_HOR_UP_PRED          7
#define STBV_AV1_VERT_LEFT_PRED       8
#define STBV_AV1_SMOOTH_PRED          9
#define STBV_AV1_SMOOTH_V_PRED        10
#define STBV_AV1_SMOOTH_H_PRED        11
#define STBV_AV1_PAETH_PRED            12
#define STBV_AV1_N_INTRA_PRED_MODES   13
#define STBV_AV1_CFL_PRED              13
#define STBV_AV1_N_UV_INTRA_PRED_MODES 14
#define STBV_AV1_FILTER_PRED           13

/* Map AV1 block dimensions to dav1d's y_mode size context. */
static int stb_av1_ymode_size_ctx(int w, int h)
{
    int bw, bh;
    bw = 0;
    bh = 0;
    while (w > 4) { w >>= 1; bw++; }
    while (h > 4) { h >>= 1; bh++; }

    if (bw >= 3 || bh >= 3) return 3;
    if (bw >= 2 || bh >= 2) return 2;
    if (bw >= 1 || bh >= 1) return 1;
    return 0;
}

static int stb_av1_intra_mode_ctx(int mode)
{
    switch (mode) {
    case STBV_AV1_DC_PRED:              return 0;
    case STBV_AV1_VERT_PRED:            return 1;
    case STBV_AV1_HOR_PRED:             return 2;
    case STBV_AV1_DIAG_DOWN_LEFT_PRED:  return 3;
    case STBV_AV1_VERT_LEFT_PRED:       return 3;
    case STBV_AV1_DIAG_DOWN_RIGHT_PRED: return 4;
    case STBV_AV1_VERT_RIGHT_PRED:      return 4;
    case STBV_AV1_HOR_DOWN_PRED:        return 4;
    case STBV_AV1_HOR_UP_PRED:          return 4;
    case STBV_AV1_SMOOTH_PRED:          return 0;
    case STBV_AV1_SMOOTH_V_PRED:        return 1;
    case STBV_AV1_SMOOTH_H_PRED:        return 2;
    case STBV_AV1_PAETH_PRED:           return 0;
    default:                            return 0;
    }
}

static int stb_av1_log2_dim4(int n)
{
    int v = 0;
    while (n > 4) { n >>= 1; v++; }
    return v;
}

/*
 * Decode the intra-specific syntax of one leaf block.
 *
 * above_mode and left_mode are the already reconstructed luma modes of the
 * neighbouring 4x4 locations.  The caller owns those context arrays.
 *
 * This intentionally omits palette/screen-content syntax in the first pass.
 * It also leaves transform-size selection to the following transform layer.
 */
typedef struct stb_av1_intra_block {
    int y_mode;
    int y_angle;
    int uv_mode;
    int uv_angle;
    int cfl_alpha_u;
    int cfl_alpha_v;
    int use_filter_intra;
    int filter_intra_mode;
    int tx;
    int uv_tx;
} stb_av1_intra_block;

static int stb_av1_decode_intra_block(
    struct stb_av1_msac *msac,
    stbv_av1_cdf *cdf,
    stb_av1_intra_block *b,
    int w, int h,
    int above_mode, int left_mode,
    int monochrome,
    int cfl_allowed,
    int filter_intra_enabled,
    int bs_index,
    int filter_intra_allowed)
{
    int ac;
    int ctx;
    int bdim_sum;
    int bdim_max;
    stbv_u16 *ymode_cdf;

    if (!msac || !cdf || !b)
        return 0;

    b->y_angle = 0;
    b->uv_angle = 0;
    b->cfl_alpha_u = 0;
    b->cfl_alpha_v = 0;
    b->use_filter_intra = 0;
    b->filter_intra_mode = 0;
    b->tx = 0;
    b->uv_tx = 0;

    /* Key/intra frame uses the 5x5 key-frame intra-mode CDF. */
    ctx = stb_av1_intra_mode_ctx(above_mode);
    ac = stb_av1_intra_mode_ctx(left_mode);
    ymode_cdf = cdf->kfym + (ctx * 5 + ac) * 16;
    b->y_mode = (int)stb_av1_msac_symbol(msac, ymode_cdf,
                                         STBV_AV1_N_INTRA_PRED_MODES - 1);

    bdim_sum = stb_av1_log2_dim4(w) + stb_av1_log2_dim4(h);
    bdim_max = stb_av1_log2_dim4(w);
    ac = stb_av1_log2_dim4(h);
    if (ac > bdim_max) bdim_max = ac;

    if (bdim_sum >= 2 && b->y_mode >= STBV_AV1_VERT_PRED &&
        b->y_mode <= STBV_AV1_VERT_LEFT_PRED) {
        stbv_u16 *angle_cdf = cdf->angle_delta +
                              (b->y_mode - STBV_AV1_VERT_PRED) * 8;
        b->y_angle = (int)stb_av1_msac_symbol(msac, angle_cdf, 6) - 3;
    }

    if (!monochrome) {
        stbv_u16 *uv_cdf = cdf->uv_mode +
            ((cfl_allowed ? 0 : 1) * STBV_AV1_N_INTRA_PRED_MODES +
             b->y_mode) * 16;
        int n_uv = STBV_AV1_N_UV_INTRA_PRED_MODES - 1 - !cfl_allowed;

        b->uv_mode = (int)stb_av1_msac_symbol(msac, uv_cdf, (size_t)n_uv);

        if (b->uv_mode == STBV_AV1_CFL_PRED) {
            int sign = (int)stb_av1_msac_symbol(msac, cdf->cfl_sign, 7) + 1;
            int sign_u = (sign * 0x56) >> 8;
            int sign_v = sign - sign_u * 3;
            int ca;

            if (sign_u) {
                ca = (sign_u == 2) * 3 + sign_v;
                b->cfl_alpha_u = (int)stb_av1_msac_symbol(
                    msac, cdf->cfl_alpha + ca * 16, 15) + 1;
                if (sign_u == 1) b->cfl_alpha_u = -b->cfl_alpha_u;
            }
            if (sign_v) {
                ca = (sign_v == 2) * 3 + sign_u;
                b->cfl_alpha_v = (int)stb_av1_msac_symbol(
                    msac, cdf->cfl_alpha + ca * 16, 15) + 1;
                if (sign_v == 1) b->cfl_alpha_v = -b->cfl_alpha_v;
            }
        } else if (bdim_sum >= 2 && b->uv_mode >= STBV_AV1_VERT_PRED &&
                   b->uv_mode <= STBV_AV1_VERT_LEFT_PRED) {
            stbv_u16 *angle_cdf = cdf->angle_delta +
                                  (b->uv_mode - STBV_AV1_VERT_PRED) * 8;
            b->uv_angle = (int)stb_av1_msac_symbol(msac, angle_cdf, 6) - 3;
        }
    } else {
        b->uv_mode = STBV_AV1_DC_PRED;
    }

    /* Filter intra is only considered for DC luma blocks of small size. */
    if (filter_intra_enabled && filter_intra_allowed &&
        b->y_mode == STBV_AV1_DC_PRED && bdim_max <= 3 && bdim_sum >= 2) {
        int use = (int)stb_av1_msac_bool_adapt(
            msac, cdf->use_filter_intra + bs_index * 1);
        if (use) {
            b->use_filter_intra = 1;
            b->y_mode = STBV_AV1_FILTER_PRED;
            b->filter_intra_mode =
                (int)stb_av1_msac_symbol(msac, cdf->filter_intra, 4);
        }
    }

    return 1;
}

#endif
