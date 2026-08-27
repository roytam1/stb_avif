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
    memset(td, 0, sizeof(*td)); td->seq = seq; td->frame = frame;
    td->tile_col = tile_col; td->tile_row = tile_row;
    qcat = (frame->quant.yac > 20) + (frame->quant.yac > 60) + (frame->quant.yac > 120);
    stbv_av1_cdf_init(&td->cdf, (unsigned)qcat);
    stb_av1_msac_init(&td->msac, data, size, (int)frame->disable_cdf_update);
    sb_log2 = 6U + seq->sb128; sb_size = 1U << sb_log2;
    sx0 = frame->tiling.col_start_sb[tile_col]; sx1 = frame->tiling.col_start_sb[tile_col + 1];
    sy0 = frame->tiling.row_start_sb[tile_row]; sy1 = frame->tiling.row_start_sb[tile_row + 1];
    if (sx1 <= sx0 || sy1 <= sy0) return -5;
    tile_x4 = sx0 * (sb_size >> 2); tile_y4 = sy0 * (sb_size >> 2);
    tile_w4 = (sx1 - sx0) * (sb_size >> 2); tile_h4 = (sy1 - sy0) * (sb_size >> 2);
    pd.msac = &td->msac; pd.cdf = &td->cdf;
    pd.frame_w4 = (int)(tile_x4 + tile_w4); pd.frame_h4 = (int)(tile_y4 + tile_h4);
    pd.ctx_x4 = (int)tile_x4; pd.ctx_y4 = (int)tile_y4;
    above_n = (int)(((tile_w4 + 15U) >> 1) + 1U); left_n = (int)(((tile_h4 + 15U) >> 1) + 1U);
    above = (stbv_u8 *)malloc((size_t)above_n); left = (stbv_u8 *)malloc((size_t)left_n);
    if (!above || !left) { if (above) free(above); if (left) free(left); return -6; }
    memset(above, 0, (size_t)above_n); memset(left, 0, (size_t)left_n);
    pd.above = above; pd.left = left; pd.above_n = above_n; pd.left_n = left_n;
    w.td = td; w.cb = cb; w.opaque = opaque; pd.leaf = stb_av1_tile_leaf_dispatch; pd.opaque = &w;
    { unsigned int sy;
      for (sy = sy0; sy < sy1; sy++) { unsigned int sx;
        memset(left, 0, (size_t)left_n); if (sy == sy0) memset(above, 0, (size_t)above_n);
        if (row_cb) row_cb(opaque);
        for (sx = sx0; sx < sx1; sx++) {
          int bl = seq->sb128 ? STBV_AV1_BL_128X128 : STBV_AV1_BL_64X64;
          int bx = (int)(sx * (sb_size >> 2)); int by = (int)(sy * (sb_size >> 2));
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
