/*
 * stb_av1_intra.h - scalar AV1 intra-mode syntax decoder
 *
 * Intra syntax adapted from dav1d 1.5.4 src/decode.c.
 * BSD-2-Clause; see dav1d COPYING for attribution/license details.
 */
#ifndef STB_AV1_INTRA_H
#define STB_AV1_INTRA_H

#ifndef STB_AV1_MSAC_H
#error "include stb_av1_msac.h first"
#endif
#ifndef STB_AV1_CDF_H
#error "include stb_av1_cdf.h first"
#endif

#define STBV_AV1_INTRA_DC          0
#define STBV_AV1_INTRA_VERT        1
#define STBV_AV1_INTRA_HOR         2
#define STBV_AV1_INTRA_DDL         3
#define STBV_AV1_INTRA_DDR         4
#define STBV_AV1_INTRA_VR          5
#define STBV_AV1_INTRA_HD          6
#define STBV_AV1_INTRA_HU          7
#define STBV_AV1_INTRA_VL          8
#define STBV_AV1_INTRA_SMOOTH      9
#define STBV_AV1_INTRA_SMOOTH_V   10
#define STBV_AV1_INTRA_SMOOTH_H   11
#define STBV_AV1_INTRA_PAETH      12
#define STBV_AV1_INTRA_CFL        13

/* dav1d_intra_mode_context[], in the same order as N_INTRA_PRED_MODES. */
static const stbv_u8 stbv_av1_intra_mode_ctx[13] = {
    0, 1, 2, 3, 4, 4, 4, 4, 3, 0, 1, 2, 0
};

struct stb_av1_intra_block {
    int y_mode;
    int y_angle;
    int uv_mode;
    int uv_angle;
    int cfl_alpha_u;
    int cfl_alpha_v;
};

static int stbv_av1_decode_intra_mode(struct stb_av1_msac *msac,
                                      stbv_av1_cdf *cdf,
                                      int above_mode, int left_mode,
                                      int cbw4, int cbh4,
                                      int cfl_allowed,
                                      struct stb_av1_intra_block *b)
{
    int ac, lc, mode;
    stbv_u16 *ycdf;
    stbv_u16 *uvcdf;
    unsigned sym;
    int sign, sign_u, sign_v, ctx;

    if (!b) return -1;
    if (above_mode < 0 || above_mode > 12) above_mode = STBV_AV1_INTRA_DC;
    if (left_mode < 0 || left_mode > 12) left_mode = STBV_AV1_INTRA_DC;

    ac = stbv_av1_intra_mode_ctx[above_mode];
    lc = stbv_av1_intra_mode_ctx[left_mode];
    ycdf = cdf->kfym + (ac * 5 + lc) * 16;
    sym = stb_av1_msac_symbol(msac, ycdf, 12);
    if (sym > 12) return -2;
    mode = (int)sym;
    b->y_mode = mode;
    b->y_angle = 0;
    b->uv_mode = STBV_AV1_INTRA_DC;
    b->uv_angle = 0;
    b->cfl_alpha_u = 0;
    b->cfl_alpha_v = 0;

    if (cbw4 + cbh4 >= 2 && mode >= STBV_AV1_INTRA_VERT &&
        mode <= STBV_AV1_INTRA_VL) {
        sym = stb_av1_msac_symbol(msac,
                                  cdf->angle_delta + (mode - STBV_AV1_INTRA_VERT) * 8,
                                  6);
        b->y_angle = (int)sym - 3;
    }

    uvcdf = cdf->uv_mode + ((cfl_allowed ? 1 : 0) * 13 + mode) * 16;
    sym = stb_av1_msac_symbol(msac, uvcdf, cfl_allowed ? 13 : 12);
    if (sym > (unsigned)(cfl_allowed ? 13 : 12)) return -3;
    b->uv_mode = (int)sym;

    if (b->uv_mode == STBV_AV1_INTRA_CFL) {
        sym = stb_av1_msac_symbol(msac, cdf->cfl_sign, 7);
        sign = (int)sym + 1;
        sign_u = sign / 3;
        sign_v = sign - sign_u * 3;

        if (sign_u) {
            ctx = (sign_u == 2) * 3 + sign_v;
            sym = stb_av1_msac_symbol(msac, cdf->cfl_alpha + ctx * 16, 15);
            b->cfl_alpha_u = (int)sym + 1;
            if (sign_u == 1) b->cfl_alpha_u = -b->cfl_alpha_u;
        }
        if (sign_v) {
            ctx = (sign_v == 2) * 3 + sign_u;
            sym = stb_av1_msac_symbol(msac, cdf->cfl_alpha + ctx * 16, 15);
            b->cfl_alpha_v = (int)sym + 1;
            if (sign_v == 1) b->cfl_alpha_v = -b->cfl_alpha_v;
        }
    } else if (cbw4 + cbh4 >= 2 && b->uv_mode >= STBV_AV1_INTRA_VERT &&
               b->uv_mode <= STBV_AV1_INTRA_VL) {
        sym = stb_av1_msac_symbol(msac,
                                  cdf->angle_delta + (b->uv_mode - STBV_AV1_INTRA_VERT) * 8,
                                  6);
        b->uv_angle = (int)sym - 3;
    }

    return 0;
}

#endif
