/*
 * stb_av1_state.h - scalar intra tile neighbor state
 *
 * The state here mirrors the small part of dav1d's TileState that is needed
 * before reconstruction: per-4x4 above/left intra modes.  It is deliberately
 * separate from pixel storage so the entropy syntax can be validated first.
 */
#ifndef STB_AV1_STATE_H
#define STB_AV1_STATE_H

#include <stddef.h>
#include <string.h>

#ifndef STB_AV1_INTRA_H
#error "include stb_av1_intra.h first"
#endif
#ifndef STB_AV1_PARTITION_H
#error "include stb_av1_partition.h first"
#endif

struct stb_av1_intra_state {
    stbv_u8 *above_mode;
    stbv_u8 *left_mode;
    unsigned int above_count;
    unsigned int left_count;
};

static void stb_av1_intra_state_init(struct stb_av1_intra_state *s,
                                     stbv_u8 *above_mode,
                                     unsigned int above_count,
                                     stbv_u8 *left_mode,
                                     unsigned int left_count)
{
    s->above_mode = above_mode;
    s->left_mode = left_mode;
    s->above_count = above_count;
    s->left_count = left_count;
    if (above_mode) memset(above_mode, STBV_AV1_INTRA_DC, above_count);
    if (left_mode) memset(left_mode, STBV_AV1_INTRA_DC, left_count);
}

static int stb_av1_intra_state_decode_leaf(
    struct stb_av1_msac *msac, stbv_av1_cdf *cdf,
    struct stb_av1_intra_state *s,
    int bx4, int by4, int bs,
    int cfl_allowed, int has_chroma,
    struct stb_av1_intra_block *out)
{
    int bw4, bh4, above, left;
    if (!s || !out || bs < 0 || bs >= STBV_AV1_N_BS_SIZES)
        return -1;
    bw4 = stbv_av1_block_dimensions[bs][0];
    bh4 = stbv_av1_block_dimensions[bs][1];
    if (bw4 <= 0 || bh4 <= 0)
        return -1;

    above = (s->above_mode && (unsigned)bx4 < s->above_count) ?
        s->above_mode[bx4] : STBV_AV1_INTRA_DC;
    left = (s->left_mode && (unsigned)by4 < s->left_count) ?
        s->left_mode[by4] : STBV_AV1_INTRA_DC;

    if (stbv_av1_decode_intra_mode(msac, cdf, above, left,
                                   bw4, bh4, cfl_allowed, has_chroma, out))
        return -2;

    return 0;
}

#endif
