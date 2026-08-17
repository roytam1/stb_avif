/*
 * AV1 partition-tree decoder derived from dav1d 1.5.4 src/decode.c.
 *
 * Copyright (c) 2018-2024, VideoLAN and dav1d authors
 * Copyright (c) 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * BSD-2-Clause. See the dav1d COPYING file for the complete license text.
 */
#ifndef STB_AV1_PARTITION_DECODE_H
#define STB_AV1_PARTITION_DECODE_H

#ifndef STB_AV1_PARTITION_H
#error "include stb_av1_partition.h first"
#endif
#ifndef STB_AV1_MSAC_H
#error "include stb_av1_msac.h first"
#endif
#ifndef STB_AV1_CDF_H
#error "include stb_av1_cdf.h first"
#endif

/* dav1d_al_part_ctx, used when a decoded block updates the partition
 * contexts on its top and left edges. */
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

/* Set the top/left partition contexts exactly in the form used by dav1d's
 * case_set_upto16(). The context arrays are indexed in 8x8 units. */
static void stbv_av1_partition_set_context(stbv_u8 *above, stbv_u8 *left,
                                            int bx8, int by8, int hsz,
                                            int bl, int bp)
{
    int n, i;
    stbv_u8 av, lv;

    av = stbv_av1_al_part_ctx[0][bl][bp];
    lv = stbv_av1_al_part_ctx[1][bl][bp];
    if (av == 0xff || lv == 0xff)
        return;

    /* hsz is 4x4 units. dav1d's case_set_upto16(ulog2(hsz)) writes
     * 1,2,4,8,16 entries. */
    n = 1;
    while (n < hsz && n < 16)
        n <<= 1;
    if (n > 16)
        n = 16;

    for (i = 0; i < n; i++) {
        if (bx8 + i < 32)
            above[bx8 + i] = av;
        if (by8 + i < 32)
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
    stbv_u8 above[32];
    stbv_u8 left[32];
    stbv_av1_partition_leaf_fn leaf;
    void *opaque;
};

static int stbv_av1_partition_emit(stbv_av1_partition_decoder *d,
                                    int bl, int bs, int bp,
                                    int bx, int by)
{
    int hsz = 16 >> bl;
    int bx8 = (bx & 31) >> 1;
    int by8 = (by & 31) >> 1;
    int r;

    r = d->leaf(d, bl, bs, bp, bx, by, d->opaque);
    if (r) return r;
    if (bp != STBV_AV1_PARTITION_SPLIT || bl == STBV_AV1_BL_8X8)
        stbv_av1_partition_set_context(d->above, d->left, bx8, by8,
                                       hsz, bl, bp);
    return 0;
}

static int stbv_av1_partition_decode_sb(stbv_av1_partition_decoder *d,
                                        int bl, int bx, int by)
{
    int hsz;
    int have_h_split, have_v_split;
    int bx8, by8, ctx;
    stbv_u16 *pc;
    int bp;
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

    bx8 = (bx & 31) >> 1;
    by8 = (by & 31) >> 1;
    ctx = ((d->above[bx8] >> (4 - bl)) & 1) |
          (((d->left[by8] >> (4 - bl)) & 1) << 1);
    pc = stbv_av1_partition_cdf(d->cdf->partition, bl, ctx);

    if (have_h_split && have_v_split) {
        bp = (int)stb_av1_msac_symbol(d->msac, pc,
                                      stbv_av1_partition_type_count[bl]);
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
                return stbv_av1_partition_emit(d, bl, STBV_AV1_BS_4x4, bp,
                                   bx + 1, by + 1);
            }
            if (stbv_av1_partition_decode_sb(d, bl + 1, bx, by)) return -1;
            if (stbv_av1_partition_decode_sb(d, bl + 1, bx + hsz, by)) return -1;
            if (stbv_av1_partition_decode_sb(d, bl + 1, bx, by + hsz)) return -1;
            if (stbv_av1_partition_decode_sb(d, bl + 1, bx + hsz, by + hsz)) return -1;
        } else {
            bs = stbv_av1_block_sizes[bl][bp][0];
            if (bs == 0xff)
                return -1;
            if (bp == STBV_AV1_PARTITION_NONE)
                return stbv_av1_partition_emit(d, bl, bs, bp, bx, by);
            if (bp == STBV_AV1_PARTITION_H) {
                int r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by);
                if (r) return r;
                return stbv_av1_partition_emit(d, bl, bs, bp, bx, by + hsz);
            }
            if (bp == STBV_AV1_PARTITION_V) {
                int r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by);
                if (r) return r;
                return stbv_av1_partition_emit(d, bl, bs, bp, bx + hsz, by);
            }
            if (bp == STBV_AV1_PARTITION_T_TOP_SPLIT) {
                int r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, bs, bp, bx + hsz, by);
                if (r) return r;
                return stbv_av1_partition_emit(d, bl, stbv_av1_block_sizes[bl][bp][1], bp,
                                               bx, by + hsz);
            }
            if (bp == STBV_AV1_PARTITION_T_BOTTOM_SPLIT) {
                int r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, stbv_av1_block_sizes[bl][bp][1], bp,
                                            bx, by + hsz);
                if (r) return r;
                return stbv_av1_partition_emit(d, bl, stbv_av1_block_sizes[bl][bp][1], bp,
                                               bx + hsz, by + hsz);
            }
            if (bp == STBV_AV1_PARTITION_T_LEFT_SPLIT) {
                int r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by + hsz);
                if (r) return r;
                return stbv_av1_partition_emit(d, bl, stbv_av1_block_sizes[bl][bp][1], bp,
                                               bx + hsz, by);
            }
            if (bp == STBV_AV1_PARTITION_T_RIGHT_SPLIT) {
                int r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by);
                if (r) return r;
                r = stbv_av1_partition_emit(d, bl, stbv_av1_block_sizes[bl][bp][1], bp,
                                            bx + hsz, by);
                if (r) return r;
                return stbv_av1_partition_emit(d, bl, stbv_av1_block_sizes[bl][bp][1], bp,
                                               bx + hsz, by + hsz);
            }
            if (bp == STBV_AV1_PARTITION_H4) {
                int i, r;
                for (i = 0; i < 4; i++) {
                    if (by + i * (hsz / 4) >= d->frame_h4) break;
                    r = stbv_av1_partition_emit(d, bl, bs, bp, bx, by + i * (hsz / 4));
                    if (r) return r;
                }
                return 0;
            }
            if (bp == STBV_AV1_PARTITION_V4) {
                int i, r;
                for (i = 0; i < 4; i++) {
                    if (bx + i * (hsz / 4) >= d->frame_w4) break;
                    r = stbv_av1_partition_emit(d, bl, bs, bp, bx + i * (hsz / 4), by);
                    if (r) return r;
                }
                return 0;
            }
        }
    } else if (have_h_split) {
        unsigned int is_split;
        is_split = stb_av1_msac_bool(d->msac,
                                     stbv_av1_gather_top_partition_prob(pc, bl));
        if (is_split) {
            if (bl >= STBV_AV1_BL_8X8) return -1;
            if (stbv_av1_partition_decode_sb(d, bl + 1, bx, by)) return -1;
            return stbv_av1_partition_decode_sb(d, bl + 1, bx + hsz, by);
        }
        bs = stbv_av1_block_sizes[bl][STBV_AV1_PARTITION_V][0];
        if (bs == 0xff) return -1;
        return stbv_av1_partition_emit(d, bl, bs, STBV_AV1_PARTITION_V, bx, by);
    } else {
        unsigned int is_split;
        is_split = stb_av1_msac_bool(d->msac,
                                     stbv_av1_gather_left_partition_prob(pc, bl));
        if (is_split) {
            if (bl >= STBV_AV1_BL_8X8) return -1;
            if (stbv_av1_partition_decode_sb(d, bl + 1, bx, by)) return -1;
            return stbv_av1_partition_decode_sb(d, bl + 1, bx, by + hsz);
        }
        bs = stbv_av1_block_sizes[bl][STBV_AV1_PARTITION_H][0];
        if (bs == 0xff) return -1;
        return stbv_av1_partition_emit(d, bl, bs, STBV_AV1_PARTITION_H, bx, by);
    }

    return 0;
}

static void stbv_av1_partition_decoder_init(stbv_av1_partition_decoder *d,
                                             struct stb_av1_msac *msac,
                                             stbv_av1_cdf *cdf,
                                             int frame_w4, int frame_h4,
                                             stbv_av1_partition_leaf_fn leaf,
                                             void *opaque)
{
    int i;
    d->msac = msac;
    d->cdf = cdf;
    d->frame_w4 = frame_w4;
    d->frame_h4 = frame_h4;
    d->leaf = leaf;
    d->opaque = opaque;
    for (i = 0; i < 32; i++) {
        d->above[i] = 0;
        d->left[i] = 0;
    }
}

#endif /* STB_AV1_PARTITION_DECODE_H */
