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
    if (w->cb)
        return w->cb(w->td, &li, w->opaque);
    return 0;
}

/*
 * Decode a single tile. Coordinates in the partition decoder are 4x4 units.
 * The initial implementation supports tile 0 only; the frame-header tiling
 * parser still exposes the exact tile geometry so this function can be
 * extended without changing the MSAC interface.
 */
static int stb_av1_decode_tile(struct stb_av1_tile_decoder *td,
                               const struct stb_av1_seqhdr *seq,
                               const struct stb_av1_framehdr *frame,
                               const stbv_u8 *data, size_t size,
                               stb_av1_tile_leaf_cb cb, void *opaque,
                               stb_av1_tile_row_cb row_cb)
{
    stbv_av1_partition_decoder pd;
    struct stb_av1_tile_walk_ctx w;
    unsigned int sb_log2;
    unsigned int sb_size;
    unsigned int sbw, sbh;
    int qcat;
    int above_n, left_n;
    stbv_u8 *above, *left;
    int rc = 0;

    if (!td || !seq || !frame || !data || !size)
        return -1;
    if (frame->frame_type != STB_AV1_FRAME_KEY &&
        frame->frame_type != STB_AV1_FRAME_INTRA_ONLY)
        return -2;
    if (frame->tiling.cols != 1 || frame->tiling.rows != 1)
        return -3;
    if (frame->superres_enabled)
        return -4;
    if (seq->monochrome)
        return -5;

    memset(td, 0, sizeof(*td));
    td->seq = seq;
    td->frame = frame;

    /* qcat is the coefficient-CDF category.  dav1d derives this from the
       frame quantizer with (qidx > 20) + (qidx > 60) + (qidx > 120). */
    qcat = (frame->quant.yac > 20) + (frame->quant.yac > 60) +
           (frame->quant.yac > 120);
    stbv_av1_cdf_init(&td->cdf, (unsigned)qcat);
    stb_av1_msac_init(&td->msac, data, size,
                      (int)frame->disable_cdf_update);
    fprintf(stderr, "MSAC0 r=%x d=%016llx c=%d\n", td->msac.rng,
            (unsigned long long)td->msac.dif, td->msac.cnt);

    sb_log2 = 6U + seq->sb128;
    sb_size = 1U << sb_log2;
    sbw = (frame->width[0] + sb_size - 1U) >> sb_log2;
    sbh = (frame->height + sb_size - 1U) >> sb_log2;

    /* The partition decoder expects dimensions in 4x4 units.  Its above/left
     * context maps are frame-wide (8x8 units), matching dav1d's f->a
     * BlockContexts; the +1 cell covers the last half-SB block. */
    pd.msac = &td->msac;
    pd.cdf = &td->cdf;
    pd.frame_w4 = (int)((frame->width[0] + 3U) >> 2);
    pd.frame_h4 = (int)((frame->height + 3U) >> 2);
    above_n = ((pd.frame_w4 + 15) >> 1) + 1;
    left_n = ((pd.frame_h4 + 15) >> 1) + 1;
    above = (stbv_u8 *)malloc((size_t)above_n);
    left = (stbv_u8 *)malloc((size_t)left_n);
    if (!above || !left) {
        if (above) free(above);
        if (left) free(left);
        return -6;
    }
    memset(above, 0, (size_t)above_n);
    memset(left, 0, (size_t)left_n);
    pd.above = above;
    pd.left = left;
    pd.above_n = above_n;
    pd.left_n = left_n;
    w.td = td;
    w.cb = cb;
    w.opaque = opaque;
    pd.leaf = stb_av1_tile_leaf_dispatch;
    pd.opaque = &w;

    if (!sbw || !sbh) {
        free(above);
        free(left);
        return -1;
    }

    {
        unsigned int sy;
        for (sy = 0; sy < sbh; sy++) {
            unsigned int sx;
            memset(left, 0, (size_t)left_n);
            if (sy == 0)
                memset(above, 0, (size_t)above_n);
            if (row_cb) row_cb(opaque);
            for (sx = 0; sx < sbw; sx++) {
                int bl = seq->sb128 ? STBV_AV1_BL_128X128 :
                                       STBV_AV1_BL_64X64;
                int bx = (int)(sx * (sb_size >> 2));
                int by = (int)(sy * (sb_size >> 2));
                if (stbv_av1_partition_decode_sb(&pd, bl, bx, by)) {
                    td->error = 1;
                    rc = -1;
                    goto done;
                }
            }
        }
    }
    rc = td->msac.buf_pos <= td->msac.buf_end ? 0 : -1;
done:
    free(above);
    free(left);
    return rc;
}

#endif
