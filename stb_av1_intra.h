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
#define STBV_AV1_INTRA_FILTER     14

/* Note: dav1d_filter_mode_to_y_mode is defined in stb_av1_leaf.h as
 * stb_filter_mode_to_y_mode (the version actually used during decode). */

/* dav1d_cfl_allowed_mask: cfl is allowed for blocks no larger than 32x32
 * (bits for BS_32x32 .. BS_4x4, dav1d tables.h). */
/* dav1d_cfl_allowed_mask: cfl allowed for blocks <= 32x32 except rect
 * 16x64/64x16/32x64/64x32 etc: bits {BS_32x32,32x16,32x8,16x32,16x16,
 * 16x8,16x4,8x32,8x16,8x8,8x4,4x16,4x8,4x4} == {7,8,9,11..21}. */
#define STBV_AV1_CFL_ALLOWED_MASK 0x3FFB80u

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
                                      int has_chroma,
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
#ifdef STB_AV1_MSB_DEBUG
    {
        extern FILE *_msac_dbg_fp;
        extern int _msac_dbg_bx4, _msac_dbg_by4;
        if (_msac_dbg_fp) {
            fprintf(_msac_dbg_fp, "  YMODE_CDF bx4=%d by4=%d above=%d left=%d ac=%d lc=%d cdf[0..3]=%d,%d,%d,%d cdf[4..7]=%d,%d,%d,%d cdf[8..11]=%d,%d,%d,%d cdf_cnt=%d rng=%u cnt=%d\n",
                    _msac_dbg_bx4, _msac_dbg_by4,
                    above_mode, left_mode, ac, lc,
                    (int)ycdf[0], (int)ycdf[1], (int)ycdf[2], (int)ycdf[3],
                    (int)ycdf[4], (int)ycdf[5], (int)ycdf[6], (int)ycdf[7],
                    (int)ycdf[8], (int)ycdf[9], (int)ycdf[10], (int)ycdf[11],
                    (int)ycdf[12],
                    msac->rng, msac->cnt);
            fflush(_msac_dbg_fp);
        }
    }
#endif
    sym = stb_av1_msac_symbol(msac, ycdf, 12);
    if (sym > 12) return -2;
    mode = (int)sym;
#ifdef STB_AV1_MSB_DEBUG
    {
        extern FILE *_msac_dbg_fp;
        extern int _msac_dbg_bx4, _msac_dbg_by4;
        if (_msac_dbg_fp) {
            fprintf(_msac_dbg_fp, "  AFTER_YMODE bx4=%d by4=%d ymode=%d rng=%u cnt=%d dif=%llu\n",
                    _msac_dbg_bx4, _msac_dbg_by4, mode, msac->rng, msac->cnt, (unsigned long long)msac->dif);
            fflush(_msac_dbg_fp);
        }
    }
#endif
    b->y_mode = mode;
    b->y_angle = 0;
    b->uv_mode = STBV_AV1_INTRA_DC;
    b->uv_angle = 0;
    b->cfl_alpha_u = 0;
    b->cfl_alpha_v = 0;

    /* dav1d uses b_dim[2] + b_dim[3] >= 2 which equals log2(bw4)+log2(bh4)>=2 */
    if (cbw4 * cbh4 >= 4 && mode >= STBV_AV1_INTRA_VERT &&
        mode <= STBV_AV1_INTRA_VL) {
        sym = stb_av1_msac_symbol(msac,
                                  cdf->angle_delta + (mode - STBV_AV1_INTRA_VERT) * 8,
                                  6);
        b->y_angle = (int)sym - 3;
    }

    /* Chroma syntax: only decoded when the block covers chroma samples.
     * For subsampled formats with small blocks at even positions,
     * has_chroma is false and NO chroma symbols are consumed. */
    if (!has_chroma)
        return 0;

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
    } else if (cbw4 * cbh4 >= 4 && b->uv_mode >= STBV_AV1_INTRA_VERT &&
               b->uv_mode <= STBV_AV1_INTRA_VL) {
        sym = stb_av1_msac_symbol(msac,
                                  cdf->angle_delta + (b->uv_mode - STBV_AV1_INTRA_VERT) * 8,
                                  6);
        b->uv_angle = (int)sym - 3;
    }

    return 0;
}

#endif
