/*
 * AV1 partition-tree decoder derived from dav1d 1.5.4 src/decode.c.
 * Copyright (c) 2018-2024, VideoLAN and dav1d authors; BSD-2-Clause.
 */
#ifndef STB_AV1_PARTITION_DECODE_H
#define STB_AV1_PARTITION_DECODE_H

#ifdef STB_AV1_PARTITION_DEBUG
#include <stdio.h>
/* Plain #ifdef blocks instead of variadic macros (MSVC6/C89); the 64-bit
 * MSAC state prints as two 32-bit halves to stay off '%ll' formats. */
/* #define STBV_AV1_PART_TRACE */ 1
#endif

#ifndef STB_AV1_PARTITION_H
#error "include stb_av1_partition.h first"
#endif
#ifndef STB_AV1_MSAC_H
#error "include stb_av1_msac.h first"
#endif
#ifndef STB_AV1_CDF_H
#error "include stb_av1_cdf.h first"
#endif

static const stbv_u8 stbv_av1_al_part_ctx[2][STBV_AV1_N_BL_LEVELS]
                                          [STBV_AV1_N_PARTITIONS] = {
    {
        { 0x00, 0x00, 0x10, 0xff, 0x00, 0x10, 0x10, 0x10, 0xff, 0xff },
        { 0x10, 0x10, 0x18, 0xff, 0x10, 0x18, 0x18, 0x18, 0x10, 0x1c },
        { 0x18, 0x18, 0x1c, 0xff, 0x18, 0x1c, 0x1c, 0x1c, 0x18, 0x1e },
        { 0x1c, 0x1c, 0x1e, 0xff, 0x1c, 0x1e, 0x1e, 0x1e, 0x1c, 0x1f },
        { 0x1e, 0x1e, 0x1f, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
    }, {
        { 0x00, 0x10, 0x00, 0xff, 0x10, 0x10, 0x00, 0x10, 0xff, 0xff },
        { 0x10, 0x18, 0x10, 0xff, 0x18, 0x18, 0x10, 0x18, 0x1c, 0x10 },
        { 0x18, 0x1c, 0x18, 0xff, 0x1c, 0x1c, 0x18, 0x1c, 0x1e, 0x18 },
        { 0x1c, 0x1e, 0x1c, 0xff, 0x1e, 0x1e, 0x1c, 0x1e, 0x1f, 0x1c },
        { 0x1e, 0x1f, 0x1e, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
    }
};

/* case_set_upto16(): dav1d writes min(pow2(hsz),16) cells. */
static void stbv_av1_partition_set_context(stbv_u8 *above, stbv_u8 *left,
                                            int above_n, int left_n,
                                            int bx8, int by8, int hsz,
                                            int bl, int bp)
{
    int n, i;
    stbv_u8 av = stbv_av1_al_part_ctx[0][bl][bp];
    stbv_u8 lv = stbv_av1_al_part_ctx[1][bl][bp];

    n = 1;
    while (n < hsz && n < 16)
        n <<= 1;

    for (i = 0; i < n; i++) {
        if (bx8 + i >= 0 && bx8 + i < above_n)
            above[bx8 + i] = av;
        if (by8 + i >= 0 && by8 + i < left_n)
            left[by8 + i] = lv;
    }
}

static unsigned int stbv_av1_gather_top_partition_prob(const stbv_u16 *pc,
                                                        int bl)
{
    unsigned int out;
    out = (unsigned int)pc[STBV_AV1_PARTITION_V - 1] - pc[STBV_AV1_PARTITION_T_TOP_SPLIT];
    out += pc[STBV_AV1_PARTITION_T_LEFT_SPLIT - 1];
    if (bl != STBV_AV1_BL_128X128)
        out += (unsigned int)pc[STBV_AV1_PARTITION_V4 - 1] -
               pc[STBV_AV1_PARTITION_T_RIGHT_SPLIT];
    return out;
}

static unsigned int stbv_av1_gather_left_partition_prob(const stbv_u16 *pc,
                                                          int bl)
{
    unsigned int out;
    out = (unsigned int)pc[STBV_AV1_PARTITION_H - 1] - pc[STBV_AV1_PARTITION_H];
    out += (unsigned int)pc[STBV_AV1_PARTITION_SPLIT - 1] -
           pc[STBV_AV1_PARTITION_T_LEFT_SPLIT];
    if (bl != STBV_AV1_BL_128X128)
        out += (unsigned int)pc[STBV_AV1_PARTITION_H4 - 1] -
               pc[STBV_AV1_PARTITION_H4];
    return out;
}

typedef struct stbv_av1_partition_decoder stbv_av1_partition_decoder;

typedef int (*stbv_av1_partition_leaf_fn)(
    stbv_av1_partition_decoder *d, int bl, int bs, int bp,
    int bx, int by, void *opaque);

struct stbv_av1_partition_decoder {
    struct stb_av1_msac *msac;
    stbv_av1_cdf *cdf;
    int frame_w4;
    int frame_h4;
    stbv_u8 *above;
    stbv_u8 *left;
    int above_n;
    int left_n;
    int ctx_x4;
    int ctx_y4;
    stbv_av1_partition_leaf_fn leaf;
    void *opaque;
};

static int stbv_av1_partition_emit(stbv_av1_partition_decoder *d,
                                    int bl, int bs, int bp,
                                    int bx, int by)
{
    return d->leaf(d, bl, bs, bp, bx, by, d->opaque);
}

static int stbv_av1_partition_decode_sb(stbv_av1_partition_decoder *d,
                                        int bl, int bx, int by)
{
    int hsz;
    int have_h_split, have_v_split;
    int bx8, by8, ctx;
    stbv_u16 *pc;
    int bp = 0;
    int bs;

    hsz = 16 >> bl;
    have_h_split = d->frame_w4 > bx + hsz;
    have_v_split = d->frame_h4 > by + hsz;

    if (!have_h_split && !have_v_split) {
        if (bl >= STBV_AV1_BL_8X8)
            return stbv_av1_partition_emit(d, bl, STBV_AV1_BS_4x4,
                                           STBV_AV1_PARTITION_SPLIT, bx, by);
        return stbv_av1_partition_decode_sb(d, bl + 1, bx, by);
    }

    bx8 = (bx - d->ctx_x4) >> 1;
    by8 = (by - d->ctx_y4) >> 1;
    if (bx8 < 0 || bx8 >= d->above_n || by8 < 0 || by8 >= d->left_n)
        return -1;
    ctx = ((d->above[bx8] >> (4 - bl)) & 1) |
          (((d->left[by8] >> (4 - bl)) & 1) << 1);
    pc = stbv_av1_partition_cdf(d->cdf->partition, bl, ctx);

    if (have_h_split && have_v_split) {
        bp = (int)stb_av1_msac_symbol(d->msac, pc,
                                      stbv_av1_partition_type_count[bl]);
#ifdef STBV_AV1_PART_TRACE
        fprintf(stderr, "P y=%d x=%d bl=%d ctx=%d bp=%d r=%u d=%08x%08x c=%d c0=%d c8=%d\n",
                by, bx, bl, ctx, bp, d->msac->rng,
                (unsigned)(d->msac->dif >> 32), (unsigned)d->msac->dif,
                d->msac->cnt, pc[0], pc[8]);
#endif
        if (bp < 0 || bp >= STBV_AV1_N_PARTITIONS)
            return -1;

        if (bp == STBV_AV1_PARTITION_SPLIT) {
            if (bl == STBV_AV1_BL_8X8) {
                int r;
                r = stbv_av1_partition_emit(d, bl, STBV_AV1_BS_4x4, bp, bx, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, STBV_AV1_BS_4x4, bp, bx + 1, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, STBV_AV1_BS_4x4, bp, bx, by + 1);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, STBV_AV1_BS_4x4, bp,
                                            bx + 1, by + 1);
                if (r) return r;
            } else {
                if (stbv_av1_partition_decode_sb(d, bl + 1, bx, by)) return -1;
                if (stbv_av1_partition_decode_sb(d, bl + 1, bx + hsz, by)) return -1;
                if (stbv_av1_partition_decode_sb(d, bl + 1, bx, by + hsz)) return -1;
                if (stbv_av1_partition_decode_sb(d, bl + 1, bx + hsz, by + hsz)) return -1;
            }
        } else {
            bs = stbv_av1_block_sizes[bl][bp][0];
            if (bs == 0xff)
                return -1;
            if (bp == STBV_AV1_PARTITION_NONE) {
                int r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by);
                if (r) return r;
            } else if (bp == STBV_AV1_PARTITION_H) {
                int r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by + hsz);
                if (r) return r;
            } else if (bp == STBV_AV1_PARTITION_V) {
                int r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, bs, bp, bx + hsz, by);
                if (r) return r;
            } else if (bp == STBV_AV1_PARTITION_T_TOP_SPLIT) {
                int r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, bs, bp, bx + hsz, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, stbv_av1_block_sizes[bl][bp][1], bp,
                                            bx, by + hsz);
                if (r) return r;
            } else if (bp == STBV_AV1_PARTITION_T_BOTTOM_SPLIT) {
                int r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, stbv_av1_block_sizes[bl][bp][1], bp,
                                            bx, by + hsz);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, stbv_av1_block_sizes[bl][bp][1], bp,
                                            bx + hsz, by + hsz);
                if (r) return r;
            } else if (bp == STBV_AV1_PARTITION_T_LEFT_SPLIT) {
                int r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by + hsz);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, stbv_av1_block_sizes[bl][bp][1], bp,
                                            bx + hsz, by);
                if (r) return r;
            } else if (bp == STBV_AV1_PARTITION_T_RIGHT_SPLIT) {
                int r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, stbv_av1_block_sizes[bl][bp][1], bp,
                                            bx + hsz, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, stbv_av1_block_sizes[bl][bp][1], bp,
                                            bx + hsz, by + hsz);
                if (r) return r;
            } else if (bp == STBV_AV1_PARTITION_H4) {
                int i, r;
                int step = hsz >> 1;
                for (i = 0; i < 4; i++) {
                    if (by + i * step >= d->frame_h4) break;
                    r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by + i * step);
                    if (r) return r;
                }
            } else if (bp == STBV_AV1_PARTITION_V4) {
                int i, r;
                int step = hsz >> 1;
                for (i = 0; i < 4; i++) {
                    if (bx + i * step >= d->frame_w4) break;
                    r = stbv_av1_partition_emit(d, bl, bs, bp, bx + i * step, by);
                    if (r) return r;
                }
            } else {
                return -1;
            }
        }

        /* dav1d decode.c:2414: once per level over the parent span
         * (case_set_upto16(ulog2(hsz))), skipping un-split interior SPLTs. */
        if (bp != STBV_AV1_PARTITION_SPLIT || bl == STBV_AV1_BL_8X8)
            stbv_av1_partition_set_context(d->above, d->left, d->above_n,
                                           d->left_n, bx8, by8,
                                           hsz, bl, bp);
    } else if (have_h_split) {
        unsigned int is_split;
        is_split = stb_av1_msac_bool(d->msac,
                                     stbv_av1_gather_top_partition_prob(pc, bl));
#ifdef STBV_AV1_PART_TRACE
        fprintf(stderr, "P y=%d x=%d bl=%d ctx=%d bp=%d r=%u d=%08x c=%d\n",
                by, bx, bl, ctx, is_split ? STBV_AV1_PARTITION_SPLIT
                                          : STBV_AV1_PARTITION_H,
                d->msac->rng, (unsigned)d->msac->dif, d->msac->cnt);
#endif
        bp = is_split ? (int)STBV_AV1_PARTITION_SPLIT : (int)STBV_AV1_PARTITION_H;
        if (is_split) {
            if (bl >= STBV_AV1_BL_8X8) {
                int r;
                r = stbv_av1_partition_emit(d, bl, STBV_AV1_BS_4x4, bp, bx, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, STBV_AV1_BS_4x4, bp, bx + 1, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, STBV_AV1_BS_4x4, bp, bx, by + 1);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, STBV_AV1_BS_4x4, bp, bx + 1, by + 1);
                if (r) return r;
            } else {
                if (stbv_av1_partition_decode_sb(d, bl + 1, bx, by)) return -1;
                if (stbv_av1_partition_decode_sb(d, bl + 1, bx + hsz, by)) return -1;
            }
        } else {
            int r;
            bs = stbv_av1_block_sizes[bl][STBV_AV1_PARTITION_H][0];
            if (bs == 0xff)
                return -1;
            r = stbv_av1_partition_emit(d, bl, bs, STBV_AV1_PARTITION_H, bx, by);
            if (r) return r;
        }
        /* dav1d sets bp before reaching the shared write below. */
        if (bp != STBV_AV1_PARTITION_SPLIT || bl == STBV_AV1_BL_8X8)
            stbv_av1_partition_set_context(d->above, d->left, d->above_n,
                                           d->left_n, bx8, by8,
                                           hsz, bl, bp);
    } else {
        unsigned int is_split;
        is_split = stb_av1_msac_bool(d->msac,
                                     stbv_av1_gather_left_partition_prob(pc, bl));
#ifdef STBV_AV1_PART_TRACE
        fprintf(stderr, "P y=%d x=%d bl=%d ctx=%d bp=%d r=%u d=%08x c=%d\n",
                by, bx, bl, ctx, is_split ? STBV_AV1_PARTITION_SPLIT
                                          : STBV_AV1_PARTITION_V,
                d->msac->rng, (unsigned)d->msac->dif, d->msac->cnt);
#endif
        bp = is_split ? (int)STBV_AV1_PARTITION_SPLIT : (int)STBV_AV1_PARTITION_V;
        if (is_split) {
            if (bl >= STBV_AV1_BL_8X8) {
                int r;
                r = stbv_av1_partition_emit(d, bl, STBV_AV1_BS_4x4, bp, bx, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, STBV_AV1_BS_4x4, bp, bx + 1, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, STBV_AV1_BS_4x4, bp, bx, by + 1);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, STBV_AV1_BS_4x4, bp, bx + 1, by + 1);
                if (r) return r;
            } else {
                if (stbv_av1_partition_decode_sb(d, bl + 1, bx, by)) return -1;
                if (stbv_av1_partition_decode_sb(d, bl + 1, bx, by + hsz)) return -1;
            }
        } else {
            int r;
            bs = stbv_av1_block_sizes[bl][STBV_AV1_PARTITION_V][0];
            if (bs == 0xff)
                return -1;
            r = stbv_av1_partition_emit(d, bl, bs, STBV_AV1_PARTITION_V, bx, by);
            if (r) return r;
        }
        if (bp != STBV_AV1_PARTITION_SPLIT || bl == STBV_AV1_BL_8X8)
            stbv_av1_partition_set_context(d->above, d->left, d->above_n,
                                           d->left_n, bx8, by8,
                                           hsz, bl, bp);
    }

    return 0;
}

static void stbv_av1_partition_decoder_init(stbv_av1_partition_decoder *d,
                                             struct stb_av1_msac *msac,
                                             stbv_av1_cdf *cdf,
                                             int frame_w4, int frame_h4,
                                             stbv_u8 *above, int above_n,
                                             stbv_u8 *left, int left_n,
                                             stbv_av1_partition_leaf_fn leaf,
                                             void *opaque)
{
    d->msac = msac;
    d->cdf = cdf;
    d->frame_w4 = frame_w4;
    d->frame_h4 = frame_h4;
    d->above = above;
    d->left = left;
    d->above_n = above_n;
    d->left_n = left_n;
    d->ctx_x4 = 0;
    d->ctx_y4 = 0;
    d->leaf = leaf;
    d->opaque = opaque;
}

#endif /* STB_AV1_PARTITION_DECODE_H */
