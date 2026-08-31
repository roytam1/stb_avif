/*
 * stb_av1_tile_decode.h - first real scalar AV1 tile walker
 *
 * This is the first integration point between the OBU/tile plumbing and the
 * MSAC/CDF partition decoder.  It deliberately stops at partition leaves:
 * leaf syntax is kept separate until the neighbor/context state is wired in.
 */
#ifndef STB_AV1_TILE_DECODE_H
#define STB_AV1_TILE_DECODE_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef STB_AV1_PARTITION_DECODE_H
#error "include stb_av1_partition_decode.h first"
#endif

/* Restoration types (AV1 spec Table 6.16) */
#define STBV_AV1_RESTORATION_NONE       0
#define STBV_AV1_RESTORATION_SWITCHABLE 1
#define STBV_AV1_RESTORATION_WIENER     2
#define STBV_AV1_RESTORATION_SGRPROJ    3

static const unsigned short stbv_av1_sgr_params[16][2] = {
    { 140, 3236 }, { 112, 2158 }, {  93, 1618 }, {  80, 1438 },
    {  70, 1295 }, {  58, 1177 }, {  47, 1079 }, {  37,  996 },
    {  30,  925 }, {  25,  863 }, {   0, 2589 }, {   0, 1618 },
    {   0, 1177 }, {   0,  925 }, {  56,    0 }, {  22,    0 },
};

/* Per-plane restoration reference state */
typedef struct stbv_av1_lr_ref {
    int filter_v[3];  /* 0=offset, 1=sharp, 2=dense */
    int filter_h[3];
    int sgr_weights[2];
    int type_val;     /* decoded restoration type for this unit */
} stbv_av1_lr_ref;

static void stb_av1_read_restoration_info(struct stb_av1_msac *msac,
                                           struct stbv_av1_cdf *cdf,
                                           stbv_av1_lr_ref *lr_ref,
                                           int plane,
                                           unsigned int frame_type)
{
    int lr_type;

    if (frame_type == STBV_AV1_RESTORATION_SWITCHABLE) {
        int filter = (int)stb_av1_msac_symbol(msac, cdf->restore_switchable, 2);
        lr_type = filter + (filter != 0); /* NONE=0, WIENER=2, SGRPROJ=3 */
    } else {
        stbv_u16 *cdf_ptr = frame_type == STBV_AV1_RESTORATION_WIENER ?
            cdf->restore_wiener : cdf->restore_sgrproj;
        int has_filter;
        has_filter = (int)stb_av1_msac_bool_adapt(msac, cdf_ptr);
        lr_type = has_filter ? (int)frame_type : STBV_AV1_RESTORATION_NONE;
    }

    if (lr_type == STBV_AV1_RESTORATION_WIENER) {
        lr_ref->filter_v[0] = plane ? 0 :
            stb_av1_msac_subexp(msac, lr_ref->filter_v[0] + 5, 16, 1) - 5;
        lr_ref->filter_v[1] =
            stb_av1_msac_subexp(msac, lr_ref->filter_v[1] + 23, 32, 2) - 23;
        lr_ref->filter_v[2] =
            stb_av1_msac_subexp(msac, lr_ref->filter_v[2] + 17, 64, 3) - 17;
        lr_ref->filter_h[0] = plane ? 0 :
            stb_av1_msac_subexp(msac, lr_ref->filter_h[0] + 5, 16, 1) - 5;
        lr_ref->filter_h[1] =
            stb_av1_msac_subexp(msac, lr_ref->filter_h[1] + 23, 32, 2) - 23;
        lr_ref->filter_h[2] =
            stb_av1_msac_subexp(msac, lr_ref->filter_h[2] + 17, 64, 3) - 17;
        /* Wiener: copy sgr_weights from ref for delta coding */
        lr_ref->sgr_weights[0] = lr_ref->sgr_weights[0];
        lr_ref->sgr_weights[1] = lr_ref->sgr_weights[1];
    } else if (lr_type == STBV_AV1_RESTORATION_SGRPROJ) {
        unsigned int idx = stb_av1_msac_bools(msac, 4);
        int w0, w1;
        /* SGR index encoded into type (matches dav1d: lr->type += idx) */
        lr_type += (int)idx;
        w0 = stbv_av1_sgr_params[idx][0] ?
            stb_av1_msac_subexp(msac, lr_ref->sgr_weights[0] + 96, 128, 4) - 96 : 0;
        w1 = stbv_av1_sgr_params[idx][1] ?
            stb_av1_msac_subexp(msac, lr_ref->sgr_weights[1] + 32, 128, 4) - 32 : 95;
        lr_ref->sgr_weights[0] = w0;
        lr_ref->sgr_weights[1] = w1;
        /* SGR: copy wiener filters from ref for delta coding */
        lr_ref->filter_v[0] = lr_ref->filter_v[0];
        lr_ref->filter_v[1] = lr_ref->filter_v[1];
        lr_ref->filter_v[2] = lr_ref->filter_v[2];
        lr_ref->filter_h[0] = lr_ref->filter_h[0];
        lr_ref->filter_h[1] = lr_ref->filter_h[1];
        lr_ref->filter_h[2] = lr_ref->filter_h[2];
    }
    lr_ref->type_val = lr_type;
}

/* Per-SB restoration unit storage (for LR filter application after decode) */
typedef struct stbv_av1_lr_unit {
    unsigned char type;          /* STBV_AV1_RESTORATION_NONE/WIENER/SGRPROJ */
    signed char filter_h[3];     /* Wiener horizontal */
    signed char filter_v[3];     /* Wiener vertical */
    signed char sgr_weights[2];  /* SGR weights */
    unsigned char sgr_idx;       /* SGR index into params table */
} stbv_av1_lr_unit;

typedef struct stbv_av1_lr_mask {
    stbv_av1_lr_unit *units[3]; /* per-plane flat array of LR units */
    int grid_stride[3];         /* stride in LR units per plane */
    int grid_rows[3];           /* rows in LR units per plane */
    int unit_size_log2[2];      /* [0]=luma, [1]=chroma */
} stbv_av1_lr_mask;

struct stb_av1_tile_decoder {
    struct stb_av1_msac msac;
    stbv_av1_cdf cdf;
    const struct stb_av1_seqhdr *seq;
    const struct stb_av1_framehdr *frame;
    unsigned int tile_col;
    unsigned int tile_row;
    unsigned int tile_w4;
    unsigned int tile_h4;
    unsigned int leaves;
    int error;
    /* LR mask for storing decoded restoration params (owned by caller) */
    stbv_av1_lr_mask *lr_mask;
};

struct stb_av1_tile_leaf_info {
    int bl;
    int bs;
    int bp;
    int bx;
    int by;
};

typedef int (*stb_av1_tile_leaf_cb)(struct stb_av1_tile_decoder *td,
                                    const struct stb_av1_tile_leaf_info *li,
                                    void *opaque);

struct stb_av1_tile_walk_ctx {
    struct stb_av1_tile_decoder *td;
    stb_av1_tile_leaf_cb cb;
    void *opaque;
};

typedef void (*stb_av1_tile_row_cb)(void *opaque);

static int stb_av1_tile_leaf_dispatch(stbv_av1_partition_decoder *pd,
                                       int bl, int bs, int bp,
                                       int bx, int by, void *opaque)
{
    struct stb_av1_tile_walk_ctx *w = (struct stb_av1_tile_walk_ctx *)opaque;
    struct stb_av1_tile_leaf_info li;
    li.bl = bl;
    li.bs = bs;
    li.bp = bp;
    li.bx = bx;
    li.by = by;
    w->td->leaves++;
    if (w->cb) {
        int rc = w->cb(w->td, &li, w->opaque);
        if (rc) w->td->error = rc;
        /* Continue decoding even on leaf errors: dav1d skips the block
         * and keeps the MSAC state for subsequent blocks. */
        return 0;
    }
    return 0;
}

/*
 * Decode a single tile. Coordinates in the partition decoder are 4x4 units.
 * The initial implementation supports tile 0 only; the frame-header tiling
 * parser still exposes the exact tile geometry so this function can be
 * extended without changing the MSAC interface.
 */
static int stb_av1_decode_tile_at(struct stb_av1_tile_decoder *td,
                                  const struct stb_av1_seqhdr *seq,
                                  const struct stb_av1_framehdr *frame,
                                  const stbv_u8 *data, size_t size,
                                  unsigned int tile_col, unsigned int tile_row,
                                  stb_av1_tile_leaf_cb cb, void *opaque,
                                  stb_av1_tile_row_cb row_cb)
{
    stbv_av1_partition_decoder pd; struct stb_av1_tile_walk_ctx w;
    unsigned int sb_log2, sb_size, sx0, sy0, sx1, sy1;
    unsigned int tile_x4, tile_y4, tile_w4, tile_h4;
    int qcat, above_n, left_n; stbv_u8 *above, *left; int rc = 0;
    if (!td || !seq || !frame || !data || !size) return -1;
    if (frame->frame_type != STB_AV1_FRAME_KEY && frame->frame_type != STB_AV1_FRAME_INTRA_ONLY) return -2;
    if (tile_col >= frame->tiling.cols || tile_row >= frame->tiling.rows) return -3;
    if (frame->superres_enabled) return -4;
    {
        stbv_av1_lr_mask *saved_lr_mask = td->lr_mask;
        memset(td, 0, sizeof(*td));
        td->lr_mask = saved_lr_mask;
    }
    td->seq = seq; td->frame = frame;
    td->tile_col = tile_col; td->tile_row = tile_row;
    qcat = (frame->quant.yac > 20) + (frame->quant.yac > 60) + (frame->quant.yac > 120);
    stbv_av1_cdf_init(&td->cdf, (unsigned)qcat);
    stb_av1_msac_init(&td->msac, data, size, (int)frame->disable_cdf_update);
    fprintf(stderr, "DBG msac_init: data0..7=%02x %02x %02x %02x %02x %02x %02x %02x size=%zu cnt=%d rng=%u\n",
            data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
            size, td->msac.cnt, td->msac.rng);
    sb_log2 = 6U + seq->sb128; sb_size = 1U << sb_log2;
    sx0 = frame->tiling.col_start_sb[tile_col]; sx1 = frame->tiling.col_start_sb[tile_col + 1];
    sy0 = frame->tiling.row_start_sb[tile_row]; sy1 = frame->tiling.row_start_sb[tile_row + 1];
    if (sx1 <= sx0 || sy1 <= sy0) return -5;
    tile_x4 = sx0 * (sb_size >> 2); tile_y4 = sy0 * (sb_size >> 2);
    tile_w4 = (sx1 - sx0) * (sb_size >> 2); tile_h4 = (sy1 - sy0) * (sb_size >> 2);
    pd.msac = &td->msac; pd.cdf = &td->cdf;
    /* dav1d uses the full frame dimensions (f->bw, f->bh) for partition
       split decisions, NOT tile-local dimensions.  Using tile-local sizes
       causes wrong splitting at the right/bottom edge of the frame. */
     pd.frame_w4 = (int)(((frame->width[0] + 7U) >> 3) << 1);
     pd.frame_h4 = (int)(((frame->height + 7U) >> 3) << 1);
    pd.ctx_x4 = (int)tile_x4; pd.ctx_y4 = (int)tile_y4;
    above_n = (int)(((tile_w4 + 15U) >> 1) + 1U); left_n = (int)(((tile_h4 + 15U) >> 1) + 1U);
    above = (stbv_u8 *)malloc((size_t)above_n); left = (stbv_u8 *)malloc((size_t)left_n);
    if (!above || !left) { if (above) free(above); if (left) free(left); return -6; }
    memset(above, 0, (size_t)above_n); memset(left, 0, (size_t)left_n);
    pd.above = above; pd.left = left; pd.above_n = above_n; pd.left_n = left_n;
    w.td = td; w.cb = cb; w.opaque = opaque; pd.leaf = stb_av1_tile_leaf_dispatch; pd.opaque = &w;
    /* Loop restoration info: per-SB MSAC reads before partition decode.
       dav1d reads these in the tile superblock row loop before decode_sb(). */
    {
        unsigned int restore_planes = 0;
        unsigned int lr_unit_size_log2[2] = {0, 0};
        stbv_av1_lr_ref lr_ref[3];
        unsigned int sy;
        int p;

        if (seq->restoration && !frame->allow_intrabc) {
            if (frame->restoration.type[0] != STBV_AV1_RESTORATION_NONE)
                restore_planes |= 1U;
            if (!seq->monochrome && frame->restoration.type[1] != STBV_AV1_RESTORATION_NONE)
                restore_planes |= 2U;
            if (!seq->monochrome && frame->restoration.type[2] != STBV_AV1_RESTORATION_NONE)
                restore_planes |= 4U;
            lr_unit_size_log2[0] = frame->restoration.unit_size[0];
            lr_unit_size_log2[1] = frame->restoration.unit_size[1];
        }
        /* LR reference defaults: set once per tile (dav1d does this in
         * setup_tile, NOT per row). The subexponential delta coding in
         * read_restoration_info uses lr_ref as the reference for the next
         * SB's LR params. Resetting per row would lose the carryover from
         * the previous row, causing wrong LR filter values and MSAC desync. */
        for (p = 0; p < 3; p++) {
            lr_ref[p].filter_v[0] = 3; lr_ref[p].filter_v[1] = -7; lr_ref[p].filter_v[2] = 15;
            lr_ref[p].filter_h[0] = 3; lr_ref[p].filter_h[1] = -7; lr_ref[p].filter_h[2] = 15;
            lr_ref[p].sgr_weights[0] = -32; lr_ref[p].sgr_weights[1] = 31;
        }
        for (sy = sy0; sy < sy1; sy++) { unsigned int sx;
          memset(left, 0, (size_t)left_n); if (sy == sy0) memset(above, 0, (size_t)above_n);
          if (row_cb) row_cb(opaque);
          for (sx = sx0; sx < sx1; sx++) {
            int bl = seq->sb128 ? STBV_AV1_BL_128X128 : STBV_AV1_BL_64X64;
            int bx = (int)(sx * (sb_size >> 2)); int by = (int)(sy * (sb_size >> 2));

            if (restore_planes) {
                for (p = 0; p < 3; p++) {
                    int ss_ver, ss_hor, unit_size_log2, unit_size, mask, half_unit;
                    int y, x, w_px, h_px;
                    if (!((restore_planes >> p) & 1U))
                        continue;
                    ss_ver = (p != 0) ? (int)(seq->ss_ver != 0) : 0;
                    ss_hor = (p != 0) ? (int)(seq->ss_hor != 0) : 0;
                    unit_size_log2 = (int)lr_unit_size_log2[p > 0 ? 1 : 0];
                    unit_size = 1 << unit_size_log2;
                    mask = unit_size - 1;
                    half_unit = unit_size >> 1;
                    y = by * 4 >> ss_ver;
                    h_px = ((int)frame->height + ss_ver) >> ss_ver;
                    x = bx * 4 >> ss_hor;
                    w_px = ((int)frame->width[0] + ss_hor) >> ss_hor;
                    if (y & mask) continue;
                    if (y && y + half_unit > h_px) continue;
                    if (x & mask) continue;
                    if (x && x + half_unit > w_px) continue;
                    stb_av1_read_restoration_info(&td->msac, &td->cdf,
                                                   &lr_ref[p], p,
                                                   frame->restoration.type[p]);
                    /* Store decoded LR params into the frame-level mask */
                    if (td->lr_mask) {
                        int lr_x = x >> unit_size_log2;
                        int lr_y = y >> unit_size_log2;
                        int gw = td->lr_mask->grid_stride[p];
                        int gr = td->lr_mask->grid_rows[p];
                        if (lr_x >= 0 && lr_x < gw && lr_y >= 0 && lr_y < gr) {
                            stbv_av1_lr_unit *u = &td->lr_mask->units[p][lr_y * gw + lr_x];
                            u->type = (stbv_u8)lr_ref[p].type_val;
                            u->filter_h[0] = (signed char)lr_ref[p].filter_h[0];
                            u->filter_h[1] = (signed char)lr_ref[p].filter_h[1];
                            u->filter_h[2] = (signed char)lr_ref[p].filter_h[2];
                            u->filter_v[0] = (signed char)lr_ref[p].filter_v[0];
                            u->filter_v[1] = (signed char)lr_ref[p].filter_v[1];
                            u->filter_v[2] = (signed char)lr_ref[p].filter_v[2];
                            u->sgr_weights[0] = (signed char)lr_ref[p].sgr_weights[0];
                            u->sgr_weights[1] = (signed char)lr_ref[p].sgr_weights[1];
                            u->sgr_idx = (u->type >= STBV_AV1_RESTORATION_SGRPROJ) ?
                                (stbv_u8)(u->type - STBV_AV1_RESTORATION_SGRPROJ) : 0;
                        }
                    }
                }
            }
            if (stbv_av1_partition_decode_sb(&pd, bl, bx, by)) { td->error = 1; rc = -1; goto done; }
          }
        }
    }
    rc = td->msac.buf_pos <= td->msac.buf_end ? 0 : -1;
done:
    free(above); free(left); return rc;
}

static int stb_av1_decode_tile(struct stb_av1_tile_decoder *td,
                               const struct stb_av1_seqhdr *seq,
                               const struct stb_av1_framehdr *frame,
                               const stbv_u8 *data, size_t size,
                               stb_av1_tile_leaf_cb cb, void *opaque,
                               stb_av1_tile_row_cb row_cb)
{
    return stb_av1_decode_tile_at(td, seq, frame, data, size, 0, 0, cb, opaque, row_cb);
}

#endif
